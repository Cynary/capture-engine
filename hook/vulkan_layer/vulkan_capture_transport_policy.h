#pragma once

// Which cached shared-texture entry may carry the Vulkan layer's capture
// transport to media, and what republishing it for a new host must do.
//
// A cached entry is one of two kinds:
//  - a layer-owned transport (IPC relay textures, or Vulkan-native NT
//    textures): the layer created the shared handles, so any media process
//    can open them;
//  - an import of media's encoder KMT textures (DXVK zero-copy adoption):
//    the textures belong to the media process that published them. Its
//    textureHandles stay null because media already owns the textures, and
//    it has no relay, so frames are signaled on the exported timeline fence.
//
// An import is the transport only while media's useEncoderTextures says so.
// Handing it to anything else - a replacement media host, or a rebuilt
// capture that is not adopting right now - published null texture handles
// and a relay fence nobody signals any more.

namespace ce::vulkan_capture_transport {

enum class HostRepublish {
    Published,  // the current transport was handed to the new host
    Rebuild,    // the transport cannot be republished; build a new one
    Retry,      // a lock was contended; try again on the next present
};

struct HostRepublishInput {
    bool locksHeld = false;
    bool stateCurrent = false;  // initialized capture state for this swapchain
    bool entryFound = false;    // valid cached entry matching that state
    bool entryIsEncoderTextureImport = false;
    bool entryHandlesComplete = false;  // every handle it would publish is non-null
};

inline HostRepublish DecideHostRepublish(const HostRepublishInput& input) {
    if (!input.locksHeld)
        return HostRepublish::Retry;
    if (!input.stateCurrent || !input.entryFound || input.entryIsEncoderTextureImport || !input.entryHandlesComplete) {
        return HostRepublish::Rebuild;
    }
    return HostRepublish::Published;
}

// A rebuild must not find the current state still "initialized": capture
// initialization treats an unchanged initialized state as current and returns
// before publishing anything.
inline bool RebuildInvalidatesCurrentState(const HostRepublishInput& input) {
    return input.locksHeld && input.stateCurrent && DecideHostRepublish(input) == HostRepublish::Rebuild;
}

// Whether a cached entry may serve a capture that is not adopting media's
// encoder textures. A valid import that is found here is stale and must be
// retired so the capture gets a layer-owned transport.
inline bool CanServeUnadoptedTransport(bool valid, bool encoderTextureImport) {
    return valid && !encoderTextureImport;
}

// Only the IPC relay signals the relay's fence; every other transport
// (including an adopted import) is signaled on the exported timeline fence.
inline bool PublishesRelayFence(bool entryHasIpcRelay, bool relayFenceAvailable) {
    return entryHasIpcRelay && relayFenceAvailable;
}

}  // namespace ce::vulkan_capture_transport
