#include "ui/main-window.h"

#include <commctrl.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>
#include <span>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"
#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "ui/column-formatter.h"
#include "ui/dpi-scale.h"
#include "ui/format-cache.h"
#include "ui/messages.h"
#include "ui/pane-controller.h"
#include "ui/status-text.h"

namespace fast_explorer::ui {

namespace {

constexpr UINT_PTR kTimerFsCoalesce = 1;
constexpr UINT kFsCoalesceMs = 100;

// RAII scope guard for the selection-reapply reentrancy flag. Ensures
// the flag is cleared even if the LVIS_SELECTED reapply throws — a
// C++ exception escaping through wndProc is undefined behavior on
// Win32, so the catch block in reapplySelectionFromPane needs the
// guard's destructor to run before the catch handler.
class ScopedFlag {
 public:
  explicit ScopedFlag(bool& flag) noexcept : flag_(flag) { flag_ = true; }
  ~ScopedFlag() { flag_ = false; }
  ScopedFlag(const ScopedFlag&) = delete;
  ScopedFlag& operator=(const ScopedFlag&) = delete;

 private:
  bool& flag_;
};

struct ColumnSpec {
  const wchar_t* title;
  int widthPx;
  int alignment;
  fast_explorer::core::SortKey sortKey;
};

constexpr ColumnSpec kColumns[] = {
    {L"Name", 300, LVCFMT_LEFT, fast_explorer::core::SortKey::Name},
    {L"Size", 100, LVCFMT_RIGHT, fast_explorer::core::SortKey::Size},
    {L"Type", 100, LVCFMT_LEFT, fast_explorer::core::SortKey::Type},
    {L"Modified", 160, LVCFMT_LEFT, fast_explorer::core::SortKey::Modified},
};

HWND createStatusBar(HWND parent, HINSTANCE instance) {
  return CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                         WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                         parent, nullptr, instance, nullptr);
}

HWND createAddressBar(HWND parent, HINSTANCE instance) {
  return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                         0, 0, 0, 0, parent, nullptr, instance, nullptr);
}

HWND createListView(HWND parent, HINSTANCE instance) {
  // LVS_NOSORTHEADER omitted intentionally: the header must accept
  // clicks so LVN_COLUMNCLICK reaches the controller for sort routing.
  return CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHAREIMAGELISTS,
      0, 0, 0, 0, parent, nullptr, instance, nullptr);
}

bool addColumns(HWND lv, unsigned int dpi) {
  for (int i = 0; i < static_cast<int>(std::size(kColumns)); ++i) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = kColumns[i].alignment;
    col.cx = scaleForDpi(kColumns[i].widthPx, dpi);
    col.pszText = const_cast<wchar_t*>(kColumns[i].title);
    if (ListView_InsertColumn(lv, i, &col) == -1) {
      return false;
    }
  }
  return true;
}

void rescaleColumnWidths(HWND lv, unsigned int dpi) {
  for (int i = 0; i < static_cast<int>(std::size(kColumns)); ++i) {
    ListView_SetColumnWidth(lv, i, scaleForDpi(kColumns[i].widthPx, dpi));
  }
}

// kColumns is the single source of truth for column index ↔ SortKey.
// Both helpers below scan the same table so a new column or a reorder
// only needs to be applied once.
bool columnIndexToSortKey(int col, fast_explorer::core::SortKey& out) {
  if (col < 0 || col >= static_cast<int>(std::size(kColumns))) {
    return false;
  }
  out = kColumns[col].sortKey;
  return true;
}

