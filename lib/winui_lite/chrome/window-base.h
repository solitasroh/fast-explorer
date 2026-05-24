// window-base.h — boilerplate for Win32 windows that follow the
// standard "this pointer stashed in GWLP_USERDATA at WM_NCCREATE"
// dispatch convention. Derived classes implement handleMessage; the
// base owns class registration, the static WNDPROC dispatcher, the
// `this` stash, and exception isolation.
//
// What it is:
//   * One ClassSpec POD describing the WNDCLASSEXW (icons, cursor,
//     background, style).
//   * One WindowSpec POD describing the CreateWindowExW call (title,
//     style/exStyle, geometry, parent, menu).
//   * createWindow(instance, classSpec, windowSpec) does
//     registerClassOnce + CreateWindowExW + GWLP_USERDATA wiring.
//   * Dispatcher catches `...` exceptions: WM_CREATE -> -1, others
//     -> DefWindowProcW so the OS always sees a well-defined LRESULT.
//
// What it is NOT:
//   * A widget framework. No timers, no children, no layout.
//   * Specific to top-level or child windows — both work; the caller
//     picks WS_OVERLAPPEDWINDOW or WS_CHILD via WindowSpec::style.
//   * Re-entrant safe in the sense of "create multiple windows
//     concurrently from different threads". RegisterClassExW is
//     thread-safe but the GetClassInfoExW probe inside
//     registerClassOnce is not atomic with the register — fine in
//     practice because Win32 GUI work happens on a single thread.

#pragma once

#include <windows.h>

namespace fast_explorer::ui {

class WindowBase {
 public:
  WindowBase() = default;
  virtual ~WindowBase() = default;
  WindowBase(const WindowBase&) = delete;
  WindowBase& operator=(const WindowBase&) = delete;

  HWND handle() const noexcept { return hwnd_; }

 protected:
  struct ClassSpec {
    const wchar_t* className = nullptr;     // required; unique per HINSTANCE
    UINT style = CS_HREDRAW | CS_VREDRAW;
    // Default background = COLOR_WINDOW+1. Pass a brush, or 0 when
    // the window paints its own background in WM_ERASEBKGND.
    HBRUSH background = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    HICON icon = nullptr;                    // nullptr = no large icon
    HICON iconSmall = nullptr;               // nullptr = no small icon
    // nullptr cursor falls through to LoadCursor(nullptr, IDC_ARROW).
    HCURSOR cursor = nullptr;
  };

  struct WindowSpec {
    DWORD exStyle = 0;
    DWORD style = WS_OVERLAPPEDWINDOW;
    const wchar_t* title = L"";
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = CW_USEDEFAULT;
    int height = CW_USEDEFAULT;
    HWND parent = nullptr;
    HMENU menu = nullptr;
  };

  // Creates the window and wires this->hwnd_ via WM_NCCREATE. Returns
  // the HWND on success, nullptr on RegisterClassExW or
  // CreateWindowExW failure. Safe to call only once per instance;
  // calling again with a live window leaks the old one.
  HWND createWindow(HINSTANCE instance,
                    const ClassSpec& classSpec,
                    const WindowSpec& windowSpec);

  // Dispatch one message; return the LRESULT the OS will receive.
  // The static dispatcher already isolates exceptions before calling
  // this — implementations may throw and the dispatcher will turn
  // throws into DefWindowProcW (or -1 for WM_CREATE). The dispatcher
  // also owns the WM_NCDESTROY teardown of hwnd_ + GWLP_USERDATA, so
  // derived handlers should focus on domain cleanup (timers, callbacks
  // registered with external services, etc.) and let the boilerplate
  // happen on the way back out.
  virtual LRESULT handleMessage(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam) = 0;

  // The live HWND. Set by wndProcDispatch in WM_NCCREATE, cleared in
  // WM_NCDESTROY after the derived handler returns. Protected so
  // derived classes can read it from helpers without going through
  // handle() — the access pattern shows up dozens of times in
  // realistic windows.
  HWND hwnd_ = nullptr;

 private:
  static LRESULT CALLBACK wndProcDispatch(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam);
  static bool registerClassOnce(HINSTANCE instance,
                                const ClassSpec& spec);
};

}  // namespace fast_explorer::ui
