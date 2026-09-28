#pragma once
#include "pacing_trace.h"

// Separate opt-in diagnostic mapping; does not change the capture texture ABI.
// Both peers must use this exact layout. A reader creates/initializes it before
// injection; the hook never creates it and retains its view until process exit.
namespace ce::present_observer {
constexpr uint64_t kMagic = 0x4345505245533031ULL;
struct Shared {
    uint64_t magic = kMagic;
    uint64_t bytes = sizeof(Shared);
    std::atomic<bool> active{true};
    pacing_trace::Ring<16384> events;
};
inline bool Accept(const Shared& shared) {
    return shared.magic == kMagic && shared.bytes == sizeof(Shared);
}
inline bool IsObservation(pacing_trace::Kind kind) {
    return kind == pacing_trace::Kind::PresentBegin || kind == pacing_trace::Kind::PresentForward ||
           kind == pacing_trace::Kind::PresentEnd || kind == pacing_trace::Kind::FinalOutput;
}
}  // namespace ce::present_observer
