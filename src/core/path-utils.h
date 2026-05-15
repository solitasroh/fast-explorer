#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace fast_explorer::core {

// Path utilities shared by RingLogger, CrashHandler, and future cache/index
// directories. Kept dependency-free so a faulting CrashHandler can call into
// these without re-entering the logger or COM.

// Recursively creates `path`. Returns true if the directory now exists.
// Safe in handler context — uses only Win32 APIs and stack buffers.
bool ensureDirectoryRecursive(const wchar_t* path) noexcept;

// Resolves <portable root or %LOCALAPPDATA%>\FastExplorer\<sub> into `out`.
// `sub` should be a leaf directory name such as L"logs" or L"crashdumps".
// Returns true if the path could be built; the directory is NOT created here
// (callers may want to defer creation until they need to write).
bool resolveAppDataSubdir(const wchar_t* sub, std::wstring& out);

// Conversion between display and internal path forms.
//
// Internal form is always prefixed with \\?\ so the OS skips path
// normalization, which lets us address > MAX_PATH paths reliably.
// Display form is what the user sees in the address bar.

enum class PathConvertError {
  None = 0,
  Empty,                // input was nullptr / empty
  RelativeUnsupported,  // no drive letter, no \\?\ prefix
  UncUnsupported,       // UNC paths are not handled
  InvalidSyntax,        // contains characters that cannot appear in a path
};

// True for any UNC path, including the \\?\UNC\... DOS prefix form.
bool isUncPath(std::wstring_view path) noexcept;

// Converts a user-facing path into the internal \\?\-prefixed form.
// Returns PathConvertError::None on success and writes the canonical form
// to `out`. On error `out` is left in a valid but unspecified state.
PathConvertError toInternal(std::wstring_view displayPath, std::wstring& out);

// Strips a leading \\?\ prefix if present. Pure function; does no I/O and
// does not validate the rest of the path.
std::wstring toDisplay(std::wstring_view internalPath);

}  // namespace fast_explorer::core
