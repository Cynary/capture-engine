#include <gtest/gtest.h>
#include "source_fragment_reader.h"

// The injected hook is not linked into unit_tests. These wiring guards cover
// the unsafe bootstrap order; native late-attachment tests exercise the GPU.
TEST(DX12PresentQueueRecovery, DefersBackbufferProcessingUntilQueueIsKnown) {
    const auto source = ce::test_source::ReadFile("hook/apis/dx12_hook_process_session.cpp");
    const auto guard = source.find("if (!dx12_hook_g_SwapchainQueue)");
    const auto session = source.find("FrameProcessSession");
    ASSERT_NE(guard, std::string::npos);
    ASSERT_NE(session, std::string::npos);
    ASSERT_LT(guard, session);
    EXPECT_NE(source.substr(guard, session - guard).find("return;"), std::string::npos);
    EXPECT_LT(source.find("lock(g_CommandQueueMutex)"), guard);
}

TEST(DX12PresentQueueRecovery, RequiresNativePresentSubmissionOnMatchingDevice) {
    const auto source = ce::test_source::ReadFile("hook/apis/dx12_hook_ecl.cpp");
    const auto publish = source.find("DX12_SetSwapchainQueue(queue, false, false, false, swapchain)");
    ASSERT_NE(publish, std::string::npos);
    const auto checks = source.substr(0, publish);
    EXPECT_NE(checks.find("ce::present_queue_trace::currentSwapchain"), std::string::npos);
    EXPECT_NE(checks.find("dx12_hook_s_insideCEOverlayECLDepth != 0"), std::string::npos);
    EXPECT_NE(checks.find("callerModule != GetModuleHandleA(\"dxgi.dll\")"), std::string::npos);
    EXPECT_NE(checks.find("D3D12_COMMAND_LIST_TYPE_DIRECT"), std::string::npos);
    EXPECT_NE(checks.find("queueDevice == swapchainDevice"), std::string::npos);
    EXPECT_NE(checks.find("if (!sameDevice)"), std::string::npos);
    // A competing creator can publish while GetDevice runs. Recheck under the
    // lock immediately before publishing; never replace an existing association.
    const auto finalLock = checks.rfind("lock(g_CommandQueueMutex)");
    ASSERT_NE(finalLock, std::string::npos);
    EXPECT_NE(checks.substr(finalLock).find("if (dx12_hook_g_SwapchainQueue)"), std::string::npos);
    EXPECT_NE(checks.substr(finalLock).find("return;"), std::string::npos);
}