// Clears HDF_SORTUP/HDF_SORTDOWN on every header column, then sets the
// arrow on `activeCol` according to `dir`. `activeCol < 0` clears all.
void updateSortIndicator(HWND lv, int activeCol,
                         fast_explorer::core::SortDirection dir) {
  HWND header = ListView_GetHeader(lv);
  if (header == nullptr) {
    return;
  }
  const int colCount = Header_GetItemCount(header);
  for (int i = 0; i < colCount; ++i) {
    HDITEMW hdi{};
    hdi.mask = HDI_FORMAT;
    if (!Header_GetItem(header, i, &hdi)) {
      continue;
    }
    hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
    if (i == activeCol) {
      hdi.fmt |=
          (dir == fast_explorer::core::SortDirection::Ascending) ? HDF_SORTUP
                                                                  : HDF_SORTDOWN;
    }
    // Header_SetItem failure leaves a single column's arrow stale; best
    // effort across the row is preferable to bailing the whole sweep.
    (void)Header_SetItem(header, i, &hdi);
  }
}

int sortKeyToColumnIndex(fast_explorer::core::SortKey key) {
  for (int i = 0; i < static_cast<int>(std::size(kColumns)); ++i) {
    if (kColumns[i].sortKey == key) {
      return i;
    }
  }
  return -1;
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

MainWindow::MainWindow(fast_explorer::core::ProcessMemoryService& memory,
                       fast_explorer::core::PerfTracker& perf) noexcept
    : memory_(memory), perf_(perf) {}

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
  perf_.record(fast_explorer::core::PerfTracker::EventId::PaneOpenStart);
  firstBatchSeen_ = false;
  if (!pane_->openFolder(path)) {
    return false;
  }
  if (addressBar_) {
    SetWindowTextW(addressBar_, path.c_str());
  }
  const std::wstring text = loadingStatusText(path);
  setStatusText(text.c_str());
  return true;
}

bool MainWindow::isStaleGeneration(WPARAM wParam) const {
  return !pane_ || static_cast<uint32_t>(wParam) != pane_->generation();
}

