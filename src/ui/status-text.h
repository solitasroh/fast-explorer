#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/fs-backend.h"

namespace fast_explorer::ui {

std::wstring loadingStatusText(const std::wstring& path);
std::wstring loadingProgressStatusText(uint64_t itemsSoFar);
std::wstring readyStatusText(size_t itemCount);
std::wstring errorStatusText(fast_explorer::core::EnumerationError err);

}  // namespace fast_explorer::ui
