#include <windows.h>

#include "core/file-entry.h"
#include "test-harness.h"
#include "explorer/column-formatter.h"

using fast_explorer::core::FileEntry;
using fast_explorer::core::file_entry_flags::kIsCloudPlaceholder;
using fast_explorer::core::file_entry_flags::kIsHidden;
using fast_explorer::core::file_entry_flags::kIsReparse;
using fast_explorer::core::file_entry_flags::kIsSymlink;
using fast_explorer::core::file_entry_flags::kIsSystem;
using fast_explorer::ui::formatAttributesForEntry;
using fast_explorer::ui::formatModified;
using fast_explorer::ui::formatSize;
using fast_explorer::ui::formatType;
using fast_explorer::ui::shouldRenderDimmed;

namespace {

FileEntry makeEntry(uint8_t flags, uint32_t attributes) {
  FileEntry e{};
  e.flags = flags;
  e.attributes = attributes;
  return e;
}

}  // namespace

FE_TEST_CASE(ColumnFormatter_Size_Zero) {
  FE_ASSERT_WSTREQ(formatSize(0), L"0 B");
}

FE_TEST_CASE(ColumnFormatter_Size_SmallBytes) {
  FE_ASSERT_WSTREQ(formatSize(1), L"1 B");
  FE_ASSERT_WSTREQ(formatSize(512), L"512 B");
  FE_ASSERT_WSTREQ(formatSize(1023), L"1023 B");
}

FE_TEST_CASE(ColumnFormatter_Size_KB) {
  FE_ASSERT_WSTREQ(formatSize(1024), L"1.0 KB");
  FE_ASSERT_WSTREQ(formatSize(1024 * 5 + 512), L"5.5 KB");
}

FE_TEST_CASE(ColumnFormatter_Size_MB) {
  FE_ASSERT_WSTREQ(formatSize(1024ULL * 1024), L"1.0 MB");
  FE_ASSERT_WSTREQ(formatSize(static_cast<uint64_t>(1024ULL * 1024 * 3.5)),
                   L"3.5 MB");
}

FE_TEST_CASE(ColumnFormatter_Size_GB) {
  FE_ASSERT_WSTREQ(formatSize(1024ULL * 1024 * 1024), L"1.0 GB");
}

FE_TEST_CASE(ColumnFormatter_Size_TB) {
  FE_ASSERT_WSTREQ(formatSize(1024ULL * 1024 * 1024 * 1024), L"1.0 TB");
}

FE_TEST_CASE(ColumnFormatter_Type_Directory) {
  FE_ASSERT_WSTREQ(formatType(L".anything", true), L"File folder");
  FE_ASSERT_WSTREQ(formatType(L"", true), L"File folder");
}

FE_TEST_CASE(ColumnFormatter_Type_NoExtension) {
  FE_ASSERT_WSTREQ(formatType(L"", false), L"File");
}

FE_TEST_CASE(ColumnFormatter_Type_WithExtension) {
  FE_ASSERT_WSTREQ(formatType(L".txt", false), L"TXT File");
  FE_ASSERT_WSTREQ(formatType(L".cpp", false), L"CPP File");
  FE_ASSERT_WSTREQ(formatType(L".PNG", false), L"PNG File");
}

FE_TEST_CASE(ColumnFormatter_Modified_ZeroReturnsEmpty) {
  FE_ASSERT_WSTREQ(formatModified(0), L"");
}

FE_TEST_CASE(ColumnFormatter_Modified_NonZeroProducesString) {
  // 2026-01-01 00:00:00 UTC in FILETIME 100-ns intervals.
  // Use Win32 to convert a known SYSTEMTIME -> FILETIME for determinism
  // across CI machines; assert only the year-month prefix to avoid
  // timezone dependence on the host.
  SYSTEMTIME st{};
  st.wYear = 2026;
  st.wMonth = 5;
  st.wDay = 15;
  st.wHour = 12;
  st.wMinute = 30;
  FILETIME ft{};
  FE_ASSERT_TRUE(SystemTimeToFileTime(&st, &ft));
  const uint64_t ft100ns =
      (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  const std::wstring formatted = formatModified(ft100ns);
  FE_ASSERT_TRUE(formatted.find(L"2026-05") != std::wstring::npos);
}

FE_TEST_CASE(ColumnFormatter_Attributes_None_Empty) {
  FE_ASSERT_WSTREQ(formatAttributesForEntry(makeEntry(0, 0)), L"");
}

FE_TEST_CASE(ColumnFormatter_Attributes_HiddenOnly) {
  FE_ASSERT_WSTREQ(formatAttributesForEntry(makeEntry(kIsHidden, 0)), L"H");
}

FE_TEST_CASE(ColumnFormatter_Attributes_SystemOnly) {
  FE_ASSERT_WSTREQ(formatAttributesForEntry(makeEntry(kIsSystem, 0)), L"S");
}

FE_TEST_CASE(ColumnFormatter_Attributes_ReadOnlyOnly) {
  FE_ASSERT_WSTREQ(
      formatAttributesForEntry(makeEntry(0, FILE_ATTRIBUTE_READONLY)), L"R");
}

FE_TEST_CASE(ColumnFormatter_Attributes_JunctionWhenReparseWithoutSymlink) {
  FE_ASSERT_WSTREQ(formatAttributesForEntry(makeEntry(kIsReparse, 0)), L"J");
}

FE_TEST_CASE(ColumnFormatter_Attributes_SymlinkWhenReparseAndSymlinkBitSet) {
  FE_ASSERT_WSTREQ(
      formatAttributesForEntry(makeEntry(kIsReparse | kIsSymlink, 0)), L"L");
}

FE_TEST_CASE(ColumnFormatter_Attributes_CloudOnly) {
  FE_ASSERT_WSTREQ(
      formatAttributesForEntry(makeEntry(kIsCloudPlaceholder, 0)), L"C");
}

FE_TEST_CASE(ColumnFormatter_Attributes_FixedOrderRegardlessOfFlagOrder) {
  const auto e = makeEntry(kIsSystem | kIsHidden | kIsCloudPlaceholder,
                           FILE_ATTRIBUTE_READONLY);
  FE_ASSERT_WSTREQ(formatAttributesForEntry(e), L"HSRC");
}

FE_TEST_CASE(ColumnFormatter_Attributes_AllSixMarkers) {
  const auto e = makeEntry(
      kIsHidden | kIsSystem | kIsReparse | kIsSymlink | kIsCloudPlaceholder,
      FILE_ATTRIBUTE_READONLY);
  // J is suppressed because kIsSymlink is set; output should be HSRLC.
  FE_ASSERT_WSTREQ(formatAttributesForEntry(e), L"HSRLC");
}

FE_TEST_CASE(ColumnFormatter_ShouldRenderDimmed_NormalEntry_False) {
  FE_ASSERT_FALSE(shouldRenderDimmed(makeEntry(0, 0)));
}

FE_TEST_CASE(ColumnFormatter_ShouldRenderDimmed_Hidden_True) {
  FE_ASSERT_TRUE(shouldRenderDimmed(makeEntry(kIsHidden, 0)));
}

FE_TEST_CASE(ColumnFormatter_ShouldRenderDimmed_System_True) {
  FE_ASSERT_TRUE(shouldRenderDimmed(makeEntry(kIsSystem, 0)));
}

FE_TEST_CASE(ColumnFormatter_ShouldRenderDimmed_ReadOnlyAlone_False) {
  FE_ASSERT_FALSE(
      shouldRenderDimmed(makeEntry(0, FILE_ATTRIBUTE_READONLY)));
}
