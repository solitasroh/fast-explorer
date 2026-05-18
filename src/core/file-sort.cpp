#include "file-sort.h"

#include <windows.h>

#include <cwctype>
#include <string>
#include <string_view>

#include "file-entry.h"

namespace fast_explorer::core {
namespace {

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

// Type description matches column-formatter's display: "File folder"
// for directories, "EXT File" (extension uppercased, leading dot
// dropped) for files with extensions, "File" otherwise. Type sort
// uses this rather than raw extension so the visible ordering tracks
// what the user sees in the Type column.
std::wstring typeDescription(const FileEntry& e) {
  if (isDirectory(e)) return L"File folder";
  std::wstring_view ext = extensionView(e);
  if (ext.empty()) return L"File";
  if (!ext.empty() && ext.front() == L'.') ext.remove_prefix(1);
  std::wstring out;
  out.reserve(ext.size() + 5);
  for (wchar_t c : ext) {
    out.push_back(static_cast<wchar_t>(std::towupper(static_cast<wint_t>(c))));
  }
  out.append(L" File");
  return out;
}

int compareTypeDescriptionAsc(const FileEntry& a, const FileEntry& b) {
  return compareOrdinalIgnoreCase(typeDescription(a), typeDescription(b));
}

int comparePrimary(const FileEntry& a,
                   const FileEntry& b,
                   SortKey key) {
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
      return compareTypeDescriptionAsc(a, b);
    case SortKey::None:
      return 0;
  }
  return 0;
}

}  // namespace

int compareEntries(const FileEntry& a,
                   const FileEntry& b,
                   SortSpec spec) {
  // Directories always group above files regardless of sort key and
  // direction. Matches Explorer's default "Group folders first"
  // behaviour and keeps the visual block contiguous.
  const bool da = isDirectory(a);
  const bool db = isDirectory(b);
  if (da != db) return da ? -1 : 1;
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
