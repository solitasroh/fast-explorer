#include "explorer/status-text.h"

#include <cwchar>

namespace fast_explorer::ui {

namespace {

// User-readable Korean message for each EnumerationError. Returns
// nullptr for errors the UI should *not* surface (None — success
// path — and Canceled — fires routinely when the user types
// rapidly in the address bar; surfacing it as "error" is noise).
const wchar_t* userErrorMessage(
    fast_explorer::core::EnumerationError e) noexcept {
  using fast_explorer::core::EnumerationError;
  switch (e) {
    case EnumerationError::None:                 return nullptr;
    case EnumerationError::Canceled:             return nullptr;
    case EnumerationError::PathNotFound:         return L"경로를 찾을 수 없습니다";
    case EnumerationError::FileNotFound:         return L"파일을 찾을 수 없습니다";
    case EnumerationError::AccessDenied:         return L"접근이 거부되었습니다";
    case EnumerationError::SharingViolation:     return L"파일이 사용 중입니다";
    case EnumerationError::NotReady:             return L"드라이브가 준비되지 않았습니다";
    case EnumerationError::DirectoryNotSupported:return L"폴더가 아닙니다";
    case EnumerationError::InvalidSyntax:        return L"경로 형식이 올바르지 않습니다";
    case EnumerationError::UncUnsupported:       return L"네트워크 경로는 지원되지 않습니다";
    case EnumerationError::Internal:             return L"내부 오류";
  }
  return L"알 수 없는 오류";
}

// "item" / "items" plural helper. English-only for now; Korean
// has no plural distinction, so the Korean call sites just use
// "개 항목" without going through here.
const wchar_t* itemsWord(unsigned long long n) noexcept {
  return n == 1 ? L"item" : L"items";
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
  const unsigned long long n = static_cast<unsigned long long>(itemsSoFar);
  swprintf_s(buf, _countof(buf), L"Loading: %llu %ls", n, itemsWord(n));
  return buf;
}

std::wstring readyStatusText(size_t itemCount) {
  wchar_t buf[64];
  const unsigned long long n = static_cast<unsigned long long>(itemCount);
  swprintf_s(buf, _countof(buf), L"%llu %ls", n, itemsWord(n));
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
  // ≤ ~76 wchars, comfortably inside the 128-wchar buffer.
  wchar_t buf[128];
  const unsigned long long tot = static_cast<unsigned long long>(totalCount);
  const unsigned long long sel = static_cast<unsigned long long>(selectedCount);
  swprintf_s(buf, _countof(buf),
             L"%llu %ls | %llu selected (%ls)",
             tot, itemsWord(tot), sel, size.c_str());
  return buf;
}

std::wstring errorStatusText(fast_explorer::core::EnumerationError err) {
  const wchar_t* msg = userErrorMessage(err);
  if (msg == nullptr) {
    // None / Canceled — caller should not write anything to the
    // status bar; an empty string lets it skip with a single
    // `if (text.empty())` guard.
    return std::wstring();
  }
  return std::wstring(msg);
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

std::wstring opResultBatchStatusText(
    const std::vector<OperationResult>& results) {
  if (results.empty()) return std::wstring();
  if (results.size() == 1) return opResultStatusText(results.front());
  // Batches in practice come from multi-select recycle-bin deletes;
  // rename and create-folder are per-row UI actions that never
  // queue more than one at a time. Treat the predominant case
  // (all-Delete) with a clean aggregate; mixed kinds fall back to
  // "latest + (+N more)".
  std::size_t deletes = 0;
  std::size_t deleteFailures = 0;
  bool allDelete = true;
  for (const auto& r : results) {
    if (r.kind != ShellCommandKind::Delete) {
      allDelete = false;
      break;
    }
    ++deletes;
    if (!r.success) ++deleteFailures;
  }
  if (allDelete) {
    wchar_t buf[64];
    const unsigned long long ok =
        static_cast<unsigned long long>(deletes - deleteFailures);
    const unsigned long long fail =
        static_cast<unsigned long long>(deleteFailures);
    if (deleteFailures == 0) {
      swprintf_s(buf, _countof(buf), L"Moved %llu %ls to Recycle Bin",
                 ok, itemsWord(ok));
    } else if (ok == 0) {
      swprintf_s(buf, _countof(buf), L"Failed to delete %llu %ls",
                 fail, itemsWord(fail));
    } else {
      swprintf_s(buf, _countof(buf),
                 L"Moved %llu %ls to Recycle Bin (%llu failed)",
                 ok, itemsWord(ok), fail);
    }
    return buf;
  }
  std::wstring out = opResultStatusText(results.back());
  wchar_t suffix[32];
  const unsigned long long extra =
      static_cast<unsigned long long>(results.size() - 1);
  swprintf_s(suffix, _countof(suffix), L" (+%llu more)", extra);
  out.append(suffix);
  return out;
}

}  // namespace fast_explorer::ui
