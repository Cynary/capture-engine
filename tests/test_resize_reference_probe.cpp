#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../hook/common/resize_reference_probe.h"

namespace probe = ce::resize_reference_probe;

namespace {

// A back buffer whose reference count the probe must leave exactly as it found
// it: GetBuffer adds one, the probe's Release takes it back.
struct FakeBuffer {
    ULONG refs = 1;
    ULONG AddRef() { return ++refs; }
    ULONG Release() { return --refs; }
};

struct FakeSwapChain {
    std::vector<FakeBuffer> buffers;
    bool refuseInterface = false;
    HRESULT GetBuffer(UINT index, REFIID, void** out) {
        if (refuseInterface || index >= buffers.size()) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        buffers[index].AddRef();
        *out = &buffers[index];
        return S_OK;
    }
};

std::string Formatted(const probe::BackBufferReferences& references) {
    char text[128] = {};
    probe::Format(references, text, sizeof(text));
    return text;
}

}  // namespace

TEST(ResizeReferenceProbeTest, ReportsWhatOthersHoldAndLeavesCountsUnchanged) {
    FakeSwapChain swapChain;
    swapChain.buffers = {FakeBuffer{1}, FakeBuffer{3}, FakeBuffer{1}};
    const auto references = probe::Probe<FakeBuffer>(&swapChain, 3, IID_IUnknown);
    ASSERT_EQ(references.probed, 3u);
    EXPECT_EQ(Formatted(references), "[1,3,1]");
    // An extra reference (the holder that makes DXGI refuse a resize) stands out.
    EXPECT_EQ(swapChain.buffers[1].refs, 3u);
    EXPECT_EQ(swapChain.buffers[0].refs, 1u);
}

TEST(ResizeReferenceProbeTest, AChainOfAnotherApiProbesEmpty) {
    FakeSwapChain swapChain;
    swapChain.buffers = {FakeBuffer{}, FakeBuffer{}};
    swapChain.refuseInterface = true;
    const auto references = probe::Probe<FakeBuffer>(&swapChain, 2, IID_IUnknown);
    EXPECT_EQ(references.probed, 0u);
    EXPECT_EQ(Formatted(references), "[]");
}

TEST(ResizeReferenceProbeTest, StopsAtTheFirstUnavailableBufferAndAtTheCap) {
    FakeSwapChain shortChain;
    shortChain.buffers = {FakeBuffer{}, FakeBuffer{}};
    EXPECT_EQ(probe::Probe<FakeBuffer>(&shortChain, 5, IID_IUnknown).probed, 2u);

    FakeSwapChain longChain;
    longChain.buffers.resize(probe::kMaxProbedBuffers + 4);
    EXPECT_EQ(probe::Probe<FakeBuffer>(&longChain, probe::kMaxProbedBuffers + 4, IID_IUnknown).probed,
              probe::kMaxProbedBuffers);
    EXPECT_EQ(probe::Probe<FakeBuffer>(static_cast<FakeSwapChain*>(nullptr), 3, IID_IUnknown).probed, 0u);
}

TEST(ResizeReferenceProbeTest, FormatNeverOverrunsASmallBuffer) {
    probe::BackBufferReferences references;
    references.probed = probe::kMaxProbedBuffers;
    for (UINT i = 0; i < references.probed; ++i)
        references.heldByOthers[i] = 1234567;
    char text[12];
    probe::Format(references, text, sizeof(text));
    EXPECT_LT(std::string(text).size(), sizeof(text));
}
