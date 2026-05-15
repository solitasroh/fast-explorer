#include "test-harness.h"

#include <cstddef>

#include "ui/shell-worker.h"

using fast_explorer::ui::ShellCommand;
using fast_explorer::ui::ShellCommandKind;
using fast_explorer::ui::ShellWorker;

FE_TEST_CASE(ShellWorker_Default_ProcessedZero) {
  ShellWorker worker(nullptr);
  FE_ASSERT_EQ(worker.processedForTest(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(ShellWorker_Request_WorkerProcessesIt) {
  ShellWorker worker(nullptr);
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::Delete;
  cmd.sourcePath = L"C:\\nonexistent\\path";
  worker.request(std::move(cmd));
  worker.waitForProcessedForTest(1);
  FE_ASSERT_EQ(worker.processedForTest(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(ShellWorker_ManyRequests_AllProcessed) {
  ShellWorker worker(nullptr);
  constexpr std::size_t kN = 30;
  for (std::size_t i = 0; i < kN; ++i) {
    ShellCommand cmd;
    cmd.kind = ShellCommandKind::Rename;
    cmd.sourcePath = L"X:\\src";
    cmd.newName = L"dst";
    worker.request(std::move(cmd));
  }
  worker.waitForProcessedForTest(kN);
  FE_ASSERT_EQ(worker.processedForTest(), kN);
}

FE_TEST_CASE(ShellWorker_DestructorJoinsWithoutRequest) {
  // jthread + stop_token-aware cv must wake the worker even if no
  // command was ever queued; otherwise the destructor would hang.
  for (int i = 0; i < 10; ++i) {
    ShellWorker worker(nullptr);
  }
  FE_ASSERT_TRUE(true);
}

FE_TEST_CASE(ShellWorker_ShellCommandKind_DistinctValues) {
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::Rename),
               static_cast<int>(ShellCommandKind::CreateFolder));
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::Rename),
               static_cast<int>(ShellCommandKind::Delete));
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::CreateFolder),
               static_cast<int>(ShellCommandKind::Delete));
}
