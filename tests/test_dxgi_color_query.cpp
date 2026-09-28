#include <gtest/gtest.h>
#include "../hook/common/dxgi_swapchain_color_query.h"

namespace {
class ColorInspection final : public ce::presentation_color::SwapChainColorInspection {
public:
    bool supported = true;
    ULONG references = 1;
    unsigned queries = 0;
    DXGI_COLOR_SPACE_TYPE color = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        ++queries;
        *object = nullptr;
        if (!supported || !IsEqualGUID(iid, ce::presentation_color::kSwapChainInspectionId))
            return E_NOINTERFACE;
        *object = static_cast<ce::presentation_color::SwapChainColorInspection*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { return --references; }
    bool STDMETHODCALLTYPE HasProxyFrontBufferSurface() override { return false; }
    HRESULT STDMETHODCALLTYPE GetFrameStatisticsTest(void*) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE EmulateXBOXBehavior(BOOL) override {}
    DXGI_COLOR_SPACE_TYPE STDMETHODCALLTYPE GetColorSpace1() override { return color; }
};

TEST(DXGIColorQueryTest, ReadsCurrentContractAndReleasesInterface) {
    ColorInspection object;
    for (auto color : {DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
                       DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
                       DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709}) {
        object.color = color;
        auto result = DXGI_COLOR_SPACE_CUSTOM;
        EXPECT_TRUE(ce::presentation_color::TryQueryCurrentSwapChainColorSpace(&object, result));
        EXPECT_EQ(result, color);
        EXPECT_EQ(object.references, 1u);
    }
    EXPECT_EQ(object.queries, 3u);
}

TEST(DXGIColorQueryTest, UnsupportedAndNullLeaveOutputUnchanged) {
    ColorInspection object;
    object.supported = false;
    auto result = DXGI_COLOR_SPACE_CUSTOM;
    EXPECT_FALSE(ce::presentation_color::TryQueryCurrentSwapChainColorSpace(&object, result));
    EXPECT_FALSE(ce::presentation_color::TryQueryCurrentSwapChainColorSpace(nullptr, result));
    EXPECT_EQ(result, DXGI_COLOR_SPACE_CUSTOM);
    EXPECT_EQ(object.references, 1u);
}

TEST(DXGIColorQueryTest, InvalidContractIsRejectedWithoutLeakingInterface) {
    ColorInspection object;
    for (auto invalid : {DXGI_COLOR_SPACE_RESERVED, DXGI_COLOR_SPACE_CUSTOM,
                        static_cast<DXGI_COLOR_SPACE_TYPE>(25)}) {
        object.color = invalid;
        auto result = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        EXPECT_FALSE(ce::presentation_color::TryQueryCurrentSwapChainColorSpace(&object, result));
        EXPECT_EQ(result, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        EXPECT_EQ(object.references, 1u);
    }
}
}  // namespace
