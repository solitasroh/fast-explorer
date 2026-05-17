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

// Concatenates the active attribute markers for `e` in the fixed order
// H (hidden), S (system), R (readonly), J (reparse not symlink),
// L (reparse symlink), C (cloud placeholder / offline). Returns L"" when
// no marker applies. R is read from the raw `attributes` mask;
// J vs L is decided by isSymlink() (set only for IO_REPARSE_TAG_SYMLINK).
std::wstring formatAttributesForEntry(const fast_explorer::core::FileEntry& e);

// True when `e` should be rendered dimmed (NM_CUSTOMDRAW
// COLOR_GRAYTEXT). Currently driven by hidden / system semantics,
// matching Windows Explorer's "Hide protected operating system files"
// disabled-view rendering.
bool shouldRenderDimmed(const fast_explorer::core::FileEntry& e) noexcept;

}  // namespace fast_explorer::ui
