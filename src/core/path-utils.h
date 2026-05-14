#pragma once

#include <windows.h>

#include <string>

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

}  // namespace fast_explorer::core
