#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/fs-backend.h"
#include "ui/shell-worker.h"

namespace fast_explorer::ui {

std::wstring loadingStatusText(const std::wstring& path);
std::wstring loadingProgressStatusText(uint64_t itemsSoFar);
std::wstring readyStatusText(size_t itemCount);
std::wstring errorStatusText(fast_explorer::core::EnumerationError err);

// Formats a shell operation outcome for the status bar.
// Examples:
//   "Moved 'foo.txt' to Recycle Bin"
//   "Failed to delete 'foo.txt'"
//   "Renamed 'before.txt' to 'after.txt'"
//   "Created folder 'NewFolder'"
std::wstring opResultStatusText(const OperationResult& result);

// Sentinel value Win32's SB_SETPARTS uses to mean "extend this part
// to the right edge of the status bar". Always lives at the trailing
// edge so the last part absorbs any rounded-down slack on odd-width
// windows.
inline constexpr int kStatusBarPartExtendsToEdge = -1;

// Right-edge x-coordinates fed to Win32 SB_SETPARTS, in the order
// pane 0 then pane 1. The returned `count` (1 or 2) is the number
// of parts the caller should pass as the wParam to SB_SETPARTS.
// Slot 1 is left zero when count == 1.
//
// Single (paneCount == 1): one full-width part — edges =
//   {kStatusBarPartExtendsToEdge, 0}, count = 1.
// Dual   (paneCount == 2): 50/50 split — edges =
//   {clientWidth/2, kStatusBarPartExtendsToEdge}, count = 2.
// Anything outside {1, 2}: single full-width fallback.
struct StatusPartLayout {
  // edges is intentionally non-const because Win32 SB_SETPARTS
  // takes a non-const int* via LPARAM (legacy unannotated API).
  std::array<int, 2> edges{};
  std::size_t count{};
};

[[nodiscard]] constexpr StatusPartLayout statusBarPartLayout(
    int clientWidth, std::size_t paneCount) noexcept {
  if (paneCount == 2 && clientWidth > 0) {
    return {{clientWidth / 2, kStatusBarPartExtendsToEdge}, 2};
  }
  return {{kStatusBarPartExtendsToEdge, 0}, 1};
}

}  // namespace fast_explorer::ui
