// dark-scrollbar-hook.h
//
// IAT-hook helper that patches comctl32.dll's delay-loaded reference to
// uxtheme.dll!OpenNcThemeData (ordinal 49) so that any subsequent call
// from comctl32 with classList == "ScrollBar" is rewritten to use
// "Explorer::ScrollBar" — the dark-themed variant Win11 Explorer uses
// for its own scrollbar children.
//
// Background
// ----------
// SetWindowTheme(L"DarkMode_ItemsView") on the listview themes the rows
// + group header band beautifully but leaves the scrollbar child HWND
// drawing against the default light theme. SetWindowTheme(L"DarkMode_
// Explorer") darkens the scrollbar but renders group headers in a dim
// near-unreadable navy. Theme classes are monolithic — there is no
// public API to mix one theme's body with another theme's scrollbar.
//
// The only known clean technique is to intercept comctl32's theme
// lookup for the "ScrollBar" class and substitute "Explorer::ScrollBar"
// before uxtheme resolves the theme atlas. This is the same approach
// used by Notepad++ (DarkMode/DarkMode.cpp::FixDarkScrollBar) and by
// ysc3839/win32-darkmode — both still functional on Win11 24H2 / 25H2.
//
// Constraints
// -----------
// - Must run AFTER comctl32.dll is loaded (InitCommonControlsEx is the
//   trigger in this process) but BEFORE any scrollbar HWND with theme
//   data already cached is created. Calling once between
//   InitCommonControlsEx and MainWindow::create satisfies both.
// - Idempotent: subsequent calls are no-ops (guarded by a static flag).
// - Safe when uxtheme cannot be loaded or the delay-load thunk is not
//   found: the call silently degrades and leaves scrollbars light.
namespace fast_explorer::ui {

// Patches comctl32's IAT entry for uxtheme!OpenNcThemeData. Idempotent.
// Safe to call before the main window is created; the patched function
// pointer survives for the life of the process.
void installDarkScrollBarHook() noexcept;

}  // namespace fast_explorer::ui
