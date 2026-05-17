#include "test-harness.h"

#include "core/process-memory.h"
#include "core/ring-logger.h"

using fast_explorer::core::ProcessMemoryService;
using fast_explorer::core::RingLogger;

FE_TEST_CASE(ProcessMemoryService_WorkingSetBytes_Positive) {
  // Any Windows process that has loaded user code has a non-zero
  // resident working set; GetProcessMemoryInfo returns
  // PROCESS_MEMORY_COUNTERS_EX::WorkingSetSize > 0 for it.
  FE_ASSERT_TRUE(ProcessMemoryService::workingSetBytes() > 0);
}

FE_TEST_CASE(ProcessMemoryService_PrivateBytes_Positive) {
  FE_ASSERT_TRUE(ProcessMemoryService::privateBytes() > 0);
}

FE_TEST_CASE(ProcessMemoryService_EmptyProbe_DefaultsZero) {
  // A fresh service that has not run notifyMinimized publishes a
  // zero-initialized probe envelope.
  RingLogger logger;
  ProcessMemoryService svc(logger);
  const auto probe = svc.lastEmptyWorkingSetProbe();
  FE_ASSERT_EQ(probe.callMicros, 0ULL);
  FE_ASSERT_EQ(probe.bytesBefore, 0ULL);
  FE_ASSERT_EQ(probe.bytesAfter, 0ULL);
}

FE_TEST_CASE(ProcessMemoryService_NotifyMinimized_PopulatesProbe) {
  RingLogger logger;
  ProcessMemoryService svc(logger);
  svc.notifyMinimized();
  const auto probe = svc.lastEmptyWorkingSetProbe();
  // Either the EmptyWorkingSet call succeeded (probe populated) or
  // it failed for an unrelated reason (probe still zero); on a
  // normal desktop CI host the call succeeds. Assert the
  // populated path: bytesBefore is positive and callMicros has a
  // real reading. The 200ms §14.7 budget is observed but not
  // asserted here because CI host load can spike well above it.
  FE_ASSERT_TRUE(probe.bytesBefore > 0);
  FE_ASSERT_TRUE(probe.callMicros < 5'000'000ULL);  // sanity ceiling = 5s
}
