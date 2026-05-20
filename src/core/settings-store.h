#pragma once

#include <array>
#include <climits>
#include <cstdint>
#include <string>

#include "core/layout-orientation.h"
#include "core/layout-preset.h"
#include "ui/splitter-ratios.h"

namespace fast_explorer::core {

// Sentinel for "use system default position/size". Matches the spirit
// of Win32 CW_USEDEFAULT: callers pass this to SetWindowPos / window
// creation to opt out of a saved value.
constexpr int kSettingsUseDefault = INT_MIN;

enum class LayoutMode : std::uint8_t { Single = 0, Dual = 1 };

constexpr std::size_t kMaxPanes = 4;

struct SessionState {
  std::wstring lastPath;
  std::wstring secondPath;
  int windowX = kSettingsUseDefault;
  int windowY = kSettingsUseDefault;
  int windowWidth = kSettingsUseDefault;
  int windowHeight = kSettingsUseDefault;

  // Legacy v4 fields retained for migration source. Writer no longer
  // emits layout_mode / orientation in v5; these are derived from
  // preset on read.
  LayoutMode layoutMode = LayoutMode::Single;
  LayoutOrientation orientation = LayoutOrientation::Vertical;

  // v5 fields
  std::array<std::wstring, kMaxPanes> panePaths{};
  std::size_t paneCount = 1;
  std::size_t activePane = 0;
  fast_explorer::core::LayoutPreset preset =
      fast_explorer::core::LayoutPreset::Single;
  std::array<fast_explorer::ui::SplitterRatios,
             fast_explorer::core::kLayoutPresetCount> ratiosPerPreset{};

  bool showHidden = false;
  bool showExtensions = true;
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
// layout_mode (string "single"|"dual"), second_path (string),
// orientation (string "vertical"|"horizontal"). A settings file
// produced by an older build (missing the newer keys) loads cleanly
// with those fields at their defaults (layoutMode=Single, secondPath
// empty, orientation=Vertical). Unrecognized layout_mode or
// orientation strings are treated as their defaults rather than
// failing the whole load (lenient forward-compat); a wrong-type
// value fails the load (strict against schema corruption).
[[nodiscard]] bool loadSessionState(const std::wstring& path,
                                    SessionState& state);

// Writes `state` to `path` atomically: writes to a sibling temp file,
// then renames over the target via MoveFileExW with REPLACE_EXISTING.
// Creates parent directories on demand. Returns false on I/O error.
[[nodiscard]] bool saveSessionState(const std::wstring& path,
                                    const SessionState& state);

}  // namespace fast_explorer::core
