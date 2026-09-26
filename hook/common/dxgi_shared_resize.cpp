#include "dxgi_shared_internal.h"
#include "present_pacing_policy.h"
#include "resize_reference_probe.h"
#include "swapchain_flag_policy.h"

namespace DXGIShared {
namespace {

// The application resize contract, in one place for ResizeBuffers and
// ResizeBuffers1.
//
// Two distinct jobs, and they must not be conflated:
//
//  1. The waitable-object bit CE may have added at creation has to be hidden
//     again.  DXGI compares the caller's flags against the chain's creation
//     flags and fails the call with E_INVALIDARG on any disagreement in that
//     bit, so the live descriptor - not the current config - is the authority.
//     Reading it back also makes the rewrite correct for a chain created before
//     the override existed and for a config reloaded mid-session, where
//     re-deriving intent from the config would produce the opposite error.
//
//  2. `backbuffer_count` is re-applied to BufferCount, but only when the caller
//     named a count at all.  BufferCount == 0 means "keep the existing count"
//     in DXGI, so substituting the configured depth there would silently resize
//     a chain the application intended to leave alone.
void ReconcileApplicationResizeRequest(IDXGISwapChain* pSwapChain, UINT& BufferCount, UINT& SwapChainFlags,
                                       const char* source) {
    if (!pSwapChain) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    const bool haveDesc = SUCCEEDED(pSwapChain->GetDesc(&scDesc));
    if (haveDesc) {
        const UINT reconciled =
            ce::swapchain_flag_policy::ReconcileApplicationResizeFlags(SwapChainFlags, scDesc.Flags);
        if (reconciled != SwapChainFlags) {
            static std::atomic<uint32_t> s_reconcileLogs{0};
            const uint32_t logIndex = s_reconcileLogs.fetch_add(1, std::memory_order_relaxed);
            if (logIndex < 8 || (logIndex % 256) == 0) {
                HookLogImportant(
                    "%s: Reconciling application resize flags 0x%X -> 0x%X against creation flags 0x%X - DXGI "
                    "rejects any disagreement in the frame-latency waitable bit with E_INVALIDARG",
                    source, SwapChainFlags, reconciled, scDesc.Flags);
            }
            SwapChainFlags = reconciled;
        }
    }

    const auto& cfg = GetActiveGraphicsConfig();
    // Same presentation-ownership rule as the pacing wait: while the CE Vulkan
    // layer owns presentation this swapchain is the Vulkan runtime's transport,
    // and the resize must forward the runtime's own BufferCount byte-for-byte.
    if (!ce::present_pacing_policy::ShouldApplyCePresentationPolicy(IsVulkanActive()) ||
        !HasBackbufferCountOverride(cfg.backbufferCount)) {
        return;
    }
    if (BufferCount == 0) {
        return;
    }

    const UINT requested = static_cast<UINT>(cfg.backbufferCount);
    if (requested == BufferCount) {
        return;
    }
    if (haveDesc && ce::swapchain_flag_policy::IsFlipSwapEffect(scDesc.SwapEffect) && requested < BufferCount) {
        HookLog("%s: Keeping the application's BufferCount %u above the configured %u (flip model)", source,
                BufferCount, requested);
        return;
    }
    HookLogImportant("%s: Overriding BufferCount %u -> %u", source, BufferCount, requested);
    BufferCount = requested;
}

// Before/after evidence for a D3D12 resize (see resize_reference_probe.h).
// Resizes are rare, so every one is probed; the log is bounded for a title
// that resizes in a loop. Chains of other APIs probe empty and log nothing.
struct D3D12ResizeDiagnostics {
    ce::resize_reference_probe::BackBufferReferences before;
    UINT bufferCount = 0;
};

D3D12ResizeDiagnostics BeginD3D12ResizeDiagnostics(IDXGISwapChain* pSwapChain) {
    D3D12ResizeDiagnostics diagnostics;
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (!pSwapChain || FAILED(pSwapChain->GetDesc(&desc))) {
        return diagnostics;
    }
    diagnostics.bufferCount = desc.BufferCount;
    diagnostics.before =
        ce::resize_reference_probe::Probe<ID3D12Resource>(pSwapChain, desc.BufferCount, __uuidof(ID3D12Resource));
    return diagnostics;
}

void EndD3D12ResizeDiagnostics(const D3D12ResizeDiagnostics& diagnostics, IDXGISwapChain* pSwapChain, HRESULT hr,
                               const char* source, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                               UINT SwapChainFlags) {
    if (diagnostics.before.probed == 0) {
        return;
    }
    static std::atomic<uint32_t> s_succeededLogs{0};
    static std::atomic<uint32_t> s_failedLogs{0};
    const uint32_t logIndex = (FAILED(hr) ? s_failedLogs : s_succeededLogs).fetch_add(1, std::memory_order_relaxed);
    if (logIndex >= (FAILED(hr) ? 32u : 16u) && (logIndex % 64) != 0) {
        return;
    }
    char before[128] = {};
    ce::resize_reference_probe::Format(diagnostics.before, before, sizeof(before));
    char after[128] = "n/a";
    if (FAILED(hr)) {
        // Still the old buffers: the probe shows whether the holder let go.
        ce::resize_reference_probe::Format(ce::resize_reference_probe::Probe<ID3D12Resource>(
                                               pSwapChain, diagnostics.bufferCount, __uuidof(ID3D12Resource)),
                                           after, sizeof(after));
    }
    char capture[160] = {};
    DX12_DescribeCaptureBindingForResize(pSwapChain, capture, sizeof(capture));
    HookLogImportant(
        "%s: D3D12 resize %s hr=0x%08lX sc=%p request(count=%u %ux%u fmt=%d flags=0x%X) buffers=%u "
        "backBufferRefsHeldByOthers before=%s after=%s %s tid=0x%04lX log=%u",
        source, FAILED(hr) ? "FAILED" : "ok", static_cast<unsigned long>(hr), pSwapChain, BufferCount, Width, Height,
        static_cast<int>(NewFormat), SwapChainFlags, diagnostics.bufferCount, before, after, capture,
        GetCurrentThreadId(), logIndex + 1);
}

}  // namespace

// Predecessors of the reconcile-only ResizeBuffers claim. Kept separate from the
// full DX11 resize detour: the only thing CE owes an application swapchain it
// otherwise leaves alone is that the flags it added at creation stay invisible,
// and running the whole resize pipeline for that would change behaviour far
// beyond the fix.
PFN_ResizeBuffers dxgi_shared_oResizeBuffersReconcile = nullptr;
PFN_ResizeBuffers1 dxgi_shared_oResizeBuffers1Reconcile = nullptr;

HRESULT STDMETHODCALLTYPE DetourResizeBuffersReconcileOnly(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width,
                                                           UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (!dxgi_shared_oResizeBuffersReconcile) {
        return DXGI_ERROR_INVALID_CALL;
    }
    if (IsShuttingDown()) {
        return dxgi_shared_oResizeBuffersReconcile(pSwapChain, BufferCount, Width, Height, NewFormat,
                                                   SwapChainFlags);
    }
    ReconcileApplicationResizeRequest(pSwapChain, BufferCount, SwapChainFlags, "ResizeBuffers");
    const D3D12ResizeDiagnostics diagnostics = BeginD3D12ResizeDiagnostics(pSwapChain);
    const HRESULT hr =
        dxgi_shared_oResizeBuffersReconcile(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    EndD3D12ResizeDiagnostics(diagnostics, pSwapChain, hr, "ResizeBuffers", BufferCount, Width, Height, NewFormat,
                              SwapChainFlags);
    return hr;
}

HRESULT STDMETHODCALLTYPE DetourResizeBuffers1ReconcileOnly(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width,
                                                            UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags,
                                                            const UINT* pCreationNodeMask,
                                                            IUnknown* const* ppPresentQueue) {
    if (!dxgi_shared_oResizeBuffers1Reconcile) {
        return DXGI_ERROR_INVALID_CALL;
    }
    if (IsShuttingDown()) {
        return dxgi_shared_oResizeBuffers1Reconcile(pSwapChain, BufferCount, Width, Height, NewFormat,
                                                    SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    }
    ReconcileApplicationResizeRequest(pSwapChain, BufferCount, SwapChainFlags, "ResizeBuffers1");
    const D3D12ResizeDiagnostics diagnostics = BeginD3D12ResizeDiagnostics(pSwapChain);
    const HRESULT hr = dxgi_shared_oResizeBuffers1Reconcile(pSwapChain, BufferCount, Width, Height, NewFormat,
                                                            SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    EndD3D12ResizeDiagnostics(diagnostics, pSwapChain, hr, "ResizeBuffers1", BufferCount, Width, Height, NewFormat,
                              SwapChainFlags);
    return hr;
}

}  // namespace DXGIShared

namespace DXGIShared {
HRESULT STDMETHODCALLTYPE DetourResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
                                              DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    // CRITICAL: Check for global shutdown - if app is closing, don't touch
    // anything
    if (IsShuttingDown()) {
        if (dxgi_shared_oResizeBuffers) {
            return dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        }
        return DXGI_ERROR_INVALID_CALL;
    }

    ReconcileApplicationResizeRequest(pSwapChain, BufferCount, SwapChainFlags, "DetourResizeBuffers");

    // CRITICAL FIX: When Vulkan is active, pass through DXGI ResizeBuffers calls
    if (IsVulkanActive()) {
        return dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    // AGGRESSIVE RECURSION GUARD: Steam overlay causes infinite recursion through
    // hook chain
    if (IsRecursiveResize()) {
        // Recursion detected - resume at CE's saved predecessor rather than
        // re-reading vtable[13]: that slot can be CE's own detour once the
        // reconciliation claim is installed, and re-entering it recurses forever.
        return dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    if (g_SharedState.wrapperResizeDepth.fetch_add(1) > 0) {
        HRESULT hr = dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        g_SharedState.wrapperResizeDepth.fetch_sub(1);
        ReleaseResize();
        return hr;
    }

    // Check if this is our wrapper swapchain - if so, skip resize handling
    void* pWrapperTest = nullptr;
    if (SUCCEEDED(pSwapChain->QueryInterface(IID_CWrapDXGISwapChain, &pWrapperTest))) {
        ((IUnknown*)pWrapperTest)->Release();
        HRESULT hr = dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        g_SharedState.wrapperResizeDepth.fetch_sub(1);
        ReleaseResize();
        return hr;
    }

    g_SharedState.swapchainInvalid.store(true);

    APIType api = DetectAPIType(pSwapChain);

    // CRITICAL FIX: Skip DX12 resize handling during initial swapchain creation
    // Some games call ResizeBuffers immediately after CreateSwapChain
    static std::atomic<int> s_initialResizeCount{0};
    if (api == APIType::D3D12 && s_initialResizeCount.fetch_add(1) == 0) {
        HookLog("DXGI: ResizeBuffers - FIRST D3D12 resize, calling CE's saved predecessor");
        // Not vtable[13]: that slot can be CE's own detour once the
        // reconciliation claim is installed, which would recurse forever.
        HRESULT hr = dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        HookLog("DXGI: ResizeBuffers - first D3D12 resize returned hr=0x%08X", hr);
        g_SharedState.wrapperResizeDepth.fetch_sub(1);

        ReleaseResize();
        return hr;
    }

    if (api == APIType::D3D12)
        HandleDX12ResizeBegin();
    else if (api == APIType::D3D11)
        HandleDX11ResizeBegin();

    HookLog("DXGI: ResizeBuffers - calling oResizeBuffers...");
    const D3D12ResizeDiagnostics diagnostics =
        api == APIType::D3D12 ? BeginD3D12ResizeDiagnostics(pSwapChain) : D3D12ResizeDiagnostics{};
    HRESULT hr = dxgi_shared_oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    HookLog("DXGI: ResizeBuffers - oResizeBuffers returned hr=0x%08X", hr);
    EndD3D12ResizeDiagnostics(diagnostics, pSwapChain, hr, "DetourResizeBuffers", BufferCount, Width, Height,
                              NewFormat, SwapChainFlags);

    if (FAILED(hr)) {
        HookLog("DXGI: ResizeBuffers FAILED with 0x%08X", hr);
    } else {
        HookLog("DXGI: ResizeBuffers SUCCESS");
    }

    // Reset resize flags after resize completes
    if (api == APIType::D3D12) {
        HookLog("DXGI: ResizeBuffers - calling HandleDX12ResizeEnd...");
        HandleDX12ResizeEnd();
        HookLog("DXGI: ResizeBuffers - HandleDX12ResizeEnd returned");
    }

    g_SharedState.swapchainInvalid.store(false);
    g_SharedState.wrapperResizeDepth.fetch_sub(1);
    ReleaseResize();
    return hr;
}
}

namespace DXGIShared {
HRESULT STDMETHODCALLTYPE DetourResizeBuffers1(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
                                               DXGI_FORMAT NewFormat, UINT SwapChainFlags,
                                               const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue) {
    if (IsShuttingDown()) {
        return dxgi_shared_oResizeBuffers1
                   ? dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags,
                                                 pCreationNodeMask, ppPresentQueue)
                   : DXGI_ERROR_INVALID_CALL;
    }
    ReconcileApplicationResizeRequest(pSwapChain, BufferCount, SwapChainFlags, "DetourResizeBuffers1");

    // Vulkan passthrough
    if (IsVulkanActive()) {
        return dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pCreationNodeMask,
                               ppPresentQueue);
    }

    // AGGRESSIVE RECURSION GUARD: Steam overlay causes infinite recursion through
    // hook chain
    if (IsRecursiveResize()) {
        // Recursion detected - resume at CE's saved predecessor rather than
        // re-reading vtable[39]: that slot can be CE's own detour once the
        // reconciliation claim is installed, and re-entering it recurses forever.
        return dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags,
                                           pCreationNodeMask, ppPresentQueue);
    }

    if (g_SharedState.wrapperResizeDepth.fetch_add(1) > 0) {
        HRESULT hr = dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags,
                                     pCreationNodeMask, ppPresentQueue);
        g_SharedState.wrapperResizeDepth.fetch_sub(1);
        ReleaseResize();
        return hr;
    }

    // Check if this is our wrapper swapchain - if so, skip resize handling
    void* pWrapperTest = nullptr;
    if (SUCCEEDED(pSwapChain->QueryInterface(IID_CWrapDXGISwapChain, &pWrapperTest))) {
        ((IUnknown*)pWrapperTest)->Release();
        HRESULT hr = dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags,
                                     pCreationNodeMask, ppPresentQueue);
        g_SharedState.wrapperResizeDepth.fetch_sub(1);
        ReleaseResize();
        return hr;
    }

