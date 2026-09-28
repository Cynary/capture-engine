#pragma once

#include <dxgi1_6.h>

namespace ce::presentation_color {

// Private Windows DXGI inspection interface, also used by ReShade:
// https://github.com/crosire/reshade/blob/main/source/dxgi/dxgi_swapchain.hpp
// QueryInterface is mandatory: this interface is not part of the public SDK
// and may be absent on other DXGI implementations. No offsets into an object
// or version-specific DLL addresses are used.
inline constexpr GUID kSwapChainInspectionId = {
    0x8c803e30, 0x9e41, 0x4ddf, {0xb2, 0x06, 0x46, 0xf2, 0x8e, 0x90, 0xe4, 0x05}};

struct SwapChainColorInspection : IUnknown {
    virtual bool STDMETHODCALLTYPE HasProxyFrontBufferSurface() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatisticsTest(void*) = 0;
    virtual void STDMETHODCALLTYPE EmulateXBOXBehavior(BOOL) = 0;
    virtual DXGI_COLOR_SPACE_TYPE STDMETHODCALLTYPE GetColorSpace1() = 0;
};

inline bool TryQueryCurrentSwapChainColorSpace(IUnknown* swapChain, DXGI_COLOR_SPACE_TYPE& result) {
    if (!swapChain)
        return false;
    SwapChainColorInspection* inspection = nullptr;
    if (FAILED(swapChain->QueryInterface(kSwapChainInspectionId, reinterpret_cast<void**>(&inspection))) ||
        !inspection)
        return false;
    const auto current = inspection->GetColorSpace1();
    inspection->Release();
    if (current < DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ||
        current > DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 || current == DXGI_COLOR_SPACE_RESERVED)
        return false;
    result = current;
    return true;
}

}  // namespace ce::presentation_color
