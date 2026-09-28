#include <gtest/gtest.h>
#include "../hook/common/present_observer.h"
TEST(PresentObserver, RejectsIncompatibleLayout) {
    auto shared = std::make_unique<ce::present_observer::Shared>();
    EXPECT_TRUE(ce::present_observer::Accept(*shared));
    --shared->bytes;
    EXPECT_FALSE(ce::present_observer::Accept(*shared));
    ++shared->bytes; shared->magic = 0;
    EXPECT_FALSE(ce::present_observer::Accept(*shared));
}
TEST(PresentObserver, IncludesBoundariesWithoutRecordingGpuWork) {
    using ce::pacing_trace::Kind;
    for (auto kind : {Kind::PresentBegin, Kind::PresentForward, Kind::PresentEnd, Kind::FinalOutput})
        EXPECT_TRUE(ce::present_observer::IsObservation(kind));
    for (auto kind : {Kind::Submit, Kind::Fence, Kind::Frame, Kind::GpuSpan})
        EXPECT_FALSE(ce::present_observer::IsObservation(kind));
}
