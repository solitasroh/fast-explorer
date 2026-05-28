#include "test-harness.h"

#include <windows.h>

#include <cstddef>
#include <memory>

#include "explorer/messages.h"
#include "explorer/result-channel.h"

using fast_explorer::ui::kWmFeOperationResult;
using fast_explorer::ui::ResultChannel;

FE_TEST_CASE(ResultChannel_Default_IsEmpty) {
  ResultChannel<int> channel(nullptr, kWmFeOperationResult);
  FE_ASSERT_EQ(channel.pendingForTest(), static_cast<std::size_t>(0));
  FE_ASSERT_TRUE(channel.drainResults().empty());
}

FE_TEST_CASE(ResultChannel_Publish_AccumulatesUntilDrain) {
  ResultChannel<int> channel(nullptr, kWmFeOperationResult);
  channel.publish(1);
  channel.publish(2);
  channel.publish(3);
  FE_ASSERT_EQ(channel.pendingForTest(), static_cast<std::size_t>(3));
  auto drained = channel.drainResults();
  FE_ASSERT_EQ(drained.size(), static_cast<std::size_t>(3));
  FE_ASSERT_EQ(drained[0], 1);
  FE_ASSERT_EQ(drained[1], 2);
  FE_ASSERT_EQ(drained[2], 3);
  FE_ASSERT_EQ(channel.pendingForTest(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(ResultChannel_Drain_ResetsGateForNextPost) {
  // After draining, a new publish must be free to post again.
  // We cannot observe PostMessage directly with a null HWND, but
  // we can verify the channel keeps accepting publishes and
  // tracks the count correctly across drain boundaries.
  ResultChannel<int> channel(nullptr, kWmFeOperationResult);
  channel.publish(10);
  auto first = channel.drainResults();
  FE_ASSERT_EQ(first.size(), static_cast<std::size_t>(1));
  channel.publish(20);
  channel.publish(30);
  auto second = channel.drainResults();
  FE_ASSERT_EQ(second.size(), static_cast<std::size_t>(2));
  FE_ASSERT_EQ(second[0], 20);
  FE_ASSERT_EQ(second[1], 30);
}

FE_TEST_CASE(ResultChannel_MoveOnlyPayload_TransfersOwnership) {
  // Smoke that the channel works with a move-only T. We use a
  // simple unique_ptr<int> stand-in for IconProvider::IconResult.
  ResultChannel<std::unique_ptr<int>> channel(nullptr,
                                              kWmFeOperationResult);
  channel.publish(std::make_unique<int>(42));
  channel.publish(std::make_unique<int>(7));
  auto drained = channel.drainResults();
  FE_ASSERT_EQ(drained.size(), static_cast<std::size_t>(2));
  FE_ASSERT_EQ(*drained[0], 42);
  FE_ASSERT_EQ(*drained[1], 7);
}

FE_TEST_CASE(ResultChannel_NullHost_StillAccumulatesForDestructorDrain) {
  // With no host the gate must not stay latched true after a
  // failed post; otherwise the destructor drain would miss
  // anything published after the first publish until drainResults
  // explicitly clears the gate.
  ResultChannel<int> channel(nullptr, kWmFeOperationResult);
  channel.publish(1);
  channel.publish(2);
  channel.publish(3);
  FE_ASSERT_EQ(channel.pendingForTest(), static_cast<std::size_t>(3));
  auto drained = channel.drainResults();
  FE_ASSERT_EQ(drained.size(), static_cast<std::size_t>(3));
}
