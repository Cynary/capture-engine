#include <gtest/gtest.h>
#include "../hook/common/streamline_runtime_policy.h"

namespace {
using ce::streamline_runtime_policy::EvaluateViewportRuntimeUpdateFromGetState;

TEST(StreamlineLateRuntimeTest, RecoversMissedActivationFromCompletedInterpolation) {
    const auto result = EvaluateViewportRuntimeUpdateFromGetState(
        true, false, false, true, false, 0, 0, 5, 2, 0, 120);
    EXPECT_TRUE(result.update.shouldUpdate);
    EXPECT_TRUE(result.update.active);
    EXPECT_EQ(result.update.multiplier, 2);
    EXPECT_EQ(result.update.generatedFrames, 1u);
}

TEST(StreamlineLateRuntimeTest, RequiresPositiveRuntimeEvidence) {
    for (uint32_t presented : {0u, 1u, 7u}) {
        EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
            true, false, false, true, false, 0, 0, 5, presented, 0, 120).update.shouldUpdate);
    }
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        true, false, false, true, false, 0, 0, 5, 2, 0, 0).update.shouldUpdate);
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        true, false, false, false, false, 0, 0, 5, 2, 0, 120).update.shouldUpdate);
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        true, false, false, true, false, 0, 0, 5, 2, 1, 120).update.shouldUpdate);
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        false, false, false, true, false, 0, 0, 5, 2, 0, 120).update.shouldUpdate);
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        true, false, false, true, false, 0, 0, 0, 2, 0, 120).update.shouldUpdate);
}

TEST(StreamlineLateRuntimeTest, PreservesExplicitOffAndProtectedTransitions) {
    const auto off = EvaluateViewportRuntimeUpdateFromGetState(
        true, true, false, true, false, 0, 0, 5, 2, 0, 120);
    EXPECT_TRUE(off.update.shouldUpdate);
    EXPECT_FALSE(off.update.active);
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        true, false, false, true, true, 0, 0, 5, 2, 0, 120).update.shouldUpdate);
    EXPECT_FALSE(EvaluateViewportRuntimeUpdateFromGetState(
        true, false, true, true, false, 0, 0, 5, 1, 0, 120).update.shouldUpdate);
}
}  // namespace
