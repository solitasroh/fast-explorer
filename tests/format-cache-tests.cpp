#include <windows.h>

#include "core/file-entry.h"
#include "test-harness.h"
#include "ui/format-cache.h"

using fast_explorer::core::FileEntry;
using fast_explorer::core::file_entry_flags::kIsDirectory;
using fast_explorer::ui::FormatCache;

namespace {

FileEntry makeFile(uint64_t size, const wchar_t* name,
                   uint16_t extOffset = 0) {
  FileEntry e{};
  e.namePtr = name;
  e.nameLength = static_cast<uint16_t>(wcslen(name));
  e.size = size;
  e.extensionOffset =
      extOffset == 0 ? fast_explorer::core::kNoExtension : extOffset;
  return e;
}

FileEntry makeDir(const wchar_t* name) {
  FileEntry e = makeFile(0, name, 0);
  e.flags = kIsDirectory;
  return e;
}

}  // namespace

FE_TEST_CASE(FormatCache_Size_File) {
  FormatCache cache;
  FileEntry e = makeFile(1024, L"file.txt");
  FE_ASSERT_WSTREQ(cache.sizeForEntry(e), L"1.0 KB");
}

FE_TEST_CASE(FormatCache_Size_Directory_ReturnsEmpty) {
  FormatCache cache;
  FileEntry d = makeDir(L"folder");
  FE_ASSERT_WSTREQ(cache.sizeForEntry(d), L"");
}

FE_TEST_CASE(FormatCache_Size_RepeatedKeyIsCached) {
  FormatCache cache;
  FileEntry a = makeFile(2048, L"a.txt");
  FileEntry b = makeFile(2048, L"b.txt");
  const std::wstring& first = cache.sizeForEntry(a);
  const std::wstring& second = cache.sizeForEntry(b);
  FE_ASSERT_WSTREQ(first, second);
  FE_ASSERT_TRUE(&first == &second);  // same cache slot
}

FE_TEST_CASE(FormatCache_Type_Directory) {
  FormatCache cache;
  FileEntry d = makeDir(L"folder");
  FE_ASSERT_WSTREQ(cache.typeForEntry(d), L"File folder");
}

FE_TEST_CASE(FormatCache_Type_FileWithExtension) {
  FormatCache cache;
  // "file.txt" has '.' at offset 4; extension is ".txt" (4 chars).
  FileEntry e = makeFile(100, L"file.txt", 4);
  FE_ASSERT_WSTREQ(cache.typeForEntry(e), L"TXT File");
}

FE_TEST_CASE(FormatCache_Modified_ZeroReturnsEmpty) {
  FormatCache cache;
  FE_ASSERT_WSTREQ(cache.modifiedAt(0), L"");
}

FE_TEST_CASE(FormatCache_Modified_SameMinuteCollapses) {
  FormatCache cache;
  // Two FILETIMEs in the same minute should collide on the cache.
  SYSTEMTIME st{};
  st.wYear = 2026;
  st.wMonth = 5;
  st.wDay = 15;
  st.wHour = 10;
  st.wMinute = 30;
  st.wSecond = 5;
  FILETIME ft{};
  FE_ASSERT_TRUE(SystemTimeToFileTime(&st, &ft));
  const uint64_t base =
      (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  const uint64_t laterSameMinute = base + 50ULL * 10'000'000ULL;  // +50 s

  const std::wstring& first = cache.modifiedAt(base);
  const std::wstring& second = cache.modifiedAt(laterSameMinute);
  FE_ASSERT_TRUE(&first == &second);
}

FE_TEST_CASE(FormatCache_Capacity_EvictsLruEntry) {
  FormatCache cache(2);
  FileEntry a = makeFile(100, L"a");
  FileEntry b = makeFile(200, L"b");
  FileEntry c = makeFile(300, L"c");

  const std::wstring* pa = &cache.sizeForEntry(a);
  cache.sizeForEntry(b);
  cache.sizeForEntry(c);  // evicts 'a' (oldest)
  // Re-inserting 'a' must produce a new entry, not reuse pa.
  const std::wstring* paAgain = &cache.sizeForEntry(a);
  FE_ASSERT_TRUE(pa != paAgain);
}
