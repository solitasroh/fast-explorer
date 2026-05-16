#pragma once

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

}  // namespace fast_explorer::ui
