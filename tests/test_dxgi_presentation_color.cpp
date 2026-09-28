#include "../hook/common/overlay_compat.h"
#include "test_dxgi_shared_shared.h"

TEST(DXGISharedTest, HDRDetectionRecognizesHDRAndSDRTenBitColorSpaces) {
    EXPECT_TRUE(ce::dx12_overlay_policy::IsHDRColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
    EXPECT_TRUE(ce::dx12_overlay_policy::IsHDRColorSpace(DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020));
    EXPECT_FALSE(ce::dx12_overlay_policy::IsHDRColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
    EXPECT_FALSE(ce::dx12_overlay_policy::IsHDRColorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
}

TEST(DXGISharedTest, HDRDetectionResolvesActualOverlayTargetStateFromFormatAndColorSpace) {
    EXPECT_FALSE(
        ce::dx12_overlay_policy::ResolveActualHDRStateForOverlayTarget(DXGI_FORMAT_R16G16B16A16_FLOAT, false, -1));
    EXPECT_TRUE(ce::dx12_overlay_policy::ResolveActualHDRStateForOverlayTarget(
        DXGI_FORMAT_R16G16B16A16_FLOAT, true, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
    EXPECT_FALSE(
        ce::dx12_overlay_policy::ResolveActualHDRStateForOverlayTarget(DXGI_FORMAT_R10G10B10A2_UNORM, false, -1));
    EXPECT_TRUE(ce::dx12_overlay_policy::ResolveActualHDRStateForOverlayTarget(
        DXGI_FORMAT_R10G10B10A2_UNORM, true, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
    EXPECT_FALSE(ce::dx12_overlay_policy::ResolveActualHDRStateForOverlayTarget(
        DXGI_FORMAT_R10G10B10A2_UNORM, true, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
    EXPECT_FALSE(ce::dx12_overlay_policy::ResolveActualHDRStateForOverlayTarget(
        DXGI_FORMAT_R8G8B8A8_UNORM, true, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
}

TEST(DXGISharedTest, RuntimeOwnedCallbackHDRFallbackUsesCachedKnownState) {
    EXPECT_FALSE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R16G16B16A16_FLOAT, false, true));
    EXPECT_TRUE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R16G16B16A16_FLOAT, true, true));
    EXPECT_FALSE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                                             true, true));

    EXPECT_TRUE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R10G10B10A2_UNORM, true, true));
    EXPECT_TRUE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R10G10B10A2_TYPELESS, true, true));
    EXPECT_TRUE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R16G16B16A16_TYPELESS, true, true));
    EXPECT_FALSE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R10G10B10A2_UNORM, true, false));
    EXPECT_FALSE(ce::dx12_overlay_policy::ResolveRuntimeOwnedCallbackHDRStateFromCachedState(
        DXGI_FORMAT_R10G10B10A2_UNORM, false, true));
}

TEST(DXGISharedTest, PresentationEncodingUsesColorSpaceRatherThanStorageFormat) {
    using ce::presentation_color::Encoding;
    EXPECT_EQ(Encoding::Sdr709,
              ce::presentation_color::ResolveDXGI(DXGI_FORMAT_R10G10B10A2_UNORM, false,
                                                  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
    EXPECT_EQ(Encoding::Sdr709,
              ce::presentation_color::ResolveDXGI(DXGI_FORMAT_R10G10B10A2_UNORM, true,
                                                  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
    EXPECT_EQ(Encoding::Hdr10Pq,
              ce::presentation_color::ResolveDXGI(DXGI_FORMAT_R10G10B10A2_UNORM, true,
                                                  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));
    EXPECT_EQ(Encoding::LinearScRgb,
              ce::presentation_color::ResolveDXGI(DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                                                  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709));
    EXPECT_EQ(Encoding::Unsupported,
              ce::presentation_color::ResolveDXGI(DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                                                  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
}

TEST(DXGISharedTest, WrappedColorSpaceForwardOwnsExactlyOncePublication) {
    EXPECT_TRUE(ce::presentation_color::ShouldRecordDetouredColorSpaceChange(0));
    EXPECT_FALSE(ce::presentation_color::ShouldRecordDetouredColorSpaceChange(1));
    EXPECT_FALSE(ce::presentation_color::ShouldRecordDetouredColorSpaceChange(2));
}

TEST(DXGISharedTest, ResidentColorTrackingSurvivesConsumerDisconnect) {
    const auto source = ce::test_source::ReadLogicalSource(
        std::filesystem::current_path() / "hook" / "common" / "dxgi_shared_hooks.cpp");
    const auto begin = source.find("HRESULT STDMETHODCALLTYPE DetourSetColorSpace1(");
    const auto end = source.find("HRESULT SetSwapChainColorSpaceFromWrapper(", begin);
    ASSERT_NE(begin, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    const auto detour = source.substr(begin, end - begin);
    // The production detour cannot be linked into the unit executable. Guard
    // its lifecycle boundary here; live disconnect/toggle/reconnect exercises
    // the COM private-data storage and the native hook together.
    EXPECT_EQ(detour.find("HookIsShuttingDown()"), std::string::npos);
    EXPECT_NE(detour.find("SUCCEEDED(result)"), std::string::npos);
    EXPECT_NE(detour.find("ShouldRecordDetouredColorSpaceChange"), std::string::npos);
    EXPECT_NE(detour.find("RecordSwapChainColorSpace"), std::string::npos);
}

TEST(DXGISharedTest, SwapchainColorSpaceTrackingNeverPatchesSharedVtableSlot) {
    const auto readSource = [](const std::filesystem::path& path) {
        return ce::test_source::ReadLogicalSource(path);
    };
    const std::string shared =
        readSource(std::filesystem::current_path() / "hook" / "common" / "dxgi_shared.cpp");
    const std::string wrapper =
        readSource(std::filesystem::current_path() / "hook" / "wrappers" / "dxgi_swapchain_wrap.cpp");
    ASSERT_FALSE(shared.empty());
    ASSERT_FALSE(wrapper.empty());
    EXPECT_NE(shared.find("DetourSetColorSpace1"), std::string::npos);
    EXPECT_NE(shared.find("InlineHook::InstallPublished(colorSpaceAddress"), std::string::npos);
    EXPECT_NE(shared.find("IsWrappedSwapChainObject(pSwapChain)"), std::string::npos);
    EXPECT_NE(shared.find("SetSwapChainColorSpaceFromWrapper"), std::string::npos);
    EXPECT_NE(wrapper.find("DXGIShared::SetSwapChainColorSpaceFromWrapper(m_pReal3, m_pReal, ColorSpace)"),
              std::string::npos);
    EXPECT_EQ(shared.find("vtable[38] = (void*)DetourSetColorSpace1"), std::string::npos);
    EXPECT_EQ(shared.find("oSetColorSpace1 ="), std::string::npos);
}

