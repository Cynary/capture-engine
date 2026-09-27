#pragma once

#include <cstddef>
#include <cstdint>

// Ownership rules for the raw IDXGIFactory2::CreateSwapChainForHwnd slot call
// the temp-swapchain installer performs.
//
// `dx12_hook_oCreateSwapChainForHwnd` is the pre-patch value of one specific
// factory vtable slot, saved together with the vtable it was taken from. That
// slot function interprets its first argument as an object of that vtable's
// class, so passing any other object — e.g. a ReShade-style factory proxy
// returned by a hooked CreateDXGIFactory1 — reads garbage C++ fields and
// crashes inside dxgi (sessions 20260813_004853 / 20260813_004923: the proxy's
// +0xE8 is not the CDXGIFactory adapter table). The call is legal exactly when
// the factory object's vtable pointer equals the vtable the saved slot was
// captured from.
namespace ce::dx12_factory_slot {

inline bool ShouldInvokeSavedCreateSwapChainForHwndSlot(const void* savedSlotVtable,
                                                        const void* factoryObject) {
    if (savedSlotVtable == nullptr || factoryObject == nullptr) {
        return false;
    }
    const void* const* objectVtable = static_cast<const void* const*>(factoryObject);
    return *objectVtable == savedSlotVtable;
}

// True when `entry` begins with the two foreign hook shapes CE's bypass
// trampolines understand: a relative E9 jump or the x64 indirect FF 25 entry
// used by Microsoft Detours and common custom hooks.
inline bool HasForeignEntryJump(const void* entry) {
    if (entry == nullptr) {
        return false;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(entry);
    return bytes[0] == 0xE9 || (bytes[0] == 0xFF && bytes[1] == 0x25);
}

// The Steam overlay tracks a D3D12 swapchain - and on ResizeBuffers releases the
// reference it holds on every back buffer - only for chains it saw being
// created through its own hook on the DXGI factory's CreateSwapChain slots. It
// installs that hook when the game creates a factory, and skips a slot that
// already points into another module. CE claims those slots when it starts, so
// with an early injection they are CE's before the game's first factory exists:
// Talos Reawakened (logs/20260927_023858) got a Steam-held reference on each of
// the six back buffers and every resolution change was refused
// (DXGI_ERROR_INVALID_CALL, then Unreal's fatal error), with or without frame
// generation. With CE injected late the overlay hooks first and CE chains above
// it, which works.
//
// CE therefore hands its factory slots back to their predecessor while it
// forwards the application's CreateDXGIFactory*, so a slot-hooking overlay sees
// pristine slots at exactly the moment it hooks them, and takes them again
// afterwards above whatever they hold. Only while it can matter: a third-party
// overlay is loaded and CE's predecessor is still the system DXGI function (once
// an overlay sits below CE there is nothing left to hand over). A bounded number
// of handbacks that change nothing ends it - an overlay that did not hook by
// then does not hook these slots, and every handback briefly takes CE out of
// swapchain creation on other threads.
inline constexpr unsigned kMaxFactorySlotHandbacksWithoutForeignHook = 8;

inline bool ShouldHandBackFactorySlotsAroundFactoryCreate(bool slotsClaimedByCE, bool thirdPartyOverlayLoaded,
                                                          bool predecessorIsSystemDxgi,
                                                          unsigned handbacksWithoutForeignHook) {
    return slotsClaimedByCE && thirdPartyOverlayLoaded && predecessorIsSystemDxgi &&
           handbacksWithoutForeignHook < kMaxFactorySlotHandbacksWithoutForeignHook;
}

// The temp swapchain CE creates to find the Present implementation must not
// enter any overlay handler (see CreateTempSwapChainViaFactorySlot). Once a
// handback put an overlay below CE, CE's saved predecessor IS that overlay's
// handler, so a slot holding CE's own detour resolves to the system function
// captured at install time, never to the predecessor.
inline const void* ResolveTempSwapChainFactorySlot(const void* slot, const void* ceDetour,
                                                   const void* systemFunction) {
    return slot == ceDetour ? systemFunction : slot;
}

}  // namespace ce::dx12_factory_slot
