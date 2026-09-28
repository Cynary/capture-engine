#pragma once
#include <d3d11.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace ce {

// Transfer one COM reference for later release. Free-threaded objects can retire
// on the worker; objects from a single-threaded device must return to their owner.
class DeferredReleaseQueue {
    struct Entry {
        IUnknown* object;
        DWORD ownerThread;
    };
    std::vector<Entry> queue;
    std::mutex mutex;
    std::atomic<bool> hasThreadAffine{false};

    void ProcessOwnedBy(DWORD ownerThread) {
        std::vector<IUnknown*> toRelease;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto remaining = queue.begin();
            bool affineRemaining = false;
            for (const auto entry : queue) {
                if (entry.ownerThread == ownerThread) {
                    toRelease.push_back(entry.object);
                } else {
                    *remaining++ = entry;
                    affineRemaining |= entry.ownerThread != 0;
                }
            }
            queue.erase(remaining, queue.end());
            hasThreadAffine.store(affineRemaining, std::memory_order_release);
        }
        // Release can re-enter capture code; never hold the queue lock here.
        for (auto* object : toRelease)
            object->Release();
    }

public:
    void Queue(IUnknown* object, DWORD ownerThread = 0) {
        if (!object)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back({object, ownerThread});
        if (ownerThread)
            hasThreadAffine.store(true, std::memory_order_release);
    }

    void Process() {
        ProcessOwnedBy(0);
    }

    // Called on the present/capture thread, also while the hook is dormant.
    // The common case needs only an atomic read, with no allocation or lock.
    void ProcessThreadAffine() {
        if (hasThreadAffine.load(std::memory_order_acquire))
            ProcessOwnedBy(GetCurrentThreadId());
    }

    // Process exit only: the runtime may already be gone.
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        queue.clear();
        hasThreadAffine.store(false, std::memory_order_release);
    }
};

}  // namespace ce
