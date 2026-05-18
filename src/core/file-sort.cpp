#include "file-sort.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
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

// Display description matching column-formatter: "File folder" for
// directories, "EXT File" (uppercase ext, leading dot dropped) for
// files with extensions, "File" otherwise. Stack buffer avoids the
// heap allocation per comparison that std::sort would otherwise make
// O(n log n).
struct TypeDescBuf {
  std::array<wchar_t, 32> buf{};
  std::size_t len = 0;
  std::wstring_view view() const noexcept {
    return std::wstring_view(buf.data(), len);
  }
};

TypeDescBuf typeDescription(const FileEntry& e) {
  TypeDescBuf out;
  if (isDirectory(e)) {
    static constexpr std::wstring_view kFolder = L"File folder";
    std::memcpy(out.buf.data(), kFolder.data(),
                kFolder.size() * sizeof(wchar_t));
    out.len = kFolder.size();
    return out;
  }
  std::wstring_view ext = extensionView(e);
  if (!ext.empty() && ext.front() == L'.') ext.remove_prefix(1);
  if (ext.empty()) {
    static constexpr std::wstring_view kFile = L"File";
    std::memcpy(out.buf.data(), kFile.data(),
                kFile.size() * sizeof(wchar_t));
    out.len = kFile.size();
    return out;
  }
  // Reserve 5 for trailing " File"; cap the extension to fit.
  const std::size_t cap = out.buf.size() - 5;
  const std::size_t n = std::min(ext.size(), cap);
  for (std::size_t i = 0; i < n; ++i) {
    out.buf[i] = static_cast<wchar_t>(
        std::towupper(static_cast<wint_t>(ext[i])));
  }
  static constexpr wchar_t kSuffix[5] = {L' ', L'F', L'i', L'l', L'e'};
  std::memcpy(out.buf.data() + n, kSuffix, sizeof(kSuffix));
  out.len = n + 5;
  return out;
}

int compareTypeDescriptionAsc(const FileEntry& a, const FileEntry& b) {
  return compareOrdinalIgnoreCase(typeDescription(a).view(),
                                   typeDescription(b).view());
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
                   SortSpec spec) noexcept {
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
