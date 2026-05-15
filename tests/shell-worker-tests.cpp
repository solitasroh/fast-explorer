#include "test-harness.h"

#include <windows.h>

#include <cstddef>
#include <string>

#include "bench-fs-helper.h"
#include "ui/shell-worker.h"

using fast_explorer::tests::TempDir;
using fast_explorer::ui::ShellCommand;
using fast_explorer::ui::ShellCommandKind;
using fast_explorer::ui::ShellWorker;

namespace {

bool fileExists(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

void writeEmptyFile(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
}

}  // namespace

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

FE_TEST_CASE(ShellWorker_Delete_RemovesFileFromDisk) {
  TempDir tmp(L"shellworker-delete");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring target = tmp.path() + L"\\victim.txt";
  writeEmptyFile(target);
  FE_ASSERT_TRUE(fileExists(target));

  ShellWorker worker(nullptr);
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::Delete;
  cmd.sourcePath = target;
  worker.request(std::move(cmd));
  worker.waitForProcessedForTest(1);

  // The recycle-bin delete is asynchronous from the file system's
  // point of view but the worker only counts the command processed
  // after PerformOperations returns, so the file must be gone now.
  FE_ASSERT_FALSE(fileExists(target));
}

FE_TEST_CASE(ShellWorker_Delete_NonexistentPath_DoesNotHangWorker) {
  // PerformOperations will fail internally — the worker must still
  // advance processed_ so the next request is unblocked.
  ShellWorker worker(nullptr);
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::Delete;
  cmd.sourcePath = L"C:\\definitely\\does\\not\\exist\\victim.txt";
  worker.request(std::move(cmd));
  worker.waitForProcessedForTest(1);
  FE_ASSERT_EQ(worker.processedForTest(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(ShellWorker_ShellCommandKind_DistinctValues) {
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::Rename),
               static_cast<int>(ShellCommandKind::CreateFolder));
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::Rename),
               static_cast<int>(ShellCommandKind::Delete));
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::CreateFolder),
               static_cast<int>(ShellCommandKind::Delete));
}
