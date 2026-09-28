#pragma once

struct IDXGISwapChain;

namespace ce::present_queue_trace {
// Diagnostic context only: a queue submission inside Present is evidence to
// inspect, not permission to adopt that queue. Nested presents restore their
// caller's context; other rendering threads cannot see this thread's chain.
inline thread_local IDXGISwapChain* currentSwapchain = nullptr;

class Scope {
    IDXGISwapChain* previous;
public:
    explicit Scope(IDXGISwapChain* swapchain) : previous(currentSwapchain) {
        currentSwapchain = swapchain;
    }
    ~Scope() { currentSwapchain = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}  // namespace ce::present_queue_trace
