#include "ui/main-window.h"

#include <commctrl.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"
#include "core/process-memory.h"
#include "ui/column-formatter.h"
#include "ui/format-cache.h"
#include "ui/messages.h"
#include "ui/pane-controller.h"
#include "ui/status-text.h"

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

HWND createStatusBar(HWND parent, HINSTANCE instance) {
  return CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                         WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                         parent, nullptr, instance, nullptr);
}

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

void writeCellText(NMLVDISPINFOW& disp, const std::wstring& text) {
  if (disp.item.pszText == nullptr || disp.item.cchTextMax <= 0) {
    return;
  }
  const size_t cap = static_cast<size_t>(disp.item.cchTextMax) - 1;
  const size_t copyChars = std::min(text.size(), cap);
  if (copyChars > 0) {
    std::wmemcpy(disp.item.pszText, text.data(), copyChars);
  }
  disp.item.pszText[copyChars] = L'\0';
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

bool MainWindow::openFolder(const std::wstring& path) {
  if (!pane_) {
    return false;
  }
  if (!pane_->openFolder(path)) {
    return false;
  }
  const std::wstring text = loadingStatusText(path);
  setStatusText(text.c_str());
  return true;
}

void MainWindow::setStatusText(const wchar_t* text) {
  if (statusBar_ && text) {
    SendMessageW(statusBar_, SB_SETTEXTW, 0,
                 reinterpret_cast<LPARAM>(text));
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
      statusBar_ = createStatusBar(hwnd, instance_);
      pane_ = std::make_unique<PaneController>(hwnd);
      formatCache_ = std::make_unique<FormatCache>();
      return 0;
    case WM_NOTIFY:
      return handleListViewNotify(reinterpret_cast<NMHDR*>(lParam));
    case WM_DPICHANGED: {
      const auto* rect = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                   rect->right - rect->left, rect->bottom - rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SIZE:
      if (statusBar_) {
        SendMessageW(statusBar_, WM_SIZE, 0, 0);
      }
      if (listView_) {
        RECT client;
        GetClientRect(hwnd, &client);
        int statusH = 0;
        if (statusBar_) {
          RECT sb;
          GetWindowRect(statusBar_, &sb);
          statusH = sb.bottom - sb.top;
        }
        const int listH =
            std::max<int>(0, (client.bottom - client.top) - statusH);
        SetWindowPos(listView_, nullptr, 0, 0, client.right - client.left,
                     listH, SWP_NOZORDER | SWP_NOACTIVATE);
      }
      if (wParam == SIZE_MINIMIZED) {
        memory_.notifyMinimized();
      } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
        memory_.notifyRestored();
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    case kWmFeEnumBatch: {
      const auto count = static_cast<uint64_t>(lParam);
      if (listView_) {
        ListView_SetItemCountEx(listView_, static_cast<int>(count),
                                LVSICF_NOSCROLL);
      }
      const std::wstring text = loadingProgressStatusText(count);
      setStatusText(text.c_str());
      return 0;
    }
    case kWmFeEnumComplete: {
      const size_t finalCount = pane_ ? pane_->store().itemCount() : 0;
      const std::wstring text = readyStatusText(finalCount);
      setStatusText(text.c_str());
      return 0;
    }
    case kWmFeEnumError: {
      const auto err =
          static_cast<fast_explorer::core::EnumerationError>(lParam);
      const std::wstring text = errorStatusText(err);
      setStatusText(text.c_str());
      return 0;
    }
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

LRESULT MainWindow::handleListViewNotify(NMHDR* hdr) {
  if (hdr == nullptr) {
    return 0;
  }
  switch (hdr->code) {
    case LVN_GETDISPINFOW:
      handleGetDispInfo(hdr);
      return 0;
    case LVN_ODCACHEHINT:
    case LVN_ODSTATECHANGED:
      return 0;
    case NM_CUSTOMDRAW:
      return handleCustomDraw(hdr);
    default:
      return 0;
  }
}

LRESULT MainWindow::handleCustomDraw(NMHDR* hdr) {
  if (hdr == nullptr) {
    return CDRF_DODEFAULT;
  }
  auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
  switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
      return CDRF_DODEFAULT;
    default:
      return CDRF_DODEFAULT;
  }
}

void MainWindow::handleGetDispInfo(NMHDR* hdr) {
  if (hdr == nullptr || !pane_) {
    return;
  }
  auto* disp = reinterpret_cast<NMLVDISPINFOW*>(hdr);
  if ((disp->item.mask & LVIF_TEXT) == 0 || disp->item.iItem < 0) {
    return;
  }
  const auto& store = pane_->store();
  const size_t row = static_cast<size_t>(disp->item.iItem);
  if (row >= store.itemCount()) {
    return;
  }
  const auto& entry = store.entryAt(row);
  switch (disp->item.iSubItem) {
    case 0: {
      const auto view = fast_explorer::core::nameView(entry);
      writeCellText(*disp, std::wstring(view));
      break;
    }
    case 1:
      writeCellText(*disp, formatCache_->sizeForEntry(entry));
      break;
    case 2:
      writeCellText(*disp, formatCache_->typeForEntry(entry));
      break;
    case 3:
      writeCellText(*disp, formatCache_->modifiedAt(entry.modifiedTime100ns));
      break;
    default:
      break;
  }
}

}  // namespace fast_explorer::ui
