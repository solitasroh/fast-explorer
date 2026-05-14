#include "ui/main-window.h"

#include <commctrl.h>

#include <iterator>

#include "core/process-memory.h"
#include "ui/messages.h"

namespace fast_explorer::ui {

namespace {

struct ColumnSpec {
  const wchar_t* title;
  int widthPx;
  int alignment;
};

constexpr ColumnSpec kColumns[] = {
    {L"Name", 300, LVCFMT_LEFT},
    {L"Size", 100, LVCFMT_RIGHT},
    {L"Type", 100, LVCFMT_LEFT},
    {L"Modified", 160, LVCFMT_LEFT},
};

HWND createListView(HWND parent, HINSTANCE instance) {
  return CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHAREIMAGELISTS | LVS_NOSORTHEADER,
      0, 0, 0, 0, parent, nullptr, instance, nullptr);
}

bool addColumns(HWND lv) {
  for (int i = 0; i < static_cast<int>(std::size(kColumns)); ++i) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = kColumns[i].alignment;
    col.cx = kColumns[i].widthPx;
    col.pszText = const_cast<wchar_t*>(kColumns[i].title);
    if (ListView_InsertColumn(lv, i, &col) == -1) {
      return false;
    }
  }
  return true;
}

bool registerClassOnce(HINSTANCE instance, const wchar_t* className, WNDPROC proc) {
  WNDCLASSEXW existing{};
  if (GetClassInfoExW(instance, className, &existing)) {
    return true;
  }
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // COLOR_WINDOW + 1 is the documented Win32 idiom for picking the system
  // window background brush without explicit GetSysColorBrush().
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = className;
  return RegisterClassExW(&wc) != 0;
}

}  // namespace

MainWindow::MainWindow(fast_explorer::core::ProcessMemoryService& memory) noexcept
    : memory_(memory) {}

MainWindow::~MainWindow() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    // hwnd_ is cleared in WM_NCDESTROY.
  }
}

bool MainWindow::create(HINSTANCE instance, int showCommand) {
  instance_ = instance;
  if (!registerClassOnce(instance, kClassName, &MainWindow::wndProc)) {
    return false;
  }

  HWND hwnd = CreateWindowExW(
      0, kClassName, L"Fast Explorer",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth, kDefaultHeight,
      nullptr, nullptr, instance, this);
  if (!hwnd) {
    return false;
  }

  ShowWindow(hwnd, showCommand);
  UpdateWindow(hwnd);
  return true;
}

LRESULT CALLBACK MainWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  MainWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<MainWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self) {
      self->hwnd_ = hwnd;
    }
  } else {
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self) {
    return self->handleMessage(hwnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      listView_ = createListView(hwnd, instance_);
      if (!listView_) {
        return -1;
      }
      if (!addColumns(listView_)) {
        DestroyWindow(listView_);
        listView_ = nullptr;
        return -1;
      }
      ListView_SetItemCountEx(listView_, 0, 0);
      return 0;
    case WM_DPICHANGED: {
      const auto* rect = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                   rect->right - rect->left, rect->bottom - rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SIZE:
      if (listView_) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        SetWindowPos(listView_, nullptr, 0, 0, rc.right - rc.left,
                     rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
      }
      if (wParam == SIZE_MINIMIZED) {
        memory_.notifyMinimized();
      } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
        memory_.notifyRestored();
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    case kWmFeEnumBatch:
      if (listView_) {
        ListView_SetItemCountEx(listView_, static_cast<int>(lParam),
                                LVSICF_NOSCROLL);
      }
      return 0;
    case kWmFeEnumComplete:
    case kWmFeEnumError:
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace fast_explorer::ui
