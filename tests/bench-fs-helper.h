#pragma once

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace fast_explorer::tests {

// True if `path` exists on disk (file or directory). Returns true for
// ERROR_ACCESS_DENIED and similar non-not-found errors so the helper
// does not paper over real permission failures with a false negative.
inline bool diskPathExists(const std::wstring& path) {
  const DWORD attr = GetFileAttributesW(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES) {
    return true;
  }
  const DWORD err = GetLastError();
  return err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND;
}

// Creates `path` as a zero-byte file, overwriting any prior content.
// No-op on CreateFileW failure; callers gate behavior on a subsequent
// diskPathExists(path) check.
inline void writeEmptyDiskFile(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
}

// Recursively deletes `path` and everything under it. Best-effort: returns
// false if any node could not be removed. Uses Win32 only.
inline bool removeDirectoryRecursive(const std::wstring& path) {
  std::wstring pattern = path + L"\\*";
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                              FindExSearchNameMatch, nullptr,
                              FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) {
    return RemoveDirectoryW(path.c_str()) != 0;
  }
  bool ok = true;
  do {
    if (wcscmp(fd.cFileName, L".") == 0 ||
        wcscmp(fd.cFileName, L"..") == 0) {
      continue;
    }
    std::wstring child = path + L"\\" + fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!removeDirectoryRecursive(child)) {
        ok = false;
      }
    } else {
      if (!DeleteFileW(child.c_str())) {
        ok = false;
      }
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  if (!RemoveDirectoryW(path.c_str())) {
    ok = false;
  }
  return ok;
}

// Returns a fresh temp directory path. The path does NOT yet exist on
// disk; caller is expected to ask the unit under test to create it.
inline std::wstring makeFreshTempDirPath(const wchar_t* label) {
  wchar_t buf[MAX_PATH];
  DWORD len = GetTempPathW(MAX_PATH, buf);
  std::wstring base(buf, len);
  if (!base.empty() && base.back() != L'\\') {
    base.push_back(L'\\');
  }
  base.append(L"fe-test-");
  base.append(label);
  base.push_back(L'-');
  wchar_t tail[64];
  swprintf_s(tail, _countof(tail), L"%lu-%llu", GetCurrentProcessId(),
             static_cast<unsigned long long>(GetTickCount64()));
  base.append(tail);
  return base;
}

// RAII wrapper that allocates a fresh temp-dir path at construction
// (the directory itself is created by the unit under test) and
// recursively removes the tree at destruction, including the
// exception-unwind path triggered by FE_ASSERT_*.
class TempDir {
 public:
  explicit TempDir(const wchar_t* label) : path_(makeFreshTempDirPath(label)) {}
  ~TempDir() { removeDirectoryRecursive(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::wstring& path() const noexcept { return path_; }

 private:
  std::wstring path_;
};

}  // namespace fast_explorer::tests
