#include "test-harness.h"

#include <windows.h>

#include "core/path-utils.h"

using fast_explorer::core::ensureDirectoryRecursive;
using fast_explorer::core::isUncPath;
using fast_explorer::core::PathConvertError;
using fast_explorer::core::resolveAppDataSubdir;
using fast_explorer::core::toDisplay;
using fast_explorer::core::toInternal;

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
    // Two-pass query so we capture environment values up to the Win32 limit
    // (32767 wchars) instead of silently dropping anything longer than
    // MAX_PATH.
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed > 0) {
      previous_.resize(needed - 1);
      const DWORD copied = GetEnvironmentVariableW(
          name, previous_.data(), static_cast<DWORD>(previous_.size() + 1));
      if (copied > 0 && copied <= previous_.size()) {
        previous_.resize(copied);
        hadPrevious_ = true;
      } else {
        previous_.clear();
      }
    }
    SetEnvironmentVariableW(name, value);
  }
  ~EnvOverride() noexcept {
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

// ---------------- toInternal / toDisplay / isUncPath ----------------

FE_TEST_CASE(toInternal_adds_prefix_to_drive_path) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"C:\\Users\\me", out), PathConvertError::None);
  FE_ASSERT_WSTREQ(out, L"\\\\?\\C:\\Users\\me");
}

FE_TEST_CASE(toInternal_converts_forward_slashes) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"C:/Users/me/Downloads", out), PathConvertError::None);
  FE_ASSERT_WSTREQ(out, L"\\\\?\\C:\\Users\\me\\Downloads");
}

FE_TEST_CASE(toInternal_idempotent_on_already_prefixed) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"\\\\?\\C:\\Users\\me", out), PathConvertError::None);
  FE_ASSERT_WSTREQ(out, L"\\\\?\\C:\\Users\\me");
}

FE_TEST_CASE(toInternal_rejects_unc) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"\\\\server\\share", out), PathConvertError::UncUnsupported);
  FE_ASSERT_EQ(toInternal(L"\\\\?\\UNC\\server\\share", out), PathConvertError::UncUnsupported);
}

FE_TEST_CASE(toInternal_rejects_empty) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"", out), PathConvertError::Empty);
}

FE_TEST_CASE(toInternal_rejects_relative) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"relative\\path", out), PathConvertError::RelativeUnsupported);
  FE_ASSERT_EQ(toInternal(L"file.txt", out), PathConvertError::RelativeUnsupported);
}

FE_TEST_CASE(toInternal_rejects_invalid_syntax) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"C:\\bad<name>.txt", out), PathConvertError::InvalidSyntax);
  FE_ASSERT_EQ(toInternal(L"C:\\bad|pipe", out), PathConvertError::InvalidSyntax);
}

FE_TEST_CASE(toDisplay_strips_prefix) {
  FE_ASSERT_WSTREQ(toDisplay(L"\\\\?\\C:\\Users\\me"), L"C:\\Users\\me");
}

FE_TEST_CASE(toDisplay_passes_through_unprefixed) {
  FE_ASSERT_WSTREQ(toDisplay(L"C:\\Users\\me"), L"C:\\Users\\me");
  FE_ASSERT_WSTREQ(toDisplay(L""), L"");
}

FE_TEST_CASE(toDisplay_unfolds_unc_dos_prefix) {
  FE_ASSERT_WSTREQ(toDisplay(L"\\\\?\\UNC\\server\\share"), L"\\\\server\\share");
}

FE_TEST_CASE(isUncPath_detects_raw_unc) {
  FE_ASSERT_TRUE(isUncPath(L"\\\\server\\share"));
}

FE_TEST_CASE(isUncPath_detects_dos_unc_prefix) {
  FE_ASSERT_TRUE(isUncPath(L"\\\\?\\UNC\\server\\share"));
}

FE_TEST_CASE(isUncPath_rejects_extended_drive) {
  FE_ASSERT_FALSE(isUncPath(L"\\\\?\\C:\\Users\\me"));
}

FE_TEST_CASE(isUncPath_rejects_plain_drive) {
  FE_ASSERT_FALSE(isUncPath(L"C:\\Users\\me"));
}

// Regression: forward-slash UNC must be classified as UNC, not Relative.
FE_TEST_CASE(toInternal_rejects_forward_slash_unc) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"//server/share", out), PathConvertError::UncUnsupported);
}

FE_TEST_CASE(isUncPath_detects_forward_slash) {
  FE_ASSERT_TRUE(isUncPath(L"//server/share"));
}

// Regression: ADS-style colon inside the body must be rejected.
FE_TEST_CASE(toInternal_rejects_colon_in_body) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"C:\\bad:stream", out), PathConvertError::InvalidSyntax);
}

FE_TEST_CASE(toInternal_rejects_colon_in_prefixed_body) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"\\\\?\\C:\\bad:stream", out),
               PathConvertError::InvalidSyntax);
}

// Regression: a bare drive ("C:" without a backslash) must not be accepted,
// with or without the \\?\ prefix.
FE_TEST_CASE(toInternal_rejects_bare_drive) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"C:", out), PathConvertError::RelativeUnsupported);
}

FE_TEST_CASE(toInternal_rejects_prefixed_bare_drive) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"\\\\?\\C:", out), PathConvertError::InvalidSyntax);
}

FE_TEST_CASE(toInternal_accepts_drive_root) {
  std::wstring out;
  FE_ASSERT_EQ(toInternal(L"C:\\", out), PathConvertError::None);
  FE_ASSERT_WSTREQ(out, L"\\\\?\\C:\\");
}
