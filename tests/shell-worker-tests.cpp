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
  if (attr != INVALID_FILE_ATTRIBUTES) {
    return true;
  }
  // Distinguish "definitely missing" from "could not stat" (e.g. ACL
  // denied) so the test only treats genuine absence as a delete win.
  const DWORD err = GetLastError();
  return err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND;
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

FE_TEST_CASE(ShellWorker_Rename_RenamesFileOnDisk) {
  TempDir tmp(L"shellworker-rename");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring source = tmp.path() + L"\\before.txt";
  const std::wstring renamed = tmp.path() + L"\\after.txt";
  writeEmptyFile(source);
  FE_ASSERT_TRUE(fileExists(source));

  ShellWorker worker(nullptr);
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::Rename;
  cmd.sourcePath = source;
  cmd.newName = L"after.txt";
  worker.request(std::move(cmd));
  worker.waitForProcessedForTest(1);

  FE_ASSERT_FALSE(fileExists(source));
  FE_ASSERT_TRUE(fileExists(renamed));
}

FE_TEST_CASE(ShellWorker_CreateFolder_AddsFolderUnderParent) {
  TempDir tmp(L"shellworker-create");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring childPath = tmp.path() + L"\\newfolder";

  ShellWorker worker(nullptr);
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::CreateFolder;
  cmd.sourcePath = tmp.path();
  cmd.newName = L"newfolder";
  worker.request(std::move(cmd));
  worker.waitForProcessedForTest(1);

  FE_ASSERT_TRUE(fileExists(childPath));
  const DWORD attr = GetFileAttributesW(childPath.c_str());
  FE_ASSERT_TRUE((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

FE_TEST_CASE(ShellWorker_Rename_EmptyName_DoesNotChangeFile) {
  // Guard against renaming to an empty name; the helper should
  // refuse without touching the file system.
  TempDir tmp(L"shellworker-rename-empty");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);
  const std::wstring source = tmp.path() + L"\\original.txt";
  writeEmptyFile(source);

  ShellWorker worker(nullptr);
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::Rename;
  cmd.sourcePath = source;
  cmd.newName = L"";
  worker.request(std::move(cmd));
  worker.waitForProcessedForTest(1);

  FE_ASSERT_TRUE(fileExists(source));
}

FE_TEST_CASE(ShellWorker_ShellCommandKind_DistinctValues) {
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::Rename),
               static_cast<int>(ShellCommandKind::CreateFolder));
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::Rename),
               static_cast<int>(ShellCommandKind::Delete));
  FE_ASSERT_NE(static_cast<int>(ShellCommandKind::CreateFolder),
               static_cast<int>(ShellCommandKind::Delete));
}
