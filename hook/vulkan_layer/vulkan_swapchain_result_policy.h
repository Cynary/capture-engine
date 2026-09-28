#pragma once

#include <cstdint>

// Which vkQueuePresentKHR / vkAcquireNextImage*KHR results tell the
// application that its swapchain no longer matches the surface and has to be
// recreated (or is gone). The layer passes every result through unchanged; it
// only names these in the log, because a swapchain the game destroys and
// recreates over and over is otherwise indistinguishable from a game that
// simply changed its settings.
//
// DOOM Eternal session `20260928_042343` is that blind spot: dropping from
// native 4K to a 1440p mode made the game recreate its swapchain eighteen
// times in eleven seconds, alternating 2560x1440 and 3840x2160, while the
// window stayed black, and the log could not say whether present, acquire or
// the application itself asked for each recreation.
//
// The codes are mirrored as plain integers so the policy stays testable
// without the Vulkan headers; the call site pins them to the real enumerants.

namespace ce::vulkan_swapchain_result_policy {

inline constexpr int32_t kSuboptimal = 1000001003;                     // VK_SUBOPTIMAL_KHR
inline constexpr int32_t kOutOfDate = -1000001004;                     // VK_ERROR_OUT_OF_DATE_KHR
inline constexpr int32_t kSurfaceLost = -1000000000;                   // VK_ERROR_SURFACE_LOST_KHR
inline constexpr int32_t kFullScreenExclusiveModeLost = -1000255000;  // VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
inline constexpr int32_t kDeviceLost = -4;                             // VK_ERROR_DEVICE_LOST

// The name of a result that invalidates the swapchain, or nullptr for any
// other result (success, timeouts, not-ready and unrelated errors).
inline const char* InvalidationResultName(int32_t result) {
    switch (result) {
        case kSuboptimal:
            return "VK_SUBOPTIMAL_KHR";
        case kOutOfDate:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case kSurfaceLost:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case kFullScreenExclusiveModeLost:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case kDeviceLost:
            return "VK_ERROR_DEVICE_LOST";
        default:
            return nullptr;
    }
}

}  // namespace ce::vulkan_swapchain_result_policy
