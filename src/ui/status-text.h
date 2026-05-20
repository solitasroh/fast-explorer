#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/fs-backend.h"
#include "ui/shell-worker.h"

namespace fast_explorer::ui {

std::wstring loadingStatusText(const std::wstring& path);
std::wstring loadingProgressStatusText(uint64_t itemsSoFar);
std::wstring readyStatusText(size_t itemCount);
// Returns the empty string for EnumerationError::None (caller skips
// the status write) and for EnumerationError::Canceled (which fires
// every time the user types in the address bar fast enough to abort
// the prior enum — surfacing it as an error is noise, not signal).
// All other errors map to user-readable Korean text.
std::wstring errorStatusText(fast_explorer::core::EnumerationError err);

// Compact human-readable byte size: "0 B", "512 B", "1.2 KB",
// "45.2 MB", "1.5 GB", "2.3 TB". Single-decimal precision above
// 1 KB so the value stays under ~8 characters of status-bar
// real estate even for terabyte totals. No allocation up to the
// SSO threshold of std::wstring (~15 wchars on MSVC).
std::wstring humanReadableSize(std::uint64_t bytes);

// Formats the combined "total | selected" line used on each
// pane's status-bar part. When `selectedCount == 0` only the
// total is rendered ("1,234 items"); otherwise the selection
// summary follows after a separator ("1,234 items | 5 selected
// (45.2 MB)"). `selectedBytes` is rendered through
// humanReadableSize and is expected to already exclude folders
// per Explorer convention (folders sum to 0 bytes for selection
// status, summing folder content is too expensive to do inline).
std::wstring formatSelectionSummary(std::size_t totalCount,
                                    std::size_t selectedCount,
                                    std::uint64_t selectedBytes);

// Formats a shell operation outcome for the status bar.
// Examples:
//   "Moved 'foo.txt' to Recycle Bin"
//   "Failed to delete 'foo.txt'"
//   "Renamed 'before.txt' to 'after.txt'"
//   "Created folder 'NewFolder'"
std::wstring opResultStatusText(const OperationResult& result);

// Aggregate variant for batches drained from a single message tick.
// Single result → equivalent to opResultStatusText(results[0]).
// Multiple Delete results → "Moved N items to Recycle Bin"
// (or "Failed to delete N items" / mixed count when some failed).
// Multiple Rename / CreateFolder → reports the latest with a
// "(+N more)" suffix, since those are typically per-row UI actions
// rather than batched.
// Empty span returns an empty string — caller should skip the
// status write.
std::wstring opResultBatchStatusText(
    const std::vector<OperationResult>& results);

// Sentinel value Win32's SB_SETPARTS uses to mean "extend this part
// to the right edge of the status bar". Always lives at the trailing
// edge so the last part absorbs any rounded-down slack on odd-width
// windows.
inline constexpr int kStatusBarPartExtendsToEdge = -1;

// Right-edge x-coordinates fed to Win32 SB_SETPARTS. Always returns
// a single full-width part regardless of paneCount or clientWidth:
//   edges = {kStatusBarPartExtendsToEdge, 0}, count = 1.
// The status bar shows only the ACTIVE pane's info so a multi-part
// layout is never needed. Parameters are accepted for API stability
// and to avoid churn at call sites; both are ignored.
// Slot 1 is left zero (unused) in the returned layout.
struct StatusPartLayout {
  // edges is intentionally non-const because Win32 SB_SETPARTS
  // takes a non-const int* via LPARAM (legacy unannotated API).
  std::array<int, 2> edges{};
  std::size_t count{};
};

[[nodiscard]] constexpr StatusPartLayout statusBarPartLayout(
    int clientWidth, std::size_t paneCount) noexcept {
  (void)clientWidth;
  (void)paneCount;
  return {{kStatusBarPartExtendsToEdge, 0}, 1};
}

}  // namespace fast_explorer::ui
