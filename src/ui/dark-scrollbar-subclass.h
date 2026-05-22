#pragma once

#include <windows.h>

namespace fast_explorer::ui {

// Installs (or refreshes) the dark-scrollbar NC paint subclass on
// `hwnd`. Idempotent: subsequent calls just update the dark/light
// state on the existing subclass refData.
//
// `dark = true` enables custom Win11-style dark scrollbar painting in
// the listview's non-client area (track + rounded thumb, with hover
// and pressed visual states). The default-themed scrollbar would
// otherwise stay light because DarkMode_ItemsView (the body theme we
// use to keep group headers bright) does not propagate to the
// scrollbar children.
//
// `dark = false` removes the subclass entirely and forces a non-client
// repaint so the system-themed scrollbar returns to its normal
// appearance. Safe to call even if the subclass was never installed.
void applyDarkScrollbarSubclass(HWND hwnd, bool dark) noexcept;

}  // namespace fast_explorer::ui
