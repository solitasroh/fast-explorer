#include "test-harness.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <stop_token>
#include <string>

#include "core/file-entry.h"
#include "core/fs-backend.h"
#include "core/win32-fs-backend.h"

using fast_explorer::core::EnumerationError;
using fast_explorer::core::EnumerationHandle;
using fast_explorer::core::extensionView;
using fast_explorer::core::FileEntry;
using fast_explorer::core::file_entry_flags::kIsDirectory;
using fast_explorer::core::file_entry_flags::kIsHidden;
using fast_explorer::core::isDirectory;
using fast_explorer::core::isHidden;
using fast_explorer::core::kNoExtension;
using fast_explorer::core::nameView;
using fast_explorer::core::Result;
using fast_explorer::core::Win32FsBackend;

namespace {

class TempDir {
 public:
  TempDir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD len = ::GetTempPathW(MAX_PATH, buf);
    std::filesystem::path base(std::wstring_view(buf, len));
    static std::atomic<unsigned long long> counter{0};
    const auto pid = static_cast<unsigned long>(::GetCurrentProcessId());
    const auto seq = counter.fetch_add(1);
    path_ = base / (L"fast-explorer-test-" + std::to_wstring(pid) + L"-" +
                    std::to_wstring(seq));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  std::wstring path() const { return path_.wstring(); }

  void touch(const std::wstring& name) {
    std::ofstream f(path_ / name);
  }

  void writeBytes(const std::wstring& name, std::size_t bytes) {
    std::ofstream f(path_ / name, std::ios::binary);
    if (bytes > 0) {
      const std::string payload(bytes, 'x');
      f.write(payload.data(), static_cast<std::streamsize>(bytes));
    }
  }

  void makeSubdir(const std::wstring& name) {
    std::filesystem::create_directory(path_ / name);
  }

  void setHidden(const std::wstring& name) {
    const auto full = (path_ / name).wstring();
    const DWORD attr = ::GetFileAttributesW(full.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
      ::SetFileAttributesW(full.c_str(), attr | FILE_ATTRIBUTE_HIDDEN);
    }
  }

 private:
  std::filesystem::path path_;
};

std::set<std::wstring> collectNames(Win32FsBackend& backend,
                                    EnumerationHandle& handle) {
  std::set<std::wstring> names;
  for (;;) {
    auto r = backend.next(handle, std::stop_token{});
    FE_ASSERT_TRUE(r.ok());
    if (!r.value.has_value()) {
      break;
    }
    names.emplace(nameView(*r.value));
  }
  return names;
}

}  // namespace

FE_TEST_CASE(win32_fs_open_nonexistent_path_returns_path_not_found) {
  Win32FsBackend backend;
  auto r = backend.openEnumeration(
      L"C:\\does-not-exist-fast-explorer-zzzz-9876543210",
      std::stop_token{});
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::PathNotFound));
}

FE_TEST_CASE(win32_fs_open_empty_path_returns_invalid_syntax) {
  Win32FsBackend backend;
  auto r = backend.openEnumeration(L"", std::stop_token{});
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::InvalidSyntax));
}

FE_TEST_CASE(win32_fs_open_with_stop_requested_returns_canceled) {
  TempDir dir;
  dir.touch(L"a.txt");
  Win32FsBackend backend;
  std::stop_source src;
  src.request_stop();
  auto r = backend.openEnumeration(dir.path(), src.get_token());
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::Canceled));
}

FE_TEST_CASE(win32_fs_streams_files_in_empty_directory_after_filtering_dots) {
  TempDir dir;
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  const auto names = collectNames(backend, *open.value);
  FE_ASSERT_TRUE(names.empty());
}

FE_TEST_CASE(win32_fs_dot_and_dotdot_are_filtered) {
  TempDir dir;
  dir.touch(L"a.txt");
  dir.touch(L"b.txt");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  const auto names = collectNames(backend, *open.value);
  FE_ASSERT_TRUE(names.find(L".") == names.end());
  FE_ASSERT_TRUE(names.find(L"..") == names.end());
  FE_ASSERT_EQ(names.size(), static_cast<std::size_t>(2));
}

