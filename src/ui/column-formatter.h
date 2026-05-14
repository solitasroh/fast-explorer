#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/file-entry.h"

namespace fast_explorer::ui {

// Pure column formatters for the LVN_GETDISPINFOW callback. Returning
// std::wstring keeps unit tests simple; if the LVN_GETDISPINFO p99
// budget tightens, switch to a buffer-direct variant + LRU cache.

std::wstring formatSize(uint64_t bytes);
std::wstring formatSizeForEntry(const fast_explorer::core::FileEntry& e);

// `extension` should include its leading '.' (or be empty for files
// without one). For directories the extension is ignored.
std::wstring formatType(std::wstring_view extension, bool isDirectory);
std::wstring formatTypeForEntry(const fast_explorer::core::FileEntry& e);

// FILETIME bit layout: 100-ns intervals since 1601-01-01 UTC. Renders
// "YYYY-MM-DD HH:MM" in local time. Returns L"" if ft100ns is 0 or
// the Win32 conversion fails.
std::wstring formatModified(uint64_t ft100ns);

}  // namespace fast_explorer::ui
