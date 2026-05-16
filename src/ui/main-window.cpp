#include "ui/main-window.h"

#include <commctrl.h>

#include <algorithm>
#include <atomic>
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
#include "ui/extension-icon-cache.h"
#include "ui/folder-name.h"
#include "ui/format-cache.h"
#include "ui/icon-cache.h"
#include "ui/icon-provider.h"
#include "ui/messages.h"
#include "ui/pane-controller.h"
#include "ui/status-text.h"

namespace fast_explorer::ui {

namespace {

constexpr UINT_PTR kTimerFsCoalesce = 1;
constexpr UINT kFsCoalesceMs = 100;

// Set by MainWindow::onCreate so the low-memory callback (which runs
// on ProcessMemoryService's notifier thread and has no captured
// state — the C-style hook signature is void(*)()) can post back to
// the main window. Cleared in WM_NCDESTROY before the callback is
// unregistered so a fire-in-flight finds a still-valid HWND.
std::atomic<HWND> g_lowMemoryTargetHwnd{nullptr};

void postLowMemoryToMainWindow() noexcept {
  if (HWND hwnd = g_lowMemoryTargetHwnd.load(std::memory_order_acquire);
      hwnd != nullptr) {
    PostMessageW(hwnd, kWmFeLowMemory, 0, 0);
  }
}

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
  // LVS_EDITLABELS lets ListView_EditLabel pop an in-place edit; under
  // LVS_OWNERDATA the list-view does not store the edited text itself,
  // so LVN_ENDLABELEDIT must return FALSE and the model is updated
  // through the controller.
  return CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHAREIMAGELISTS | LVS_EDITLABELS,
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

void MainWindow::deleteFocusedItem() {
  if (!pane_ || listView_ == nullptr) {
    return;
  }
  // Without this guard, the VK_DELETE accelerator would also fire while
  // the address bar (or any non-list-view child) has focus, hijacking
  // the user's "erase character" key. Accelerator translation runs in
  // the message loop before the focused control sees the key, so the
  // gate has to be here.
  if (GetFocus() != listView_) {
    return;
  }
  const int focused = ListView_GetNextItem(listView_, -1, LVNI_FOCUSED);
  if (focused < 0) {
    return;
  }
  pane_->deleteItem(static_cast<std::uint32_t>(focused));
}

void MainWindow::beginRenameFocusedItem() {
  if (!pane_ || listView_ == nullptr) {
    return;
  }
  // Same accelerator-vs-focused-control rationale as deleteFocusedItem.
  if (GetFocus() != listView_) {
    return;
  }
  const int focused = ListView_GetNextItem(listView_, -1, LVNI_FOCUSED);
  if (focused < 0) {
    return;
  }
  ListView_EditLabel(listView_, focused);
}

LRESULT MainWindow::handleBeginLabelEdit() {
  // Returning FALSE permits the in-place edit to proceed. The
  // edit control is owned by the list-view and pre-filled via
  // LVN_GETDISPINFO with the current entry name.
  return FALSE;
}

void MainWindow::beginCreateSubfolder() {
  if (!pane_ || pane_->currentPath().empty()) {
    return;
  }
  const auto& store = pane_->store();
  const std::uint32_t count = store.publishedCount();
  std::vector<std::wstring_view> existing;
  existing.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& entry = store.visibleAt(i);
    existing.emplace_back(entry.namePtr, entry.nameLength);
  }
  std::wstring leaf = uniqueFolderLeaf(existing, L"New folder");
  if (!pane_->createSubfolder(leaf)) {
    return;
  }
  pendingEditFolderName_ = std::move(leaf);
}

void MainWindow::maybeStartPendingFolderEdit() {
  if (pendingEditFolderName_.empty() || !pane_ || listView_ == nullptr) {
    return;
  }
  // Swap-then-clear so a delayed second onEnumComplete cannot
  // retrigger the auto-edit even if any step below throws.
  std::wstring target;
  target.swap(pendingEditFolderName_);
  const auto& store = pane_->store();
  const std::uint32_t count = store.publishedCount();
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& entry = store.visibleAt(i);
    if (std::wstring_view(entry.namePtr, entry.nameLength) == target) {
      ListView_SetItemState(listView_, static_cast<int>(i),
                            LVIS_FOCUSED | LVIS_SELECTED,
                            LVIS_FOCUSED | LVIS_SELECTED);
      // ListView_EditLabel requires the list-view to have focus.
      SetFocus(listView_);
      ListView_EditLabel(listView_, static_cast<int>(i));
      return;
    }
  }
}