FE_TEST_CASE(win32_fs_streams_files_and_subdirs) {
  TempDir dir;
  dir.touch(L"readme.txt");
  dir.touch(L"image.png");
  dir.makeSubdir(L"sub");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  const auto names = collectNames(backend, *open.value);
  FE_ASSERT_EQ(names.size(), static_cast<std::size_t>(3));
  FE_ASSERT_TRUE(names.find(L"readme.txt") != names.end());
  FE_ASSERT_TRUE(names.find(L"image.png") != names.end());
  FE_ASSERT_TRUE(names.find(L"sub") != names.end());
}

FE_TEST_CASE(win32_fs_subdir_flag_is_set_for_directories_only) {
  TempDir dir;
  dir.touch(L"file");
  dir.makeSubdir(L"folder");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());

  bool sawFile = false;
  bool sawFolder = false;
  for (;;) {
    auto r = backend.next(*open.value, std::stop_token{});
    FE_ASSERT_TRUE(r.ok());
    if (!r.value.has_value()) break;
    const FileEntry& e = *r.value;
    if (nameView(e) == L"file") {
      sawFile = true;
      FE_ASSERT_FALSE(isDirectory(e));
    } else if (nameView(e) == L"folder") {
      sawFolder = true;
      FE_ASSERT_TRUE(isDirectory(e));
    }
  }
  FE_ASSERT_TRUE(sawFile);
  FE_ASSERT_TRUE(sawFolder);
}

FE_TEST_CASE(win32_fs_hidden_flag_is_set_for_hidden_files) {
  TempDir dir;
  dir.touch(L"normal.txt");
  dir.touch(L"secret.txt");
  dir.setHidden(L"secret.txt");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());

  bool secretSawHidden = false;
  bool normalSawNotHidden = false;
  for (;;) {
    auto r = backend.next(*open.value, std::stop_token{});
    FE_ASSERT_TRUE(r.ok());
    if (!r.value.has_value()) break;
    if (nameView(*r.value) == L"secret.txt") {
      secretSawHidden = isHidden(*r.value);
    } else if (nameView(*r.value) == L"normal.txt") {
      normalSawNotHidden = !isHidden(*r.value);
    }
  }
  FE_ASSERT_TRUE(secretSawHidden);
  FE_ASSERT_TRUE(normalSawNotHidden);
}

FE_TEST_CASE(win32_fs_size_is_round_tripped) {
  TempDir dir;
  dir.writeBytes(L"sized.bin", 1234);
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto r = backend.next(*open.value, std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value.has_value());
  FE_ASSERT_EQ(r.value->size, static_cast<uint64_t>(1234));
}

FE_TEST_CASE(win32_fs_extension_offset_points_at_last_dot) {
  TempDir dir;
  dir.touch(L"archive.tar.gz");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto r = backend.next(*open.value, std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value.has_value());
  const FileEntry& e = *r.value;
  FE_ASSERT_NE(e.extensionOffset, kNoExtension);
  FE_ASSERT_TRUE(extensionView(e) == std::wstring_view(L".gz"));
}

FE_TEST_CASE(win32_fs_dotfile_has_no_extension) {
  TempDir dir;
  dir.touch(L".bashrc");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto r = backend.next(*open.value, std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value.has_value());
  FE_ASSERT_EQ(r.value->extensionOffset, kNoExtension);
}

FE_TEST_CASE(win32_fs_modified_time_is_nonzero) {
  TempDir dir;
  dir.touch(L"timestamped.txt");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto r = backend.next(*open.value, std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value.has_value());
  FE_ASSERT_TRUE(r.value->modifiedTime100ns != 0);
}

FE_TEST_CASE(win32_fs_name_pointer_stays_valid_until_handle_destroyed) {
  TempDir dir;
  dir.touch(L"persistent.bin");
  Win32FsBackend backend;
  auto open = backend.openEnumeration(dir.path(), std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto r = backend.next(*open.value, std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value.has_value());
  const wchar_t* firstPtr = r.value->namePtr;
  auto end = backend.next(*open.value, std::stop_token{});
  FE_ASSERT_TRUE(end.ok());
  FE_ASSERT_FALSE(end.value.has_value());
  FE_ASSERT_EQ(
      std::memcmp(firstPtr, L"persistent.bin", 14 * sizeof(wchar_t)),
      0);
}
