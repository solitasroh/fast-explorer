#include "test-harness.h"

#include "core/file-entry.h"
#include "core/file-grouping.h"

#include <windows.h>

using fast_explorer::core::FileEntry;
using fast_explorer::core::GroupKey;
using fast_explorer::core::groupIdForEntry;
using fast_explorer::core::kNoExtension;

namespace {

FileEntry makeEntry(std::wstring_view name,
                    uint16_t extOffset = kNoExtension,
                    uint8_t entryFlags = 0,
                    uint64_t modified100ns = 0) {
  FileEntry e{};
  e.namePtr = name.data();
  e.nameLength = static_cast<uint16_t>(name.size());
  e.extensionOffset = extOffset;
  e.flags = entryFlags;
  e.modifiedTime100ns = modified100ns;
  return e;
}

// Build a FILETIME (UTC ticks since 1601) representing the given local
// wall-clock time. Mirrors the same conversion the production code uses.
uint64_t localFiletime(WORD year, WORD month, WORD day,
                       WORD hour, WORD minute) {
  SYSTEMTIME st{};
  st.wYear = year; st.wMonth = month; st.wDay = day;
  st.wHour = hour; st.wMinute = minute;
  FILETIME local{}, utc{};
  SystemTimeToFileTime(&st, &local);
  LocalFileTimeToFileTime(&local, &utc);
  ULARGE_INTEGER ui{};
  ui.LowPart = utc.dwLowDateTime;
  ui.HighPart = utc.dwHighDateTime;
  return ui.QuadPart;
}

// Mirrors what the FS backend stores in FileEntry::extensionOffset — the
// position of the LAST '.' in the name, or kNoExtension if there is none.
uint16_t findExtensionOffset(std::wstring_view name) {
  const auto pos = name.rfind(L'.');
  if (pos == std::wstring_view::npos) return kNoExtension;
  return static_cast<uint16_t>(pos);
}

}  // namespace

FE_TEST_CASE(group_none_returns_zero_for_every_entry) {
  auto e1 = makeEntry(L"foo.txt");
  auto e2 = makeEntry(L"bar");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::None, e1, 0), 0);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::None, e2, 0), 0);
}

FE_TEST_CASE(group_name_korean_choseong_first_syllable) {
  // U+AC00 = '가' → choseong index 0 (ㄱ)
  auto e = makeEntry(L"가나다.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 0);
}

FE_TEST_CASE(group_name_korean_choseong_middle_range) {
  // U+B9C8 = '마' → choseong index 6 (ㅁ)
  auto e = makeEntry(L"마라톤.exe");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 6);
}

FE_TEST_CASE(group_name_korean_choseong_last) {
  // '하' → choseong index 18 (ㅎ)
  auto e = makeEntry(L"하늘.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 18);
}

FE_TEST_CASE(group_name_latin_uppercase_A_is_19) {
  auto e = makeEntry(L"Apple.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 19);
}

FE_TEST_CASE(group_name_latin_lowercase_normalized) {
  auto e1 = makeEntry(L"apple.txt");
  auto e2 = makeEntry(L"APPLE.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0),
               groupIdForEntry(GroupKey::Name, e2, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0), 19);
}

FE_TEST_CASE(group_name_latin_Z_is_44) {
  auto e = makeEntry(L"zebra.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 44);
}

FE_TEST_CASE(group_name_digit_is_45) {
  auto e1 = makeEntry(L"9-readme.txt");
  auto e2 = makeEntry(L"0.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0), 45);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e2, 0), 45);
}

FE_TEST_CASE(group_name_symbol_is_other_46) {
  auto e1 = makeEntry(L"_foo.txt");
  auto e2 = makeEntry(L"!bar.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0), 46);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e2, 0), 46);
}

FE_TEST_CASE(group_name_compat_jamo_normalized_to_choseong) {
  // U+3131 (ㄱ, Hangul Compatibility Jamo) maps to choseong ㄱ (id 0)
  auto e = makeEntry(L"ㄱ-file.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 0);
  // U+314E (ㅎ) maps to choseong ㅎ (id 18)
  auto e2 = makeEntry(L"ㅎ-file.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e2, 0), 18);
}

FE_TEST_CASE(group_name_empty_name_is_other) {
  auto e = makeEntry(L"");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 46);
}

FE_TEST_CASE(group_modified_today) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);  // Mon noon
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 15, 0, 1));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 0);
}

FE_TEST_CASE(group_modified_yesterday) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 14, 23, 59));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 1);
}

FE_TEST_CASE(group_modified_this_week) {
  // now = Wed 2026-06-17 12:00; this-week start = Mon 2026-06-15 00:00.
  // A Mon noon item (2026-06-15 09:00) is in this-week bucket (>= week start,
  // older than yesterday).
  const uint64_t now = localFiletime(2026, 6, 17, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 15, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 2);
}

FE_TEST_CASE(group_modified_this_month) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  // Sunday from 2 weeks ago (2026-06-01) — same month, before this-week-start.
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 1, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 3);
}

FE_TEST_CASE(group_modified_this_year) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  // February of same year — before this-month, in this-year.
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 2, 1, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 4);
}

FE_TEST_CASE(group_modified_older) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2024, 1, 1, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 5);
}

FE_TEST_CASE(group_modified_future_clamps_to_today) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2027, 1, 1, 0, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 0);
}

FE_TEST_CASE(group_type_folder_is_zero) {
  auto e = makeEntry(L"My Folder", kNoExtension,
                     fast_explorer::core::file_entry_flags::kIsDirectory);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e, 0), 0);
}

FE_TEST_CASE(group_type_no_extension_is_one) {
  auto e = makeEntry(L"README", kNoExtension);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e, 0), 1);
}

FE_TEST_CASE(group_type_same_extension_same_id) {
  auto e1 = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  auto e2 = makeEntry(L"different.txt", findExtensionOffset(L"different.txt"));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e1, 0),
               groupIdForEntry(GroupKey::Type, e2, 0));
}

FE_TEST_CASE(group_type_different_extension_different_id) {
  auto e1 = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  auto e2 = makeEntry(L"a.pdf", findExtensionOffset(L"a.pdf"));
  FE_ASSERT_NE(groupIdForEntry(GroupKey::Type, e1, 0),
               groupIdForEntry(GroupKey::Type, e2, 0));
}

FE_TEST_CASE(group_type_extension_is_case_insensitive) {
  auto e1 = makeEntry(L"a.TXT", findExtensionOffset(L"a.TXT"));
  auto e2 = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e1, 0),
               groupIdForEntry(GroupKey::Type, e2, 0));
}

FE_TEST_CASE(group_type_extension_is_above_one) {
  auto e = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  FE_ASSERT_TRUE(groupIdForEntry(GroupKey::Type, e, 0) > 1);
}
