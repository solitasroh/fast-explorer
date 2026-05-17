#pragma once

#include <climits>
#include <cstdint>
#include <string>

namespace fast_explorer::core {

// Sentinel for "use system default position/size". Matches the spirit
// of Win32 CW_USEDEFAULT: callers pass this to SetWindowPos / window
// creation to opt out of a saved value.
constexpr int kSettingsUseDefault = INT_MIN;

enum class LayoutMode : std::uint8_t { Single = 0, Dual = 1 };

struct SessionState {
  std::wstring lastPath;
  int windowX = kSettingsUseDefault;
  int windowY = kSettingsUseDefault;
  int windowWidth = kSettingsUseDefault;
  int windowHeight = kSettingsUseDefault;
  LayoutMode layoutMode = LayoutMode::Single;
  std::wstring secondPath;
};

// Resolves the canonical settings file path:
// <portable root or %LOCALAPPDATA%>\FastExplorer\settings.json.
// Returns an empty string when neither root is available (rare; the
// caller should treat that as "skip persistence").
[[nodiscard]] std::wstring defaultSettingsPath();

// Reads JSON from `path` into `state`. Returns false on missing file,
// I/O error, or malformed content; `state` is left at construction
// defaults. The format is a flat object with these keys: last_path
// (string), window_x / window_y / window_w / window_h (integers),
// layout_mode (string "single"|"dual"), second_path (string). A
// settings file produced by an older build (missing layout_mode /
// second_path) loads cleanly with those fields at their defaults
// (Single, empty). An unrecognized layout_mode string is treated as
// Single rather than failing the whole load.
[[nodiscard]] bool loadSessionState(const std::wstring& path,
                                    SessionState& state);

// Writes `state` to `path` atomically: writes to a sibling temp file,
// then renames over the target via MoveFileExW with REPLACE_EXISTING.
// Creates parent directories on demand. Returns false on I/O error.
[[nodiscard]] bool saveSessionState(const std::wstring& path,
                                    const SessionState& state);

}  // namespace fast_explorer::core
