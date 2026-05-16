#include "ui/folder-name.h"

#include <windows.h>

#include <string>

namespace fast_explorer::ui {

namespace {

bool equalIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  if (a.empty()) {
    return true;
  }
  // CompareStringOrdinal matches Win32 NTFS case-folding (Unicode
  // upcase table, no locale dependency); towlower would only fold
  // ASCII correctly.
  return CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
                              b.data(), static_cast<int>(b.size()),
                              TRUE) == CSTR_EQUAL;
}

bool nameInUse(std::span<const std::wstring_view> existing,
               std::wstring_view candidate) noexcept {
  for (const auto& name : existing) {
    if (equalIgnoreCase(name, candidate)) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::wstring uniqueFolderLeaf(
    std::span<const std::wstring_view> existingNames,
    std::wstring_view base) {
  std::wstring candidate(base);
  if (!nameInUse(existingNames, candidate)) {
    return candidate;
  }
  for (int suffix = 2; suffix < 1'000'000; ++suffix) {
    candidate.assign(base);
    candidate.append(L" (");
    candidate.append(std::to_wstring(suffix));
    candidate.append(L")");
    if (!nameInUse(existingNames, candidate)) {
      return candidate;
    }
  }
  return candidate;
}

}  // namespace fast_explorer::ui
