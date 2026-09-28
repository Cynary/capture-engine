#include "dx12_hook_internal.h"
#include "../common/present_queue_trace.h"

// Recover only a missing association, from a native DXGI submission inside
// Present on this thread. An arbitrary game's/overlay's queue submission is not
// evidence that its queue owns the currently presented backbuffer.
void DX12_TryRecoverPresentQueue(ID3D12CommandQueue* queue, const void* caller) {
    auto* swapchain = ce::present_queue_trace::currentSwapchain;
    if (!swapchain || !queue || dx12_hook_s_insideCEOverlayECLDepth != 0)
        return;
    {
        std::lock_guard<std::recursive_mutex> lock(g_CommandQueueMutex);
        if (dx12_hook_g_SwapchainQueue)
            return;
    }
    HMODULE callerModule = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(caller), &callerModule) ||
        callerModule != GetModuleHandleA("dxgi.dll"))
        return;
    if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return;

    ID3D12Device* queueDevice = nullptr;
    ID3D12Device* swapchainDevice = nullptr;
    const HRESULT queueHr = queue->GetDevice(IID_PPV_ARGS(&queueDevice));
    const HRESULT swapchainHr = swapchain->GetDevice(IID_PPV_ARGS(&swapchainDevice));
    const bool sameDevice = SUCCEEDED(queueHr) && SUCCEEDED(swapchainHr) &&
                            queueDevice && queueDevice == swapchainDevice;
    if (queueDevice) queueDevice->Release();
    if (swapchainDevice) swapchainDevice->Release();
    if (!sameDevice)
        return;

    std::lock_guard<std::recursive_mutex> lock(g_CommandQueueMutex);
    if (dx12_hook_g_SwapchainQueue)
        return;
    DX12_SetSwapchainQueue(queue, false, false, false, swapchain);
    HookLogImportant("DX12: Recovered missing presentation queue from native DXGI submit "
                     "(swapchain=%p queue=%p caller=%p)", swapchain, queue, caller);
}
