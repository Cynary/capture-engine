#include <gtest/gtest.h>
#include "../hook/wrappers/inline_hook_policy.h"

TEST(X86DeepPrologue, RestorableWindowsFrameStopsBeforeAlignmentAndAllocation) {
    const unsigned char resize[] = {0x8b, 0xff, 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec};
    EXPECT_EQ(ce::inline_hook_policy::X86DeepHookFramePrologueLength(resize, sizeof(resize)), 5);
    const unsigned char alternate[] = {0x8b, 0xff, 0x55, 0x89, 0xe5};
    EXPECT_EQ(ce::inline_hook_policy::X86DeepHookFramePrologueLength(alternate, sizeof(alternate)), 5);
}

TEST(X86DeepPrologue, RefusesUnknownAndTruncatedFrames) {
    const unsigned char bytes[] = {0x8b, 0xff, 0x55, 0x8b, 0xec};
    for (int size = 0; size < 5; ++size)
        EXPECT_EQ(ce::inline_hook_policy::X86DeepHookFramePrologueLength(bytes, size), 0);
    EXPECT_EQ(ce::inline_hook_policy::X86DeepHookFramePrologueLength(nullptr, 64), 0);
    const unsigned char wrongRegister[] = {0x8b, 0xff, 0x53, 0x8b, 0xec};
    EXPECT_EQ(ce::inline_hook_policy::X86DeepHookFramePrologueLength(wrongRegister, sizeof(wrongRegister)), 0);
    const unsigned char notFrame[] = {0x8b, 0xff, 0x55, 0x8b, 0xe4};
    EXPECT_EQ(ce::inline_hook_policy::X86DeepHookFramePrologueLength(notFrame, sizeof(notFrame)), 0);
}
