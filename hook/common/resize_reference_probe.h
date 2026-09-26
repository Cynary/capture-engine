#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdio>

// Diagnostics for a failing IDXGISwapChain::ResizeBuffers. DXGI rejects a
// resize with DXGI_ERROR_INVALID_CALL while anyone still references a back
// buffer, and the rejection names no holder. Talos Reawakened died of exactly
// that while CE was recording (logs/20260926_083506) and nothing on record
// said who held what. Probing each buffer right before and after the call
// turns the next occurrence into evidence.
//
// The probe takes a reference with GetBuffer and releases it: the count
// Release returns is what everybody else still holds, DXGI's own references
// included. That baseline is why successful resizes are logged too.

namespace ce::resize_reference_probe {

inline constexpr UINT kMaxProbedBuffers = 16;

struct BackBufferReferences {
    UINT probed = 0;
    ULONG heldByOthers[kMaxProbedBuffers] = {};
};

// `bufferIid` selects the API: a D3D12 chain refuses an ID3D11 interface and the
// other way round, so a probe for one API is empty on the other's chains.
template <typename BufferType, typename SwapChain>
BackBufferReferences Probe(SwapChain* swapChain, UINT bufferCount, REFIID bufferIid) {
    BackBufferReferences references;
    if (!swapChain) {
        return references;
    }
    const UINT count = bufferCount < kMaxProbedBuffers ? bufferCount : kMaxProbedBuffers;
    for (UINT i = 0; i < count; ++i) {
        BufferType* buffer = nullptr;
        if (FAILED(swapChain->GetBuffer(i, bufferIid, reinterpret_cast<void**>(&buffer))) || !buffer) {
            break;
        }
        references.heldByOthers[i] = buffer->Release();
        ++references.probed;
    }
    return references;
}

// "[2,2,3]" - one entry per probed buffer; "[]" when nothing could be probed.
inline void Format(const BackBufferReferences& references, char* out, size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }
    size_t used = 0;
    auto append = [&](const char* text) {
        const int written = snprintf(out + used, outSize - used, "%s", text);
        if (written > 0) {
            used += static_cast<size_t>(written) < outSize - used ? static_cast<size_t>(written) : outSize - used - 1;
        }
    };
    out[0] = '\0';
    append("[");
    for (UINT i = 0; i < references.probed && i < kMaxProbedBuffers; ++i) {
        char entry[16];
        snprintf(entry, sizeof(entry), i == 0 ? "%lu" : ",%lu", static_cast<unsigned long>(references.heldByOthers[i]));
        append(entry);
    }
    append("]");
}

}  // namespace ce::resize_reference_probe
