// theme-watcher.h — palette + system-theme probe for chrome rows.
//
// FastExplorer's chrome has three near-identical implementations of
// the AppsUseLightTheme registry probe (one per file that paints
// something dark-mode-aware) and two copies of the
// SetPreferredAppMode(AllowDark) bootstrap call. This header is the
// one canonical home for both, plus a small palette struct so chrome
// no longer hand-codes RGB(32, 32, 32) / RGB(40, 40, 40) etc.
//
// What's intentionally NOT here:
//   * A subscribable watcher / callback list. WM_SETTINGCHANGE is
//     received by exactly one window (the top-level shell window),
//     which already broadcasts theme refresh downward through its
//     own helpers — a publish/subscribe layer would be one consumer
//     wearing a hat.
//   * Anything FastExplorer-specific (listview colours, status-bar
//     subclass colours). Those still live in the host because they
//     mix the palette with widget-specific state.

#pragma once

#include <windows.h>

namespace fast_explorer::ui {

// Palette for chrome rows (toolbar background, address-bar Edit fill
// + border, hover pill behind the address-dropdown chevron). The
// dark values match Windows 11 Explorer's chrome; the light values
// fall through to standard system colours where one exists.
struct RowTheme {
  COLORREF background;
  COLORREF text;
  COLORREF disabledText;
  // The address-bar Edit paints itself dark via WM_CTLCOLOREDIT +
  // an NC-padding subclass that owns the border strip. Both paths
  // read these two fields.
  COLORREF editBackground;
  COLORREF editBorder;
  // Hover / pressed fill used by the address-dropdown chevron button
  // (and any future row pill controls).
  COLORREF hoverPill;
};

// Single registry probe of HKCU\...\Personalize\AppsUseLightTheme.
// Cheap (no caching needed: one open+query+close pair) and idempotent.
// Returns false when the key is missing or unreadable so the legacy
// light theme is the safe default for non-Windows-10+ hosts.
bool isAppInDarkMode() noexcept;

// True iff a WM_SETTINGCHANGE notification is the "user toggled
// theme in Settings → Personalization → Colors" broadcast. Centralises
// the lstrcmpiW(lParam, L"ImmersiveColorSet") check so callers do not
// have to know the broadcast key by string.
bool isThemeSettingChange(WPARAM wParam, LPARAM lParam) noexcept;

// One-shot per process: tells uxtheme to allow dark-themed window
// classes (DarkMode_Explorer / DarkMode_CFD / DarkMode_ItemsView) to
// take effect on subsequent SetWindowTheme calls. Must run before
// any listview / combobox / treeview is created — calling it later
// leaves the first batch of controls stuck on their default theme
// until the next WM_THEMECHANGED.
//
// Safe to call repeatedly; only the first call performs work.
void enableProcessDarkMode() noexcept;

// OS-tracking palette: returns the dark table when isAppInDarkMode()
// is true, otherwise a light table built from system colours.
RowTheme currentRowTheme() noexcept;

}  // namespace fast_explorer::ui
