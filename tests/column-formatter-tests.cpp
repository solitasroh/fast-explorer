#include <windows.h>

#include "test-harness.h"
#include "ui/column-formatter.h"

using fast_explorer::ui::formatModified;
using fast_explorer::ui::formatSize;
using fast_explorer::ui::formatType;

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