LRESULT MainWindow::handleEndLabelEdit(NMHDR* hdr) {
  if (hdr == nullptr || !pane_) {
    return FALSE;
  }
  auto* disp = reinterpret_cast<NMLVDISPINFOW*>(hdr);
  // pszText is null when the user cancels with Escape.
  if (disp->item.pszText == nullptr || disp->item.iItem < 0) {
    return FALSE;
  }
  const std::wstring newName(disp->item.pszText);
  if (newName.empty()) {
    return FALSE;
  }
  pane_->renameItem(static_cast<std::uint32_t>(disp->item.iItem), newName);
  // Always return FALSE under LVS_OWNERDATA: the list-view holds no
  // text of its own, and the visible row will refresh once the
  // watcher observes the on-disk rename and re-enumerates.
  return FALSE;
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
    // Win32 documents that a C++ exception crossing wndProc back into
    // the OS is undefined behavior, and the inner handlers do call
    // throwing operations (std::make_unique, std::wstring formatting,
    // std::vector growth, unordered_set::insert). Cap the boundary
    // here: any C++ exception that escapes the handler chain is
    // swallowed and we fall back to DefWindowProcW so the OS still
    // gets a well-defined return value. Inner try/catches in
    // handleItemChanged and reapplySelectionFromPane stay in place
    // because they restore a finer-grained invariant before letting
    // control continue. SEH (access violation, stack overflow, etc.)
    // is intentionally NOT caught here — under /EHsc catch(...) does
    // not see SEH, and swallowing a hardware fault on top of corrupt
    // state would mask far worse bugs.
    try {
      return self->handleMessage(hwnd, msg, wParam, lParam);
    } catch (...) {
      // WM_CREATE returning anything other than -1 tells the system
      // the window was constructed successfully, so a throw here
      // would otherwise leave a half-built MainWindow alive on the
      // message loop. Force CreateWindowExW to fail instead.
      if (msg == WM_CREATE) {
        return -1;
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:           return onCreate(hwnd);
    case WM_NOTIFY:           return handleListViewNotify(
                                  reinterpret_cast<NMHDR*>(lParam));
    case WM_DPICHANGED:       return onDpiChanged(hwnd, wParam, lParam);
    case WM_SIZE:             return onSize(hwnd, msg, wParam, lParam);
    case WM_COMMAND:          return onCommand(hwnd, msg, wParam, lParam);
    case kWmFeAddressCommit:  handleAddressCommit(); return 0;
    case kWmFeEnumBatch:      return onEnumBatch(wParam, lParam);
    case kWmFeEnumComplete:   return onEnumComplete(wParam);
    case kWmFeSortComplete:   return onSortComplete(wParam);
    case kWmFeIconBatch:      return onIconBatch();
    case kWmFeOperationResult: return onOperationResult();
    case kWmFeLowMemory:      return onLowMemory();
    case kWmFeEnumError:      return onEnumError(wParam, lParam);
    case kWmFeFsChange:       return onFsChange(hwnd);
    case WM_TIMER:            return onTimer(hwnd, msg, wParam, lParam);
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_NCDESTROY:
      // Tear down the low-memory hook before clearing the HWND so a
      // notifier-thread fire-in-flight either still sees a valid
      // HWND and posts (the message lands in a window pending
      // destruction — PostMessageW handles this), or sees a null
      // callback and skips entirely.
      memory_.setLowMemoryCallback(nullptr);
      g_lowMemoryTargetHwnd.store(nullptr, std::memory_order_release);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

LRESULT MainWindow::onCreate(HWND hwnd) {
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
    SetWindowSubclass(addressBar_, &MainWindow::addressBarSubclassProc, 0, 0);
  }
  pane_ = std::make_unique<PaneController>(hwnd);
  formatCache_ = std::make_unique<FormatCache>();
  iconCache_ = std::make_unique<IconCache>(GetDpiForWindow(hwnd));
  if (iconCache_->ok()) {
    // LVS_SHAREIMAGELISTS keeps ownership with us, so we destroy the
    // image list via IconCache rather than the list-view.
    ListView_SetImageList(listView_, iconCache_->handle(), LVSIL_SMALL);
  }
  extensionCache_ = std::make_unique<ExtensionIconCache>();
  iconProvider_ = std::make_unique<IconProvider>(hwnd);
  // Release-store HWND first, then release-store the callback. The
  // notifier acquire-loads the callback before invoking it; observing
  // the non-null callback synchronizes-with the prior HWND store, so
  // the free function below cannot observe a stale uninitialized
  // HWND. Reverse order on teardown (see WM_NCDESTROY).
  g_lowMemoryTargetHwnd.store(hwnd, std::memory_order_release);
  memory_.setLowMemoryCallback(&postLowMemoryToMainWindow);
  return 0;
}

LRESULT MainWindow::onDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam) {
  const auto* rect = reinterpret_cast<const RECT*>(lParam);
  SetWindowPos(hwnd, nullptr, rect->left, rect->top,
               rect->right - rect->left, rect->bottom - rect->top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  if (listView_) {
    rescaleColumnWidths(listView_, LOWORD(wParam));
  }
  return 0;
}

LRESULT MainWindow::onSize(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    const int addressH =
        addressBar_ ? scaleForDpi(28, GetDpiForWindow(hwnd)) : 0;
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
}

LRESULT MainWindow::onCommand(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
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
      case kAccelDelete:
        deleteFocusedItem();
        return 0;
      case kAccelRename:
        beginRenameFocusedItem();
        return 0;
      case kAccelCreateFolder:
        beginCreateSubfolder();
        return 0;
    }
    // Unknown accelerator id: swallow without calling DefWindowProc so
    // an unbound key does not produce a system beep.
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::onTimer(HWND hwnd, UINT msg, WPARAM wParam,
                            LPARAM lParam) {
  if (wParam == kTimerFsCoalesce) {
    KillTimer(hwnd, kTimerFsCoalesce);
    if (pane_) {
      pane_->refresh();
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::onEnumBatch(WPARAM wParam, LPARAM lParam) {
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  const auto count = static_cast<uint64_t>(lParam);
  if (!firstBatchSeen_) {
    perf_.record(fast_explorer::core::PerfTracker::EventId::PaneFirstBatch,
                 count);
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

LRESULT MainWindow::onEnumComplete(WPARAM wParam) {
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  const size_t finalCount = pane_->store().itemCount();
  const std::wstring text = readyStatusText(finalCount);
  setStatusText(text.c_str());
  maybeStartPendingFolderEdit();
  return 0;
}

LRESULT MainWindow::onEnumError(WPARAM wParam, LPARAM lParam) {
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  const auto err = static_cast<fast_explorer::core::EnumerationError>(lParam);
  const std::wstring text = errorStatusText(err);
  setStatusText(text.c_str());
  return 0;
}

LRESULT MainWindow::onSortComplete(WPARAM wParam) {
  if (!pane_ || listView_ == nullptr) {
    return 0;
  }
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  pane_->applyPendingSort(static_cast<std::uint32_t>(wParam));
  finalizeSortApply();
  return 0;
}

int MainWindow::resolveIconIndex(
    const fast_explorer::core::FileEntry& entry) {
  const int placeholder = placeholderIndexFor(entry);
  if (isDirectory(entry) || !extensionCache_ || !iconProvider_) {
    return placeholder;
  }
  const auto extView = fast_explorer::core::extensionView(entry);
  if (extView.empty()) {
    return placeholder;
  }
  const int cached = extensionCache_->lookup(extView);
  if (cached != ExtensionIconCache::kMissingIndex) {
    return cached;
  }
  // First sighting of this extension. Pin the row against the
  // placeholder slot so later repaints stop re-requesting, then
  // hand the real lookup off to the icon worker.
  extensionCache_->insert(extView, placeholder);
  iconProvider_->request(std::wstring(extView));
  return placeholder;
}

LRESULT MainWindow::onIconBatch() {
  if (iconProvider_ == nullptr) {
    return 0;
  }
  auto results = iconProvider_->drainResults();
  if (results.empty()) {
    return 0;
  }
  if (iconCache_ == nullptr || !iconCache_->ok() ||
      extensionCache_ == nullptr) {
    // Defensive: drop the icons rather than leaking the HICON
    // handles if any dependency is missing.
    for (auto& r : results) {
      if (r.icon != nullptr) {
        DestroyIcon(r.icon);
      }
    }
    return 0;
  }
  HIMAGELIST imageList = iconCache_->handle();
  for (auto& r : results) {
    if (r.icon == nullptr) {
      continue;
    }
    const int slot = ImageList_AddIcon(imageList, r.icon);
    DestroyIcon(r.icon);
    if (slot < 0) {
      continue;
    }
    // The pending entry already points at the placeholder; overwrite
    // it with the real slot. We do not yet recycle the evicted slot
    // (ImageList does not support per-slot removal in a way that
    // preserves the other entries' indices), so the cache simply
    // forgets about the displaced extension and the slot stays
    // allocated until the window is destroyed.
    extensionCache_->insert(r.extension, slot);
  }
  redrawVisibleRows();
  return 0;
}

void MainWindow::redrawVisibleRows() {
  if (listView_ == nullptr || !pane_) {
    return;
  }
  const int count = static_cast<int>(pane_->store().publishedCount());
  if (count > 0) {
    ListView_RedrawItems(listView_, 0, count - 1);
  }
}

LRESULT MainWindow::onLowMemory() {
  shrinkIconCache();
  return 0;
}

void MainWindow::shrinkIconCache() {
  if (!iconCache_ || !iconCache_->ok() || hwnd_ == nullptr) {
    return;
  }
  HIMAGELIST fresh = createPlaceholderImageList(GetDpiForWindow(hwnd_));
  if (fresh == nullptr) {
    return;
  }
  HIMAGELIST old = iconCache_->swap(fresh);
  if (listView_ != nullptr) {
    ListView_SetImageList(listView_, iconCache_->handle(), LVSIL_SMALL);
  }
  if (old != nullptr) {
    ImageList_Destroy(old);
  }
  if (extensionCache_) {
    extensionCache_->clear();
  }
  redrawVisibleRows();
}

LRESULT MainWindow::onOperationResult() {
  if (!pane_) {
    return 0;
  }
  auto results = pane_->drainShellResults();
  if (results.empty()) {
    return 0;
  }
  // Surface only the latest outcome — repeated rapid operations
  // would otherwise flicker the status bar through every step.
  const std::wstring text = opResultStatusText(results.back());
  setStatusText(text.c_str());
  return 0;
}

LRESULT MainWindow::onFsChange(HWND hwnd) {
  // Debounce: every event restarts the timer; the actual refresh fires
  // once after kFsCoalesceMs of quiet.
  SetTimer(hwnd, kTimerFsCoalesce, kFsCoalesceMs, nullptr);
  return 0;
}

void MainWindow::finalizeSortApply() {
  if (!pane_ || listView_ == nullptr) {
    return;
  }
  const auto spec = pane_->currentSortSpec();
  updateSortIndicator(listView_, sortKeyToColumnIndex(spec.key),
                      spec.direction);
  reapplySelectionFromPane();
  const int count = static_cast<int>(pane_->store().publishedCount());
  if (count > 0) {
    ListView_RedrawItems(listView_, 0, count - 1);
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
    case LVN_ITEMACTIVATE:
      handleItemActivate(hdr);
      return 0;
    case LVN_ITEMCHANGED:
      handleItemChanged(hdr);
      return 0;
    case LVN_BEGINLABELEDITW:
      return handleBeginLabelEdit();
    case LVN_ENDLABELEDITW:
      return handleEndLabelEdit(hdr);
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
  if (disp->item.iItem < 0) {
    return;
  }
  if ((disp->item.mask & (LVIF_TEXT | LVIF_IMAGE)) == 0) {
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
  if ((disp->item.mask & LVIF_IMAGE) != 0) {
    disp->item.iImage = resolveIconIndex(entry);
  }
  if ((disp->item.mask & LVIF_TEXT) == 0) {
    return;
  }
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

void MainWindow::handleItemActivate(NMHDR* hdr) {
  if (hdr == nullptr || !pane_) {
    return;
  }
  // NMITEMACTIVATE's first member is NMHDR by Win32 contract.
  auto* nmia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
  if (nmia->iItem < 0) {
    return;
  }
  pane_->openItem(static_cast<std::uint32_t>(nmia->iItem));
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
  // tracked separately.
  const auto dispatch = pane_->requestSort(key);
  if (dispatch == fast_explorer::ui::SortDispatch::Rejected) {
    return;
  }
  if (dispatch == fast_explorer::ui::SortDispatch::AppliedSync) {
    finalizeSortApply();
  } else {
    // Background path: paint the arrow eagerly so the click feels
    // responsive; the final selection-aware repaint runs in
    // onSortComplete via finalizeSortApply().
    const auto spec = pane_->currentSortSpec();
    updateSortIndicator(listView_, sortKeyToColumnIndex(spec.key),
                        spec.direction);
  }
}

}  // namespace fast_explorer::ui
