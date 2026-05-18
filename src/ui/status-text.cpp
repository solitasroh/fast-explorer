#include "ui/status-text.h"

#include <cwchar>

namespace fast_explorer::ui {

namespace {

const wchar_t* enumerationErrorName(
    fast_explorer::core::EnumerationError e) {
  using fast_explorer::core::EnumerationError;
  switch (e) {
    case EnumerationError::None: return L"None";
    case EnumerationError::PathNotFound: return L"PathNotFound";
    case EnumerationError::FileNotFound: return L"FileNotFound";
    case EnumerationError::AccessDenied: return L"AccessDenied";
    case EnumerationError::SharingViolation: return L"SharingViolation";
    case EnumerationError::NotReady: return L"NotReady";
    case EnumerationError::DirectoryNotSupported:
      return L"DirectoryNotSupported";
    case EnumerationError::InvalidSyntax: return L"InvalidSyntax";
    case EnumerationError::UncUnsupported: return L"UncUnsupported";
    case EnumerationError::Canceled: return L"Canceled";
    case EnumerationError::Internal: return L"Internal";
  }
  return L"Unknown";
}

}  // namespace

std::wstring loadingStatusText(const std::wstring& path) {
  std::wstring out;
  out.reserve(path.size() + 16);
  out.append(L"Loading: ");
  out.append(path);
  return out;
}

std::wstring loadingProgressStatusText(uint64_t itemsSoFar) {
  wchar_t buf[64];
  swprintf_s(buf, _countof(buf), L"Loading: %llu items",
             static_cast<unsigned long long>(itemsSoFar));
  return buf;
}

std::wstring readyStatusText(size_t itemCount) {
  wchar_t buf[64];
  swprintf_s(buf, _countof(buf), L"%llu items",
             static_cast<unsigned long long>(itemCount));
  return buf;
}

std::wstring humanReadableSize(std::uint64_t bytes) {
  // Below 1 KiB the byte count is printed verbatim with the "B"
  // suffix; above, we scale to the largest unit that keeps the
  // value in [1.0, 1024.0) and render one decimal place. kUnits
  // is named for the precondition that the caller already
  // divided by 1024 once before consulting the table.
  constexpr std::uint64_t kKiB = 1024ull;
  if (bytes < kKiB) {
    wchar_t buf[32];
    swprintf_s(buf, _countof(buf), L"%llu B",
               static_cast<unsigned long long>(bytes));
    return buf;
  }
  static const wchar_t* const kUnitsAboveKiB[] = {
      L"KB", L"MB", L"GB", L"TB", L"PB", L"EB"};
  double value = static_cast<double>(bytes) / 1024.0;
  std::size_t unitIdx = 0;
  // Compare against 1023.95 so a value like 1023.99 KB rounds up
  // to "1.0 MB" via the next-unit jump rather than printing
  // "1024.0 KB" via swprintf rounding.
  constexpr double kNextUnitThreshold = 1023.95;
  while (value >= kNextUnitThreshold &&
         unitIdx + 1 < (sizeof(kUnitsAboveKiB) / sizeof(*kUnitsAboveKiB))) {
    value /= 1024.0;
    ++unitIdx;
  }
  wchar_t buf[32];
  swprintf_s(buf, _countof(buf), L"%.1f %ls", value,
             kUnitsAboveKiB[unitIdx]);
  return buf;
}

std::wstring formatSelectionSummary(std::size_t totalCount,
                                    std::size_t selectedCount,
                                    std::uint64_t selectedBytes) {
  if (selectedCount == 0) {
    return readyStatusText(totalCount);
  }
  const std::wstring size = humanReadableSize(selectedBytes);
  // Worst-case bound: two uint64_t in decimal (20 wchars each), the
  // literal "items | " / " selected (" / ")" prefixes (~24 wchars),
  // plus humanReadableSize output (≤ ~10 wchars: "9999.9 XB"). Total
  // ≤ ~74 wchars, comfortably inside the 128-wchar buffer.
  wchar_t buf[128];
  swprintf_s(buf, _countof(buf),
             L"%llu items | %llu selected (%ls)",
             static_cast<unsigned long long>(totalCount),
             static_cast<unsigned long long>(selectedCount),
             size.c_str());
  return buf;
}

std::wstring errorStatusText(fast_explorer::core::EnumerationError err) {
  std::wstring out;
  out.reserve(32);
  out.append(L"Error: ");
  out.append(enumerationErrorName(err));
  return out;
}

namespace {

std::wstring_view leafOf(const std::wstring& path) noexcept {
  if (path.empty()) {
    return {};
  }
  const std::size_t sep = path.find_last_of(L"\\/");
  if (sep == std::wstring::npos) {
    return path;
  }
  return std::wstring_view(path).substr(sep + 1);
}

void appendQuoted(std::wstring& out, std::wstring_view name) {
  out.append(L"'");
  out.append(name);
  out.append(L"'");
}

}  // namespace

std::wstring opResultStatusText(const OperationResult& result) {
  std::wstring out;
  out.reserve(64);
  const std::wstring_view leaf = leafOf(result.sourcePath);
  switch (result.kind) {
    case ShellCommandKind::Delete:
      if (result.success) {
        out.append(L"Moved ");
        appendQuoted(out, leaf);
        out.append(L" to Recycle Bin");
      } else {
        out.append(L"Failed to delete ");
        appendQuoted(out, leaf);
      }
      break;
    case ShellCommandKind::Rename:
      if (result.success) {
        out.append(L"Renamed ");
        appendQuoted(out, leaf);
        out.append(L" to ");
        appendQuoted(out, result.newName);
      } else {
        out.append(L"Failed to rename ");
        appendQuoted(out, leaf);
      }
      break;
    case ShellCommandKind::CreateFolder:
      if (result.success) {
        out.append(L"Created folder ");
        appendQuoted(out, result.newName);
      } else {
        out.append(L"Failed to create folder ");
        appendQuoted(out, result.newName);
      }
      break;
  }
  return out;
}

}  // namespace fast_explorer::ui