LRESULT CALLBACK MainWindow::addressBarSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/) {
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, &MainWindow::addressBarSubclassProc, 0);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_GETDLGCODE && wParam == VK_RETURN) {
    return DLGC_WANTMESSAGE;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    HWND parent = GetParent(hwnd);
    if (parent) {
      SendMessageW(parent, kWmFeAddressCommit, 0, 0);
    }
    return 0;
  }
  if (msg == WM_CHAR && wParam == VK_RETURN) {
    return 0;  // suppress the system beep on Enter
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MainWindow::handleAddressCommit() {
  if (!addressBar_) {
    return;
  }
  const int len = GetWindowTextLengthW(addressBar_);
  std::wstring text(static_cast<size_t>(len), L'\0');
  if (len > 0) {
    GetWindowTextW(addressBar_, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
  }
  if (text.empty()) {
    return;
  }
  openFolder(text);
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
      if (!addColumns(listView_, GetDpiForWindow(hwnd))) {
        DestroyWindow(listView_);
        listView_ = nullptr;
        return -1;
      }
      ListView_SetItemCountEx(listView_, 0, 0);
      statusBar_ = createStatusBar(hwnd, instance_);
      addressBar_ = createAddressBar(hwnd, instance_);
      if (addressBar_) {
        SetWindowSubclass(addressBar_, &MainWindow::addressBarSubclassProc, 0,
                          0);
      }
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
      if (listView_) {
        rescaleColumnWidths(listView_, LOWORD(wParam));
      }
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
        const int addressH = addressBar_ ? scaleForDpi(28, GetDpiForWindow(hwnd)) : 0;
        const int clientW = client.right - client.left;
        const int clientH = client.bottom - client.top;
        if (addressBar_) {
          SetWindowPos(addressBar_, nullptr, 0, 0, clientW, addressH,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        }
        const int listH = std::max<int>(0, clientH - statusH - addressH);
        SetWindowPos(listView_, nullptr, 0, addressH, clientW, listH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      if (wParam == SIZE_MINIMIZED) {
        memory_.notifyMinimized();
      } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
        memory_.notifyRestored();
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_COMMAND:
      if (HIWORD(wParam) == 1) {
        switch (LOWORD(wParam)) {
          case kAccelFocusAddress:
            if (addressBar_) {
              SetFocus(addressBar_);
              SendMessageW(addressBar_, EM_SETSEL, 0, -1);
            }
            return 0;
          case kAccelNavBack:
            if (pane_) pane_->back();
            return 0;
          case kAccelNavForward:
            if (pane_) pane_->forward();
            return 0;
          case kAccelNavUp:
            if (pane_) pane_->up();
            return 0;
          case kAccelRefresh:
            if (pane_) {
              pane_->refresh();
            }
            return 0;
        }
        return 0;  // unknown accelerator: swallow, do not Def-process
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    case kWmFeAddressCommit:
      handleAddressCommit();
      return 0;
    case kWmFeEnumBatch: {
      if (isStaleGeneration(wParam)) {
        return 0;
      }
      const auto count = static_cast<uint64_t>(lParam);
      if (!firstBatchSeen_) {
        perf_.record(
            fast_explorer::core::PerfTracker::EventId::PaneFirstBatch, count);
        firstBatchSeen_ = true;
      }
      if (listView_) {
        ListView_SetItemCountEx(listView_, static_cast<int>(count),
                                LVSICF_NOSCROLL);
      }
      const std::wstring text = loadingProgressStatusText(count);
      setStatusText(text.c_str());
      return 0;
    }
    case kWmFeEnumComplete: {
      if (isStaleGeneration(wParam)) {
        return 0;
      }
      const size_t finalCount = pane_->store().itemCount();
      const std::wstring text = readyStatusText(finalCount);
      setStatusText(text.c_str());
      return 0;
    }
    case kWmFeSortComplete: {
      if (!pane_ || listView_ == nullptr) {
        return 0;
      }
      if (isStaleGeneration(wParam)) {
        // Worker for a previous folder; pane state has moved on.
        // applyPendingSort would also detect this via pendingSortGen_
        // but the early return saves the indicator/redraw work.
        return 0;
      }
      pane_->applyPendingSort(static_cast<std::uint32_t>(wParam));
      const auto spec = pane_->currentSortSpec();
      updateSortIndicator(listView_, sortKeyToColumnIndex(spec.key),
                          spec.direction);
      reapplySelectionFromPane();
      const int count =
          static_cast<int>(pane_->store().publishedCount());
      if (count > 0) {
        ListView_RedrawItems(listView_, 0, count - 1);
      }
      return 0;
    }
    case kWmFeFsChange:
      // Debounce: every event restarts the timer; the actual refresh
      // fires once after kFsCoalesceMs of quiet.
      SetTimer(hwnd, kTimerFsCoalesce, kFsCoalesceMs, nullptr);
      return 0;
    case WM_TIMER:
      if (wParam == kTimerFsCoalesce) {
        KillTimer(hwnd, kTimerFsCoalesce);
        if (pane_) {
          pane_->refresh();
        }
        return 0;
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    case kWmFeEnumError: {
      if (isStaleGeneration(wParam)) {
        return 0;
      }
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
    case LVN_COLUMNCLICK:
      handleColumnClick(hdr);
      return 0;
    case LVN_ITEMCHANGED:
      handleItemChanged(hdr);
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
  // publishedCount() acquire-loads the worker's release-store after the
  // matching batch of push_backs, so rows below it are guaranteed to
  // observe fully-initialized FileEntry records. itemCount() is unsafe
  // here because vector::size() may be mid-modification on the worker.
  if (row >= store.publishedCount()) {
    return;
  }
  // iItem is the visible row index from the list-view; map through
  // visibleOrder so sort() reorderings flow into LVN_GETDISPINFO
  // without further plumbing. Identity until the first sort.
  const auto& entry = store.visibleAt(row);
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

void MainWindow::handleItemChanged(NMHDR* hdr) {
  if (hdr == nullptr || !pane_ || reapplyingSelection_) {
    return;
  }
  auto* nmlv = reinterpret_cast<NMLISTVIEW*>(hdr);
  if ((nmlv->uChanged & LVIF_STATE) == 0) {
    return;
  }
  if (nmlv->iItem < 0) {
    // -1 is a list-wide change (LVS_OWNERDATA broadcast); we re-derive
    // selection from the pane on the next reapplySelectionFromPane(),
    // so nothing to track here.
    return;
  }
  const auto row = static_cast<std::size_t>(nmlv->iItem);
  const auto& store = pane_->store();
  if (row >= store.publishedCount()) {
    return;
  }
  const std::span<const std::uint32_t> order = store.visibleOrder();
  if (row >= order.size()) {
    return;
  }
  const std::uint32_t raw = order[row];
  const bool wasSelected = (nmlv->uOldState & LVIS_SELECTED) != 0;
  const bool isSelected = (nmlv->uNewState & LVIS_SELECTED) != 0;
  if (wasSelected == isSelected) {
    return;
  }
  // selectRaw / deselectRaw use unordered_set under the hood and
  // therefore can throw bad_alloc; an uncaught C++ exception leaving
  // the wndProc callstack is UB on Win32, so we swallow the
  // synchronization miss — the next reapplySelectionFromPane()
  // rebuilds list-view state from the pane model.
  try {
    if (isSelected) {
      pane_->selectRaw(raw);
    } else {
      pane_->deselectRaw(raw);
    }
  } catch (const std::bad_alloc&) {
  }
}

void MainWindow::reapplySelectionFromPane() {
  if (!pane_ || listView_ == nullptr) {
    return;
  }
  ScopedFlag guard(reapplyingSelection_);
  // -1 broadcasts the state mask to every item; this clears the
  // LVIS_SELECTED bit list-wide before we re-set it on the rows the
  // pane says are still selected.
  ListView_SetItemState(listView_, -1, 0, LVIS_SELECTED);
  try {
    for (int row : pane_->selectedRowsUnderCurrentOrder()) {
      ListView_SetItemState(listView_, row, LVIS_SELECTED, LVIS_SELECTED);
    }
  } catch (const std::bad_alloc&) {
    // Best-effort: the broadcast clear above leaves the list-view in
    // a coherent "nothing selected" state, which is better than
    // letting the exception cross the wndProc boundary.
  }
}

void MainWindow::handleColumnClick(NMHDR* hdr) {
  if (hdr == nullptr || !pane_ || listView_ == nullptr) {
    return;
  }
  // NMLISTVIEW's first member is NMHDR by Win32 contract for LVN_*
  // notifications, so the reinterpret is well-defined here. The
  // dispatch in handleListViewNotify already gated on hdr->code.
  auto* nmlv = reinterpret_cast<NMLISTVIEW*>(hdr);
  fast_explorer::core::SortKey key;
  if (!columnIndexToSortKey(nmlv->iSubItem, key)) {
    return;
  }
  // requestSort guards against the enumeration worker still writing
  // entries_; the GETDISPINFO read path against in-flight appends is
  // tracked separately and addressed by the sort-worker milestone.
  const auto dispatch = pane_->requestSort(key);
  if (dispatch == fast_explorer::ui::SortDispatch::Rejected) {
    return;
  }
  // Update the arrow eagerly for both paths so the click feels
  // responsive; the background path will still get its final repaint
  // when kWmFeSortComplete commits applyPendingSort().
  const auto spec = pane_->currentSortSpec();
  updateSortIndicator(listView_, sortKeyToColumnIndex(spec.key), spec.direction);
  if (dispatch == fast_explorer::ui::SortDispatch::AppliedSync) {
    reapplySelectionFromPane();
    const int count =
        static_cast<int>(pane_->store().publishedCount());
    if (count > 0) {
      ListView_RedrawItems(listView_, 0, count - 1);
    }
  }
}

}  // namespace fast_explorer::ui
