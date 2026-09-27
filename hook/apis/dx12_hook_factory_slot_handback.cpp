#include "dx12_hook_internal.h"

#include "../common/dx12_factory_slot_policy.h"

// See ce::dx12_factory_slot::ShouldHandBackFactorySlotsAroundFactoryCreate for why CE hands its
// DXGI factory CreateSwapChain slots back while the application creates a factory.
//
// A handback takes CE's slot detours out of the slots for the length of one CreateDXGIFactory*
// call. A swapchain another thread creates through those slots meanwhile reaches DXGI (or the
// overlay) without CE's creation detour - to CE it is then a swapchain that already existed, the
// case late injection always has, and CE's Present-side discovery takes it over. CreateSwapChainForHwnd
// additionally keeps CE's inline and deep hooks on the function itself throughout.

namespace {

std::atomic<void**> g_ClaimedFactoryVtable{nullptr};
std::atomic<bool> g_CreateSwapChainClaimed{false};
std::atomic<bool> g_CreateSwapChainForHwndClaimed{false};
std::atomic<unsigned> g_HandbacksWithoutForeignHook{0};
// DXGI's own CreateSwapChain as CE found it in the slot, when that was the system function.
std::atomic<PFN_CreateSwapChain> g_SystemCreateSwapChain{nullptr};
std::mutex g_HandbackMutex;

// Per thread: an overlay's hook can create another factory from inside the forwarded call.
thread_local bool t_InHandback = false;
thread_local bool t_HandedBackCreateSwapChain = false;
thread_local bool t_HandedBackCreateSwapChainForHwnd = false;

void DescribeCode(const void* address, char* out, size_t outCount) {
    char path[MAX_PATH] = {};
    HMODULE module = nullptr;
    if (address && ce::overlay_compat::TryGetModulePathFromCodeAddress(address, path, sizeof(path), &module) &&
        module) {
        const char* base = strrchr(path, '\\');
        snprintf(out, outCount, "%s+0x%llX", base ? base + 1 : path,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) -
                                                 reinterpret_cast<uintptr_t>(module)));
    } else {
        snprintf(out, outCount, "%p", address);
    }
}

// Returns true when the slot now has a foreign hook below CE.
bool ReclaimSlot(void** entry, void** predecessorStorage, const char* method, const char* source) {
    void* predecessor = nullptr;
    const VTableHook::Status status = VTableHook::TakeBack(entry, predecessorStorage, &predecessor);
    char owner[MAX_PATH + 32] = {};
    DescribeCode(predecessor, owner, sizeof(owner));
    if (status != VTableHook::Success) {
        HookLogImportant(
            "DX12 factory slot handback: could not take %s back (%s, source=%s) - CE sees swapchain creation "
            "through that slot no longer; CreateSwapChainForHwnd keeps its inline hooks",
            method, VTableHook::StatusToString(status), source ? source : "?");
        return false;
    }
    const bool foreign = !DXGIShared::IsAddressInsideSystemDXGI(predecessor);
    HookLogImportant("DX12 factory slot handback: %s taken back above %s (%s, source=%s)", method, owner,
                     foreign ? "another module hooked it during the handback - CE now chains above it"
                             : "unchanged - nothing hooked it",
                     source ? source : "?");
    return foreign;
}

}  // namespace

void DX12_NoteFactorySlotsClaimed(void** vtable, bool createSwapChainClaimed, bool createSwapChainForHwndClaimed) {
    if (createSwapChainClaimed &&
        DXGIShared::IsAddressInsideSystemDXGI(reinterpret_cast<const void*>(dx12_hook_oCreateSwapChainGlobal))) {
        g_SystemCreateSwapChain.store(dx12_hook_oCreateSwapChainGlobal, std::memory_order_relaxed);
    }
    g_CreateSwapChainClaimed.store(createSwapChainClaimed, std::memory_order_relaxed);
    g_CreateSwapChainForHwndClaimed.store(createSwapChainForHwndClaimed, std::memory_order_relaxed);
    g_ClaimedFactoryVtable.store(vtable, std::memory_order_release);
}

PFN_CreateSwapChain DX12_ResolveInternalProbeCreateSwapChain() {
    // CE's own D3D10/11 probe swapchains keep reaching DXGI directly, as they did before any
    // handback put an overlay's CreateSwapChain handler below CE: an overlay has no business
    // seeing CE's hidden probe, and Steam's handler is not safe to enter before Steam has drawn
    // on a real swapchain (see CreateTempSwapChainViaFactorySlot). Without a recorded system
    // function (CE injected after the overlay hooked) the predecessor is what it always was.
    const PFN_CreateSwapChain system = g_SystemCreateSwapChain.load(std::memory_order_relaxed);
    return system ? system : dx12_hook_oCreateSwapChainGlobal;
}

