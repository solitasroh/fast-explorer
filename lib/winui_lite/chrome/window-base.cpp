#include "winui_lite/chrome/window-base.h"

namespace fast_explorer::ui {

bool WindowBase::registerClassOnce(HINSTANCE instance,
                                    const ClassSpec& spec) {
  WNDCLASSEXW existing{};
  if (GetClassInfoExW(instance, spec.className, &existing)) {
    return true;
  }
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = spec.style;
  wc.lpfnWndProc = &WindowBase::wndProcDispatch;
  wc.hInstance = instance;
  wc.hCursor = spec.cursor != nullptr
                   ? spec.cursor
                   : LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = spec.background;
  wc.lpszClassName = spec.className;
  wc.hIcon = spec.icon;
  wc.hIconSm = spec.iconSmall;
  return RegisterClassExW(&wc) != 0;
}

HWND WindowBase::createWindow(HINSTANCE instance,
                               const ClassSpec& classSpec,
                               const WindowSpec& windowSpec) {
  if (!registerClassOnce(instance, classSpec)) {
    return nullptr;
  }
  // `this` rides through CreateWindowExW as lpCreateParams and is
  // pulled out + stashed in GWLP_USERDATA inside wndProcDispatch's
  // WM_NCCREATE branch. From that point on every message reaches the
  // right WindowBase instance.
  HWND hwnd = CreateWindowExW(
      windowSpec.exStyle, classSpec.className, windowSpec.title,
      windowSpec.style,
      windowSpec.x, windowSpec.y, windowSpec.width, windowSpec.height,
      windowSpec.parent, windowSpec.menu, instance,
      static_cast<LPVOID>(this));
  return hwnd;
}

LRESULT CALLBACK WindowBase::wndProcDispatch(HWND hwnd, UINT msg,
                                              WPARAM wParam,
                                              LPARAM lParam) {
  WindowBase* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<WindowBase*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
    if (self != nullptr) {
      self->hwnd_ = hwnd;
    }
  } else {
    self = reinterpret_cast<WindowBase*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self == nullptr) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  // Win32 documents that a C++ exception crossing wndProc back into
  // the OS is undefined behaviour. Cap any escaping throw here so the
  // OS still gets a well-defined LRESULT. WM_CREATE returning -1
  // tells CreateWindowExW to fail (and destroy the half-built HWND);
  // for everything else fall through to DefWindowProcW so the window
  // keeps running rather than freezing. SEH / hardware faults are
  // not caught — catch(...) under /EHsc does not see them.
  LRESULT result;
  try {
    result = self->handleMessage(hwnd, msg, wParam, lParam);
  } catch (...) {
    if (msg == WM_CREATE) {
      return -1;
    }
    result = DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_NCDESTROY) {
    // Clear our `this` from GWLP_USERDATA so any further messages
    // (the OS occasionally delivers a few after WM_NCDESTROY when a
    // child is destroyed mid-loop) take the self == nullptr fast
    // path. Null out hwnd_ too so the C++ object outliving the HWND
    // sees handle() == nullptr.
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    self->hwnd_ = nullptr;
  }
  return result;
}

}  // namespace fast_explorer::ui
