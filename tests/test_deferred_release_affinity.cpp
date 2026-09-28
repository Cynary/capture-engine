#include <gtest/gtest.h>
#include "../hook/common/deferred_release.h"
#include <thread>

namespace {
class ReleaseProbe final : public IUnknown {
public:
    std::atomic<unsigned> releases{0};
    std::atomic<DWORD> releasedOn{0};
    ce::DeferredReleaseQueue* reenter = nullptr;
    IUnknown* next = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        releasedOn.store(GetCurrentThreadId());
        releases.fetch_add(1);
        if (reenter)
            reenter->Queue(next);
        return 0;
    }
};
}

TEST(DeferredReleaseAffinity, WorkerCannotReleaseSingleThreadedDeviceObjects) {
    ce::DeferredReleaseQueue queue;
    ReleaseProbe affine, freeThreaded;
    const DWORD owner = GetCurrentThreadId();
    queue.Queue(&affine, owner);
    queue.Queue(&freeThreaded);
    std::thread worker([&] { queue.Process(); queue.ProcessThreadAffine(); });
    worker.join();
    EXPECT_EQ(affine.releases.load(), 0u);
    EXPECT_EQ(freeThreaded.releases.load(), 1u);
    queue.ProcessThreadAffine();
    EXPECT_EQ(affine.releases.load(), 1u);
    EXPECT_EQ(affine.releasedOn.load(), owner);
    queue.ProcessThreadAffine();
    EXPECT_EQ(affine.releases.load(), 1u);
}

TEST(DeferredReleaseAffinity, OwnerDrainLeavesWorkerObjectsForWorker) {
    ce::DeferredReleaseQueue queue;
    ReleaseProbe affine, freeThreaded;
    queue.Queue(nullptr, GetCurrentThreadId());
    queue.Queue(&freeThreaded);
    queue.Queue(&affine, GetCurrentThreadId());
    queue.ProcessThreadAffine();
    EXPECT_EQ(affine.releases.load(), 1u);
    EXPECT_EQ(freeThreaded.releases.load(), 0u);
    queue.Process();
    EXPECT_EQ(freeThreaded.releases.load(), 1u);
}

TEST(DeferredReleaseAffinity, ReleaseMayQueueAnotherObjectWithoutDeadlock) {
    ce::DeferredReleaseQueue queue;
    ReleaseProbe first, second;
    first.reenter = &queue;
    first.next = &second;
    queue.Queue(&first, GetCurrentThreadId());
    queue.ProcessThreadAffine();
    EXPECT_EQ(first.releases.load(), 1u);
    EXPECT_EQ(second.releases.load(), 0u);
    queue.Process();
    EXPECT_EQ(second.releases.load(), 1u);
}

TEST(DeferredReleaseAffinity, ProcessExitClearDoesNotCallIntoRetiredRuntime) {
    ce::DeferredReleaseQueue queue;
    ReleaseProbe object;
    queue.Queue(&object, GetCurrentThreadId());
    queue.Clear();
    queue.ProcessThreadAffine();
    queue.Process();
    EXPECT_EQ(object.releases.load(), 0u);
}
