#include "file-sort.h"

#include <windows.h>

#include <string_view>

#include "file-entry.h"

namespace fast_explorer::core {
namespace {

// CompareStringOrdinal requires non-null pointers even when the length is
// zero; an empty wstring_view may legitimately carry a null .data(), so
// substitute a stable empty literal in that case.
constexpr const wchar_t* safeData(const wchar_t* p) noexcept {
  return p ? p : L"";
}

// CompareStringOrdinal returns CSTR_LESS_THAN(1) / CSTR_EQUAL(2) /
// CSTR_GREATER_THAN(3); subtracting CSTR_EQUAL yields the conventional
// <0/0/>0 tri-state used throughout this module.
int compareOrdinalIgnoreCase(std::wstring_view a,
                             std::wstring_view b) noexcept {
  const int rc = ::CompareStringOrdinal(safeData(a.data()),
                                        static_cast<int>(a.size()),
                                        safeData(b.data()),
                                        static_cast<int>(b.size()),
                                        TRUE);
  return rc - CSTR_EQUAL;
}

int compareNameAsc(const FileEntry& a, const FileEntry& b) noexcept {
  return compareOrdinalIgnoreCase(nameView(a), nameView(b));
}

int compareExtensionAsc(const FileEntry& a, const FileEntry& b) noexcept {
  return compareOrdinalIgnoreCase(extensionView(a), extensionView(b));
}

int comparePrimary(const FileEntry& a,
                   const FileEntry& b,
                   SortKey key) noexcept {
  switch (key) {
    case SortKey::Name:
      return compareNameAsc(a, b);
    case SortKey::Size:
      if (a.size < b.size) return -1;
      if (a.size > b.size) return 1;
      return 0;
    case SortKey::Modified:
      if (a.modifiedTime100ns < b.modifiedTime100ns) return -1;
      if (a.modifiedTime100ns > b.modifiedTime100ns) return 1;
      return 0;
    case SortKey::Type:
      return compareExtensionAsc(a, b);
  }
  return 0;
}

}  // namespace

int compareEntries(const FileEntry& a,
                   const FileEntry& b,
                   SortSpec spec) noexcept {
  int primary = comparePrimary(a, b, spec.key);
  if (spec.direction == SortDirection::Descending) {
    primary = -primary;
  }
  if (primary != 0) {
    return primary;
  }
  return compareNameAsc(a, b);
}

}  // namespace fast_explorer::core