bool DX12_BeginFactorySlotHandback(const char* source) {
    void** const vtable = g_ClaimedFactoryVtable.load(std::memory_order_acquire);
    if (!vtable || t_InHandback || HookIsShuttingDown()) {
        return false;
    }
    const bool claimed = g_CreateSwapChainClaimed.load(std::memory_order_relaxed) ||
                         g_CreateSwapChainForHwndClaimed.load(std::memory_order_relaxed);
    const bool overlayLoaded = ce::overlay_compat::IsThirdPartyOverlayLoaded();
    // Both predecessors are published together; the CreateSwapChain one decides when it exists.
    const void* predecessor = g_CreateSwapChainClaimed.load(std::memory_order_relaxed)
                                  ? reinterpret_cast<const void*>(dx12_hook_oCreateSwapChainGlobal)
                                  : reinterpret_cast<const void*>(dx12_hook_oCreateSwapChainForHwndGlobal);
    const bool predecessorIsSystemDxgi = DXGIShared::IsAddressInsideSystemDXGI(predecessor);
    if (!ce::dx12_factory_slot::ShouldHandBackFactorySlotsAroundFactoryCreate(
            claimed, overlayLoaded, predecessorIsSystemDxgi,
            g_HandbacksWithoutForeignHook.load(std::memory_order_relaxed))) {
        return false;
    }
    // Never wait: another thread's handback already gives the overlay its pristine slots.
    if (!g_HandbackMutex.try_lock()) {
        return false;
    }
    t_InHandback = true;
    t_HandedBackCreateSwapChain = g_CreateSwapChainClaimed.load(std::memory_order_relaxed) &&
                                  VTableHook::HandBack(&vtable[10]) == VTableHook::Success;
    t_HandedBackCreateSwapChainForHwnd = g_CreateSwapChainForHwndClaimed.load(std::memory_order_relaxed) &&
                                         VTableHook::HandBack(&vtable[15]) == VTableHook::Success;
    if (!t_HandedBackCreateSwapChain && !t_HandedBackCreateSwapChainForHwnd) {
        t_InHandback = false;
        g_HandbackMutex.unlock();
        return false;
    }
    const char* overlay = ce::overlay_compat::GetLoadedThirdPartyOverlayModuleName();
    HookLogImportant(
        "DX12 factory slot handback #%u: CreateSwapChain=%d CreateSwapChainForHwnd=%d handed back to DXGI for %s "
        "(overlay=%s) so a slot-hooking overlay can install its factory hooks below CE",
        g_HandbacksWithoutForeignHook.load(std::memory_order_relaxed) + 1, t_HandedBackCreateSwapChain ? 1 : 0,
        t_HandedBackCreateSwapChainForHwnd ? 1 : 0, source ? source : "?", overlay ? overlay : "?");
    return true;
}

void DX12_EndFactorySlotHandback(const char* source) {
    void** const vtable = g_ClaimedFactoryVtable.load(std::memory_order_acquire);
    if (!t_InHandback || !vtable) {
        return;
    }
    bool foreignBelowCE = false;
    if (t_HandedBackCreateSwapChain) {
        foreignBelowCE |= ReclaimSlot(&vtable[10], reinterpret_cast<void**>(&dx12_hook_oCreateSwapChainGlobal),
                                      "CreateSwapChain", source);
    }
    if (t_HandedBackCreateSwapChainForHwnd) {
        foreignBelowCE |=
            ReclaimSlot(&vtable[15], reinterpret_cast<void**>(&dx12_hook_oCreateSwapChainForHwndGlobal),
                        "CreateSwapChainForHwnd", source);
    }
    t_HandedBackCreateSwapChain = false;
    t_HandedBackCreateSwapChainForHwnd = false;
    t_InHandback = false;
    if (!foreignBelowCE) {
        const unsigned unchanged = g_HandbacksWithoutForeignHook.fetch_add(1, std::memory_order_relaxed) + 1;
        if (unchanged == ce::dx12_factory_slot::kMaxFactorySlotHandbacksWithoutForeignHook) {
            HookLogImportant(
                "DX12 factory slot handback: %u handbacks and no overlay hooked the factory slots - stopping; the "
                "loaded overlay does not track swapchains through them",
                unchanged);
        }
    }
    g_HandbackMutex.unlock();
}
