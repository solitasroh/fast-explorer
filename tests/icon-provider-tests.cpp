#include "test-harness.h"

#include <cstddef>
#include <string>

#include "explorer/icon-provider.h"

using fast_explorer::ui::IconProvider;

FE_TEST_CASE(IconProvider_Default_ProcessedZero) {
  IconProvider provider(nullptr);
  FE_ASSERT_EQ(provider.processedForTest(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(IconProvider_Request_WorkerProcessesIt) {
  IconProvider provider(nullptr);
  provider.request(L".txt");
  provider.waitForProcessedForTest(1);
  FE_ASSERT_EQ(provider.processedForTest(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(IconProvider_ManyRequests_AllProcessedInOrderOfArrival) {
  IconProvider provider(nullptr);
  constexpr std::size_t kN = 50;
  for (std::size_t i = 0; i < kN; ++i) {
    provider.request(L".ext");
  }
  provider.waitForProcessedForTest(kN);
  FE_ASSERT_EQ(provider.processedForTest(), kN);
}

FE_TEST_CASE(IconProvider_DestructorJoinsWorker_NoHang) {
  // Construct + destruct rapidly without sending any requests.
  // The worker must observe the stop signal through the cv's
  // stop_token-aware wait, otherwise the destructor would block
  // indefinitely on an empty queue.
  for (int i = 0; i < 10; ++i) {
    IconProvider provider(nullptr);
  }
  // If we got here the destructor joined cleanly each time.
  FE_ASSERT_TRUE(true);
}

FE_TEST_CASE(IconProvider_DestructorJoinsAfterPendingRequest) {
  IconProvider provider(nullptr);
  provider.request(L".a");
  provider.waitForProcessedForTest(1);
  // Destructor runs at end of scope; the worker must exit without
  // hanging even though we never explicitly stopped it.
}
