#include "test-harness.h"

#include <windows.h>

#include "core/path-utils.h"

using fast_explorer::core::ensureDirectoryRecursive;
using fast_explorer::core::resolveAppDataSubdir;

namespace {

bool directoryExists(const wchar_t* path) {
  const DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

void removeIfExists(const wchar_t* path) {
  RemoveDirectoryW(path);
}

class EnvOverride {
 public:
  EnvOverride(const wchar_t* name, const wchar_t* value) : name_(name) {
    wchar_t prev[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(name, prev, _countof(prev));
    if (len > 0 && len < _countof(prev)) {
      previous_.assign(prev, len);
      hadPrevious_ = true;
    }
    SetEnvironmentVariableW(name, value);
  }
  ~EnvOverride() {
    SetEnvironmentVariableW(name_, hadPrevious_ ? previous_.c_str() : nullptr);
  }
  EnvOverride(const EnvOverride&) = delete;
  EnvOverride& operator=(const EnvOverride&) = delete;

 private:
  const wchar_t* name_;
  std::wstring previous_;
  bool hadPrevious_ = false;
};

}  // namespace

FE_TEST_CASE(resolveAppDataSubdir_default_returns_localappdata_path) {
  // Make sure we do NOT see a stale portable override.
  SetEnvironmentVariableW(L"FAST_EXPLORER_PORTABLE_ROOT", nullptr);
  std::wstring out;
  FE_ASSERT_TRUE(resolveAppDataSubdir(L"logs", out));
  FE_ASSERT_FALSE(out.empty());
  // Must end with FastExplorer\logs.
  const std::wstring suffix = L"\\FastExplorer\\logs";
  FE_ASSERT_TRUE(out.size() > suffix.size());
  FE_ASSERT_TRUE(out.compare(out.size() - suffix.size(), suffix.size(), suffix) == 0);
}

FE_TEST_CASE(resolveAppDataSubdir_portable_override_wins) {
  EnvOverride scope(L"FAST_EXPLORER_PORTABLE_ROOT", L"D:\\PortableRoot");
  std::wstring out;
  FE_ASSERT_TRUE(resolveAppDataSubdir(L"logs", out));
  FE_ASSERT_WSTREQ(out, L"D:\\PortableRoot\\logs");
}

FE_TEST_CASE(resolveAppDataSubdir_rejects_empty_sub) {
  std::wstring out;
  FE_ASSERT_FALSE(resolveAppDataSubdir(L"", out));
  FE_ASSERT_FALSE(resolveAppDataSubdir(nullptr, out));
}

FE_TEST_CASE(ensureDirectoryRecursive_creates_missing_path) {
  // Build a temp-relative path so the test does not depend on a hard-coded
  // drive layout.
  wchar_t tempDir[MAX_PATH];
  const DWORD tempLen = GetTempPathW(_countof(tempDir), tempDir);
  FE_ASSERT_TRUE(tempLen > 0 && tempLen < _countof(tempDir));

  std::wstring leaf = tempDir;
  leaf += L"FastExplorerTests\\nested\\path";

  // Clean up beforehand so the assertion does not pass spuriously.
  removeIfExists(leaf.c_str());
  removeIfExists((std::wstring(tempDir) + L"FastExplorerTests\\nested").c_str());
  removeIfExists((std::wstring(tempDir) + L"FastExplorerTests").c_str());

  FE_ASSERT_TRUE(ensureDirectoryRecursive(leaf.c_str()));
  FE_ASSERT_TRUE(directoryExists(leaf.c_str()));

  // Cleanup.
  removeIfExists(leaf.c_str());
  removeIfExists((std::wstring(tempDir) + L"FastExplorerTests\\nested").c_str());
  removeIfExists((std::wstring(tempDir) + L"FastExplorerTests").c_str());
}

FE_TEST_CASE(ensureDirectoryRecursive_idempotent_on_existing) {
  wchar_t tempDir[MAX_PATH];
  const DWORD tempLen = GetTempPathW(_countof(tempDir), tempDir);
  FE_ASSERT_TRUE(tempLen > 0 && tempLen < _countof(tempDir));

  // Temp directory always exists at this point.
  std::wstring existing = tempDir;
  // Strip trailing backslash from GetTempPath for the test comparison only;
  // CreateDirectoryW handles either form, and ensureDirectoryRecursive too.
  if (!existing.empty() && existing.back() == L'\\') {
    existing.pop_back();
  }
  FE_ASSERT_TRUE(ensureDirectoryRecursive(existing.c_str()));
}

FE_TEST_CASE(ensureDirectoryRecursive_rejects_null_and_empty) {
  FE_ASSERT_FALSE(ensureDirectoryRecursive(nullptr));
  FE_ASSERT_FALSE(ensureDirectoryRecursive(L""));
}
