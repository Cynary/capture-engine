#include <gtest/gtest.h>
#include "../hook/common/streamline_runtime_policy.h"

TEST(StreamlineNgxDiscovery, ClassifiesHashedPluginsForPinningAndUnloadTracking) {
    const char* paths[] = {
        "C:/ProgramData/NVIDIA/NGX/models/sl_common_0/versions/134656/files/1B0_E658703.dll",
        "C:/ProgramData/NVIDIA/NGX/models/sl_dlss_g_0/versions/134656/files/1B0_E658703.dll",
        "C:/ProgramData/NVIDIA/NGX/models/sl_reflex_0/versions/134656/files/1B0_E658703.dll",
        "C:/ProgramData/NVIDIA/NGX/models/sl_pcl_0/versions/134656/files/1B0_E658703.dll",
    };
    for (const auto* path : paths) {
        EXPECT_TRUE(ce::streamline_runtime_policy::IsStreamlineModuleNameForFeatureHooking(path));
        EXPECT_TRUE(ce::streamline_runtime_policy::ShouldInspectStreamlineModuleOnLoad(path));
        EXPECT_TRUE(ce::streamline_runtime_policy::ShouldInvalidateStreamlineHooksOnModuleUnload(path));
    }
}

TEST(StreamlineNgxDiscovery, DoesNotClassifyEveryHashedOrNgxLibraryAsStreamline) {
    EXPECT_FALSE(ce::streamline_runtime_policy::IsStreamlineModuleNameForFeatureHooking("1B0_E658703.dll"));
    EXPECT_FALSE(ce::streamline_runtime_policy::IsStreamlineModuleNameForFeatureHooking(
        "C:/ProgramData/NVIDIA/NGX/models/nvngx_dlss_0/versions/134656/files/1B0_E658703.dll"));
    EXPECT_FALSE(ce::streamline_runtime_policy::IsStreamlineModuleNameForFeatureHooking(
        "C:/Game/sl_dlss_g_0/1B0_E658703.dll"));
}