    g_SharedState.swapchainInvalid.store(true);

    APIType api = DetectAPIType(pSwapChain);

    // CRITICAL FIX: Skip DX12 resize handling during initial swapchain creation
    // Some games call ResizeBuffers immediately after CreateSwapChain
    static std::atomic<int> s_initialResizeCount{0};
    if (api == APIType::D3D12 && s_initialResizeCount.fetch_add(1) == 0) {
        HookLog("DXGI: ResizeBuffers1 - FIRST D3D12 resize, calling CE's saved predecessor");
        // Not vtable[13]: that is ResizeBuffers, so it silently dropped this
        // call's node mask and present queues, and it can now be CE's own detour.
        HRESULT hr = dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags,
                                                 pCreationNodeMask, ppPresentQueue);
        HookLog("DXGI: ResizeBuffers1 - first D3D12 resize returned hr=0x%08X", hr);
        g_SharedState.wrapperResizeDepth.fetch_sub(1);
        ReleaseResize();
        return hr;
    }

    if (api == APIType::D3D12)
        HandleDX12ResizeBegin();
    else if (api == APIType::D3D11)
        HandleDX11ResizeBegin();

    const D3D12ResizeDiagnostics diagnostics =
        api == APIType::D3D12 ? BeginD3D12ResizeDiagnostics(pSwapChain) : D3D12ResizeDiagnostics{};
    HRESULT hr = dxgi_shared_oResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pCreationNodeMask,
                                 ppPresentQueue);
    EndD3D12ResizeDiagnostics(diagnostics, pSwapChain, hr, "DetourResizeBuffers1", BufferCount, Width, Height,
                              NewFormat, SwapChainFlags);

    if (FAILED(hr)) {
        HookLog("DXGI: ResizeBuffers1 FAILED with 0x%08X", hr);
    } else {
        HookLog("DXGI: ResizeBuffers1 SUCCESS");
    }

    // Reset resize flags after resize completes
    if (api == APIType::D3D12)
        HandleDX12ResizeEnd();

    g_SharedState.swapchainInvalid.store(false);
    g_SharedState.wrapperResizeDepth.fetch_sub(1);
    ReleaseResize();
    return hr;
}
}
