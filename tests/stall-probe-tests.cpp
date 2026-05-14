#include "test-harness.h"
#include "ui/stall-probe.h"

using fast_explorer::ui::classifyStall;
using fast_explorer::ui::StallLevel;

FE_TEST_CASE(StallProbe_Zero_None) {
  FE_ASSERT_EQ(classifyStall(0), StallLevel::None);
}

FE_TEST_CASE(StallProbe_BelowInfo_None) {
  FE_ASSERT_EQ(classifyStall(49'999), StallLevel::None);
}

FE_TEST_CASE(StallProbe_AtInfoThreshold_Info) {
  FE_ASSERT_EQ(classifyStall(50'000), StallLevel::Info);
  FE_ASSERT_EQ(classifyStall(99'999), StallLevel::Info);
}

FE_TEST_CASE(StallProbe_AtWarnThreshold_Warn) {
  FE_ASSERT_EQ(classifyStall(100'000), StallLevel::Warn);
  FE_ASSERT_EQ(classifyStall(499'999), StallLevel::Warn);
}

FE_TEST_CASE(StallProbe_AtErrorThreshold_Error) {
  FE_ASSERT_EQ(classifyStall(500'000), StallLevel::Error);
  FE_ASSERT_EQ(classifyStall(5'000'000), StallLevel::Error);
}
