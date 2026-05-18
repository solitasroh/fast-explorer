#include "ui/main-window.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"
#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "core/settings-store.h"
#include "ui/column-formatter.h"
#include "ui/dispinfo-histogram.h"
#include "ui/dpi-scale.h"
#include "ui/format-cache.h"
#include "ui/address-bar-popup.h"
#include "ui/icon-cache.h"
#include "ui/icon-cache-coordinator.h"
#include "ui/label-edit-controller.h"
#include "ui/messages.h"
#include "ui/pane-controller.h"
#include "ui/pane-layout.h"
#include "ui/pane-manager.h"
#include "ui/selection-sync.h"
#include "ui/shell-context-menu.h"
#include "ui/status-text.h"

namespace fast_explorer::ui {

namespace {

// One timer id per pane so dual-mode debounce windows do not collide.
// The actual id used is (kTimerFsCoalesceBase + paneIndex).
constexpr UINT_PTR kTimerFsCoalesceBase = 1;
constexpr UINT kFsCoalesceMs = 100;

struct ColumnSpec {
  const wchar_t* title;
  int widthPx;
  int alignment;
  fast_explorer::core::SortKey sortKey;
  bool sortable;  // false suppresses the sort dispatch on header click
};

constexpr ColumnSpec kColumns[] = {
    {L"Name", 300, LVCFMT_LEFT, fast_explorer::core::SortKey::Name, true},
    {L"Size", 100, LVCFMT_RIGHT, fast_explorer::core::SortKey::Size, true},
    {L"Type", 100, LVCFMT_LEFT, fast_explorer::core::SortKey::Type, true},
    {L"Modified", 160, LVCFMT_LEFT, fast_explorer::core::SortKey::Modified, true},
    {L"Attributes", 80, LVCFMT_LEFT, fast_explorer::core::SortKey::None, false},
};

HWND createStatusBar(HWND parent, HINSTANCE instance) {
  return CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                         WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                         parent, nullptr, instance, nullptr);
}

HWND createAddressBar(HWND parent, HINSTANCE instance) {
  return CreateWindowExW(
      0, WC_COMBOBOXEXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          CBS_DROPDOWN | CBS_AUTOHSCROLL,
      0, 0, 0, 0, parent, nullptr, instance, nullptr);
}

HWND createListView(HWND parent, HINSTANCE instance) {
  // LVS_NOSORTHEADER omitted intentionally: the header must accept
  // clicks so LVN_COLUMNCLICK reaches the controller for sort routing.
  // LVS_EDITLABELS lets ListView_EditLabel pop an in-place edit; under
  // LVS_OWNERDATA the list-view does not store the edited text itself,
  // so LVN_ENDLABELEDIT must return FALSE and the model is updated
  // through the controller.
  HWND lv = CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHAREIMAGELISTS | LVS_EDITLABELS,
      0, 0, 0, 0, parent, nullptr, instance, nullptr);
  if (lv != nullptr) {
    // FULLROWSELECT extends the selection highlight across every
    // column. DOUBLEBUFFER kills the report-mode flicker caused by
    // LVS_OWNERDATA during scroll. LABELTIP shows a tooltip when a
    // label is clipped. The Explorer theme is what actually paints
    // the row-wide hover highlight under Vista+ visual styles —
    // without it, hover state is invisible even with FULLROWSELECT.
    const DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                          LVS_EX_LABELTIP;
    ListView_SetExtendedListViewStyle(lv, exStyle);
    SetWindowTheme(lv, L"Explorer", nullptr);
  }
  return lv;
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
  if (!kColumns[col].sortable) {
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
    if (kColumns[i].sortable && kColumns[i].sortKey == key) {
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
    : memory_(memory),
      perf_(perf),
      capturedState_(std::make_unique<fast_explorer::core::SessionState>()) {}

MainWindow::~MainWindow() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    // hwnd_ is cleared in WM_NCDESTROY.
  }
}

bool MainWindow::installPaneCoordinators(std::size_t idx, HWND listView) {
  if (idx >= iconCoords_.size() || listView == nullptr ||
      !paneManager_ || idx >= paneManager_->count()) {
    return false;
  }
  PaneController& pane = paneManager_->at(idx);
  try {
    iconCoords_[idx] = std::make_unique<IconCacheCoordinator>(
        hwnd_, listView, GetDpiForWindow(hwnd_), idx);
    if (iconCoords_[idx] && iconCoords_[idx]->ok()) {
      ListView_SetImageList(listView, iconCoords_[idx]->imageListHandle(),
                            LVSIL_SMALL);
    }
    selectionSyncs_[idx] =
        std::make_unique<SelectionSync>(listView, pane);
    labelEdits_[idx] =
        std::make_unique<LabelEditController>(listView, pane);
  } catch (const std::bad_alloc&) {
    labelEdits_[idx].reset();
    selectionSyncs_[idx].reset();
    iconCoords_[idx].reset();
    return false;
  }
  return true;
}

void MainWindow::relayout() {
  if (hwnd_ == nullptr) {
    return;
  }
  // Build the same arg shape a real WM_SIZE delivers so onSize sees
  // an honest message rather than a fabricated lParam=0 PostMessage.
  RECT client{};
  GetClientRect(hwnd_, &client);
  const LPARAM lp = MAKELPARAM(client.right - client.left,
                               client.bottom - client.top);
  onSize(hwnd_, WM_SIZE, SIZE_RESTORED, lp);
}

void MainWindow::enterDualMode(const std::wstring& secondPath) {
  if (hwnd_ == nullptr || !paneManager_) {
    return;
  }
  if (paneManager_->isDual()) {
    return;
  }
  // Capture the fallback before openSecond so the read is obviously
  // independent of any PaneManager mutation.
  const std::wstring fallback =
      pane_ ? pane_->currentPath() : std::wstring();
  HWND second = createListView(hwnd_, instance_);
  if (second == nullptr) {
    return;
  }
  if (!addColumns(second, GetDpiForWindow(hwnd_))) {
    DestroyWindow(second);
    return;
  }
  ListView_SetItemCountEx(second, 0, 0);
  listViews_[1] = second;
  paneManager_->openSecond(hwnd_);
  if (!installPaneCoordinators(1, second)) {
    // Coordinator construction failed mid-flight. Roll back so we do
    // not leave a visible second list-view backed by a partially-
    // constructed coordinator chain.
    paneManager_->closeSecond();
    DestroyWindow(second);
    listViews_[1] = nullptr;
    return;
  }
  const std::wstring& openIn =
      chooseSecondPaneInitialPath(secondPath, fallback);
  if (!openIn.empty()) {
    // Best-effort: an invalid persisted path (deleted folder) leaves
    // the second pane empty rather than failing the whole dual-mode
    // entry — the user can still navigate via Ctrl+L or Alt+Up.
    paneManager_->at(1).openFolder(openIn);
  }
  relayout();
}

void MainWindow::enterSingleMode() {
  if (hwnd_ == nullptr || !paneManager_ || !paneManager_->isDual()) {
    return;
  }
  // Release per-pane coordinators first so the second pane's worker
  // threads (icon STA, shell STA) join before we tear down the
  // PaneController they reference.
  labelEdits_[1].reset();
  selectionSyncs_[1].reset();
  iconCoords_[1].reset();
  paneManager_->closeSecond();
  pane_ = &paneManager_->active();
  if (listViews_[1] != nullptr) {
    DestroyWindow(listViews_[1]);
    listViews_[1] = nullptr;
  }
  relayout();
}

void MainWindow::setActivePane(std::size_t idx) {
  if (!paneManager_ || !paneManager_->setActive(idx)) {
    return;
  }
  pane_ = &paneManager_->active();
  if (listViews_[idx] != nullptr) {
    SetFocus(listViews_[idx]);
  }
  if (statusBar_) {
    const wchar_t* label =
        idx == 0 ? L"활성: 왼쪽" : L"활성: 오른쪽";
    SendMessageW(statusBar_, SB_SETTEXTW, 0,
                 reinterpret_cast<LPARAM>(label));
  }
  syncAddressBar();
}

void MainWindow::applyInitialState(
    const fast_explorer::core::SessionState& state) {
  if (hwnd_ == nullptr) {
    return;
  }
  // All-or-nothing contract: any sentinel in the four placement
  // fields falls back to the system default. The writer always emits
  // all four together, so a partial-sentinel state can only arise
  // from a hand-edited or forward-compat settings file.
  if (state.windowX == fast_explorer::core::kSettingsUseDefault ||
      state.windowY == fast_explorer::core::kSettingsUseDefault ||
      state.windowWidth == fast_explorer::core::kSettingsUseDefault ||
      state.windowHeight == fast_explorer::core::kSettingsUseDefault) {
    return;
  }
  // Clamp to a sane minimum so a corrupted file cannot make the
  // window invisible. The values themselves are then validated by
  // SetWindowPos against the monitor work area.
  const int w = std::max(state.windowWidth, 320);
  const int h = std::max(state.windowHeight, 240);
  SetWindowPos(hwnd_, nullptr, state.windowX, state.windowY, w, h,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::restoreLayoutFromSession(
    const fast_explorer::core::SessionState& state) {
  if (state.layoutMode == fast_explorer::core::LayoutMode::Dual) {
    enterDualMode(state.secondPath);
  }
}

const fast_explorer::core::SessionState&
MainWindow::capturedSessionState() const noexcept {
  return *capturedState_;
}

bool MainWindow::openFolder(const std::wstring& path) {
  if (!pane_) {
    return false;
  }
  perf_.record(fast_explorer::core::PerfTracker::EventId::PaneOpenStart);
  fast_explorer::core::recordMemoryProbe(perf_);
  firstBatchSeen_ = false;
  if (!pane_->openFolder(path)) {
    return false;
  }
  syncAddressBar();
  const std::wstring text = loadingStatusText(path);
  setStatusText(text.c_str());
  return true;
}

void MainWindow::syncAddressBar() {
  if (!addressBar_ || !pane_) return;
  const std::wstring& path = pane_->currentPath();
  SetWindowTextW(addressBar_, path.c_str());
  if (addressBarPopup_) {
    addressBarPopup_->reflectCurrentPath(path);
  }
}

bool MainWindow::isStaleGeneration(WPARAM wParam) const {
  PaneController* target = paneForWParam(wParam);
  return target == nullptr ||
         generationFromWParam(wParam) != target->generation();
}

PaneController* MainWindow::paneForWParam(WPARAM wParam) const {
  if (!paneManager_) {
    return nullptr;
  }
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx >= paneManager_->count()) {
    return nullptr;
  }
  return &paneManager_->at(idx);
}

SelectionSync* MainWindow::activeSelectionSync() {
  if (!paneManager_) return nullptr;
  const std::size_t idx = paneManager_->activeIndex();
  return idx < selectionSyncs_.size() ? selectionSyncs_[idx].get() : nullptr;
}

LabelEditController* MainWindow::activeLabelEdit() {
  if (!paneManager_) return nullptr;
  const std::size_t idx = paneManager_->activeIndex();
  return idx < labelEdits_.size() ? labelEdits_[idx].get() : nullptr;
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
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) SendMessageW(root, kWmFeAddressCommit, 0, 0);
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
    case kWmFeAddressPopupPick: {
      auto* payload = reinterpret_cast<std::wstring*>(wParam);
      if (payload) {
        std::wstring path = std::move(*payload);
        delete payload;
        openFolder(path);
      }
      return 0;
    }
    case kWmFeEnumBatch:      return onEnumBatch(wParam, lParam);
    case kWmFeEnumComplete:   return onEnumComplete(wParam);
    case kWmFeSortComplete:   return onSortComplete(wParam);
    case kWmFeIconBatch:      return onIconBatch(wParam);
    case kWmFeOperationResult: return onOperationResult(wParam);
    case kWmFeLowMemory:      return onLowMemory();
    case kWmFeEnumError:      return onEnumError(wParam, lParam);
    case kWmFeFsChange:       return onFsChange(hwnd, wParam);
    case WM_TIMER:            return onTimer(hwnd, msg, wParam, lParam);
    case WM_DESTROY: {
      // GetWindowPlacement reports the restored rect even when the
      // window is currently minimized, so a Ctrl+Z-as-minimize-and-
      // close still records the visible position.
      WINDOWPLACEMENT wp{};
      wp.length = sizeof(wp);
      if (GetWindowPlacement(hwnd, &wp)) {
        const RECT& r = wp.rcNormalPosition;
        capturedState_->windowX = r.left;
        capturedState_->windowY = r.top;
        capturedState_->windowWidth = r.right - r.left;
        capturedState_->windowHeight = r.bottom - r.top;
      }
      if (pane_) {
        capturedState_->lastPath = pane_->currentPath();
      }
      if (paneManager_) {
        const bool dual = paneManager_->isDual();
        capturedState_->layoutMode = dual
            ? fast_explorer::core::LayoutMode::Dual
            : fast_explorer::core::LayoutMode::Single;
        capturedState_->secondPath = dual
            ? paneManager_->at(1).currentPath()
            : std::wstring();
      }
      if (addressBarPopup_) {
        addressBarPopup_->hide();
        addressBarPopup_.reset();
      }
      PostQuitMessage(0);
      return 0;
    }
    case WM_NCDESTROY:
      // Clear the registration so future low-memory events do not
      // PostMessage to a destroyed window. A notifier already
      // inside the prior callback keeps its captured HWND and the
      // PostMessage is benign (dropped if the window is gone).
      memory_.setLowMemoryCallback(nullptr);
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
  listViews_[0] = listView_;
  statusBar_ = createStatusBar(hwnd, instance_);
  addressBar_ = createAddressBar(hwnd, instance_);
  if (addressBar_) {
    HWND innerEdit = reinterpret_cast<HWND>(
        SendMessageW(addressBar_, CBEM_GETEDITCONTROL, 0, 0));
    if (innerEdit != nullptr) {
      SetWindowSubclass(innerEdit, &MainWindow::addressBarSubclassProc, 0, 0);
    }
    addressBarPopup_ = std::make_unique<AddressBarPopup>(hwnd);
  }
  paneManager_ = std::make_unique<PaneManager>();
  paneManager_->openInitial(hwnd);
  pane_ = &paneManager_->active();
  formatCache_ = std::make_unique<FormatCache>();
  if (!installPaneCoordinators(0, listView_)) {
    return -1;
  }
  dispInfoHist_ = std::make_unique<DispInfoHistogram>();
  LARGE_INTEGER qpcFreq{};
  if (QueryPerformanceFrequency(&qpcFreq)) {
    qpcFrequencyHz_ = static_cast<std::uint64_t>(qpcFreq.QuadPart);
  }
  if (iconCoords_[0] && iconCoords_[0]->ok()) {
    // LVS_SHAREIMAGELISTS keeps ownership with the coordinator,
    // not the list-view.
    ListView_SetImageList(listView_, iconCoords_[0]->imageListHandle(),
                          LVSIL_SMALL);
  }
  // Lambda captures the HWND by value; the notifier copies the
  // callback under ProcessMemoryService's mutex before invoking,
  // so an in-flight call holds its own copy of the lambda and the
  // captured HWND remains valid even after WM_NCDESTROY clears the
  // registration. PostMessage on a HWND mid-destruction is well-
  // defined (the message is dropped if the window is gone).
  memory_.setLowMemoryCallback([hwnd]() {
    PostMessageW(hwnd, kWmFeLowMemory, 0, 0);
  });
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
    const std::size_t paneCount = paneManager_ ? paneManager_->count() : 1;
    const auto rects = computePaneRects(clientW, clientH, addressH, statusH,
                                        paneCount);
    for (std::size_t i = 0; i < listViews_.size(); ++i) {
      if (listViews_[i] == nullptr) continue;
      const RECT& r = rects.panes[i];
      const int w = r.right - r.left;
      const int h = r.bottom - r.top;
      if (w <= 0 || h <= 0) {
        ShowWindow(listViews_[i], SW_HIDE);
        continue;
      }
      ShowWindow(listViews_[i], SW_SHOW);
      SetWindowPos(listViews_[i], nullptr, r.left, r.top, w, h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
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
  const HWND srcCtrl = reinterpret_cast<HWND>(lParam);
  if (srcCtrl != nullptr && srcCtrl == addressBar_ && addressBar_ != nullptr) {
    const WORD notif = HIWORD(wParam);
    if (notif == CBN_DROPDOWN) {
      SendMessageW(addressBar_, CB_SHOWDROPDOWN, FALSE, 0);
      if (addressBarPopup_) {
        addressBarPopup_->showFor(addressBar_,
                                   pane_ ? pane_->currentPath()
                                         : std::wstring());
      }
      return 0;
    }
  }
  if (HIWORD(wParam) == 1) {
    switch (LOWORD(wParam)) {
      case kAccelFocusAddress:
        if (addressBar_) {
          SetFocus(addressBar_);
          SendMessageW(addressBar_, EM_SETSEL, 0, -1);
        }
        return 0;
      case kAccelNavBack:
        if (pane_) { pane_->back(); syncAddressBar(); }
        return 0;
      case kAccelNavForward:
        if (pane_) { pane_->forward(); syncAddressBar(); }
        return 0;
      case kAccelNavUp:
        if (pane_) { pane_->up(); syncAddressBar(); }
        return 0;
      case kAccelRefresh:
        if (pane_) { pane_->refresh(); syncAddressBar(); }
        return 0;
      case kAccelDelete:
        deleteFocusedItem();
        return 0;
      case kAccelRename:
        if (auto* le = activeLabelEdit()) le->beginRenameFocused();
        return 0;
      case kAccelCreateFolder:
        if (auto* le = activeLabelEdit()) le->beginCreateSubfolder();
        return 0;
      case kAccelLayoutSingle:
        enterSingleMode();
        return 0;
      case kAccelLayoutDual:
        enterDualMode();
        return 0;
      case kAccelLayoutToggle:
        if (paneManager_ && paneManager_->isDual()) {
          enterSingleMode();
        } else {
          enterDualMode();
        }
        return 0;
      case kAccelPaneSwitch:
        if (paneManager_ && paneManager_->isDual()) {
          const std::size_t next =
              (paneManager_->activeIndex() + 1) % paneManager_->count();
          setActivePane(next);
        }
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
  if (wParam >= kTimerFsCoalesceBase &&
      wParam < kTimerFsCoalesceBase + 2) {
    const std::size_t idx = wParam - kTimerFsCoalesceBase;
    KillTimer(hwnd, wParam);
    if (paneManager_ && idx < paneManager_->count()) {
      paneManager_->at(idx).refresh();
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::onEnumBatch(WPARAM wParam, LPARAM lParam) {
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx >= listViews_.size() || listViews_[idx] == nullptr) {
    return 0;
  }
  const auto count = static_cast<uint64_t>(lParam);
  if (!firstBatchSeen_) {
    perf_.record(fast_explorer::core::PerfTracker::EventId::PaneFirstBatch,
                 count);
    fast_explorer::core::recordMemoryProbe(perf_);
    firstBatchSeen_ = true;
  }
  ListView_SetItemCountEx(listViews_[idx], static_cast<int>(count),
                          LVSICF_NOSCROLL);
  const std::wstring text = loadingProgressStatusText(count);
  setStatusText(text.c_str());
  return 0;
}

LRESULT MainWindow::onEnumComplete(WPARAM wParam) {
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  PaneController* target = paneForWParam(wParam);
  if (target == nullptr) {
    return 0;
  }
  const size_t finalCount = target->store().itemCount();
  fast_explorer::core::recordMemoryProbe(perf_);
  const std::wstring text = readyStatusText(finalCount);
  setStatusText(text.c_str());
  // Label-edit pending consumption belongs to whichever pane just
  // finished enumerating, not necessarily the currently active one
  // (createSubfolder may complete on a background pane).
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx < labelEdits_.size() && labelEdits_[idx]) {
    labelEdits_[idx]->maybeStartPendingEdit();
  }
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
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  PaneController* target = paneForWParam(wParam);
  if (target == nullptr) {
    return 0;
  }
  const std::size_t paneIdx = paneIndexFromWParam(wParam);
  target->applyPendingSort(generationFromWParam(wParam));
  finalizeSortApply(paneIdx);
  return 0;
}

LRESULT MainWindow::onIconBatch(WPARAM wParam) {
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx >= iconCoords_.size() || !iconCoords_[idx]) {
    return 0;
  }
  if (iconCoords_[idx]->onIconBatch()) {
    redrawVisibleRows(idx);
  }
  return 0;
}

void MainWindow::redrawVisibleRows(std::size_t idx) {
  if (idx >= listViews_.size() || listViews_[idx] == nullptr ||
      !paneManager_ || idx >= paneManager_->count()) {
    return;
  }
  const int count =
      static_cast<int>(paneManager_->at(idx).store().publishedCount());
  if (count > 0) {
    ListView_RedrawItems(listViews_[idx], 0, count - 1);
  }
}

LRESULT MainWindow::onLowMemory() {
  // Low-memory is a process-wide signal — broadcast the shrink to
  // every populated pane's coordinator. Redraw the panes that
  // actually shrank.
  for (std::size_t i = 0; i < iconCoords_.size(); ++i) {
    if (iconCoords_[i] && iconCoords_[i]->shrinkIconCache()) {
      redrawVisibleRows(i);
    }
  }
  return 0;
}

LRESULT MainWindow::onOperationResult(WPARAM wParam) {
  PaneController* target = paneForWParam(wParam);
  if (target == nullptr) {
    return 0;
  }
  auto results = target->drainShellResults();
  if (results.empty()) {
    return 0;
  }
  // Surface only the latest outcome — repeated rapid operations
  // would otherwise flicker the status bar through every step.
  const std::wstring text = opResultStatusText(results.back());
  setStatusText(text.c_str());
  return 0;
}

LRESULT MainWindow::onFsChange(HWND hwnd, WPARAM wParam) {
  // Debounce: every event restarts the per-pane timer; the actual
  // refresh fires once after kFsCoalesceMs of quiet. We pack the
  // pane index into the timer id so two panes' debounce windows do
  // not collide.
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx >= 2) return 0;
  const UINT_PTR timerId = kTimerFsCoalesceBase + idx;
  SetTimer(hwnd, timerId, kFsCoalesceMs, nullptr);
  return 0;
}

void MainWindow::finalizeSortApply(std::size_t paneIdx) {
  if (!paneManager_ || paneIdx >= paneManager_->count() ||
      paneIdx >= listViews_.size() || listViews_[paneIdx] == nullptr) {
    return;
  }
  PaneController& target = paneManager_->at(paneIdx);
  HWND lv = listViews_[paneIdx];
  const auto spec = target.currentSortSpec();
  updateSortIndicator(lv, sortKeyToColumnIndex(spec.key), spec.direction);
  if (paneIdx < selectionSyncs_.size() && selectionSyncs_[paneIdx]) {
    selectionSyncs_[paneIdx]->reapplyFromPane();
  }
  const int count = static_cast<int>(target.store().publishedCount());
  if (count > 0) {
    ListView_RedrawItems(lv, 0, count - 1);
  }
}

LRESULT MainWindow::handleListViewNotify(NMHDR* hdr) {
  if (hdr == nullptr) return 0;
  const bool fromListView =
      hdr->hwndFrom != nullptr &&
      std::find(listViews_.begin(), listViews_.end(), hdr->hwndFrom) !=
          listViews_.end();
  if (!fromListView) return 0;
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
      if (auto* sync = activeSelectionSync()) sync->handleItemChanged(hdr);
      return 0;
    case LVN_BEGINLABELEDITW: {
      auto* le = activeLabelEdit();
      return le ? le->handleBeginEdit() : FALSE;
    }
    case LVN_ENDLABELEDITW: {
      auto* le = activeLabelEdit();
      return le ? le->handleEndEdit(hdr) : FALSE;
    }
    case LVN_ODCACHEHINT:
    case LVN_ODSTATECHANGED:
      return 0;
    case NM_CUSTOMDRAW:
      return handleCustomDraw(hdr);
    case NM_RCLICK:
      handleListViewRightClick(hdr);
      return 0;
    default:
      return 0;
  }
}

void MainWindow::handleListViewRightClick(NMHDR* hdr) {
  if (hdr == nullptr || paneManager_ == nullptr) return;
  std::size_t paneIdx = 0;
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    if (listViews_[i] == hdr->hwndFrom) {
      paneIdx = i;
      break;
    }
  }
  if (paneIdx >= paneManager_->count()) return;
  PaneController& targetPane = paneManager_->at(paneIdx);
  const std::wstring& folderPath = targetPane.currentPath();
  if (folderPath.empty()) return;

  auto* nmia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
  POINT screenPt = nmia->ptAction;
  // ptAction is (-1, -1) for keyboard-invoked context menus (Shift+F10
  // / VK_APPS). Anchor to the focused row's rect in that case, or the
  // list-view origin when nothing is focused.
  if (screenPt.x == -1 && screenPt.y == -1) {
    const int focused = ListView_GetNextItem(hdr->hwndFrom, -1, LVNI_FOCUSED);
    RECT r{};
    if (focused >= 0 &&
        ListView_GetItemRect(hdr->hwndFrom, focused, &r, LVIR_LABEL)) {
      screenPt.x = r.left;
      screenPt.y = r.bottom;
    } else {
      screenPt.x = 0;
      screenPt.y = 0;
    }
  }
  ClientToScreen(hdr->hwndFrom, &screenPt);

  std::vector<std::wstring> leaves;
  if (nmia->iItem >= 0) {
    const auto& store = targetPane.store();
    const auto row = static_cast<std::size_t>(nmia->iItem);
    if (row < store.publishedCount()) {
      const auto& entry = store.visibleAt(row);
      leaves.emplace_back(entry.namePtr, entry.nameLength);
    }
  }
  ShellContextMenu::show(hwnd_, folderPath, leaves, screenPt);
}

LRESULT MainWindow::handleCustomDraw(NMHDR* hdr) {
  if (hdr == nullptr) {
    return CDRF_DODEFAULT;
  }
  auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);
  switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
      if (!pane_) {
        return CDRF_DODEFAULT;
      }
      const auto row = static_cast<std::size_t>(cd->nmcd.dwItemSpec);
      const auto& store = pane_->store();
      if (row >= store.publishedCount()) {
        return CDRF_DODEFAULT;
      }
      if (!shouldRenderDimmed(store.visibleAt(row))) {
        return CDRF_DODEFAULT;
      }
      // CDRF_NEWFONT is the documented Win32 return value for honouring
      // a clrText change even when no font swap is requested.
      cd->clrText = GetSysColor(COLOR_GRAYTEXT);
      return CDRF_NEWFONT;
    }
    default:
      return CDRF_DODEFAULT;
  }
}

void MainWindow::handleGetDispInfo(NMHDR* hdr) {
  // QPC bracket around the body so every early return inside
  // handleGetDispInfoBody still produces one sample.
  if (dispInfoHist_ == nullptr) {
    handleGetDispInfoBody(hdr);
    return;
  }
  LARGE_INTEGER t0{};
  QueryPerformanceCounter(&t0);
  handleGetDispInfoBody(hdr);
  LARGE_INTEGER t1{};
  QueryPerformanceCounter(&t1);
  const auto deltaTicks =
      static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
  dispInfoHist_->recordTicks(deltaTicks, qpcFrequencyHz_);
}

void MainWindow::handleGetDispInfoBody(NMHDR* hdr) {
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
    auto& activeCoord = iconCoords_[paneManager_ ?
                                    paneManager_->activeIndex() : 0];
    disp->item.iImage = activeCoord ? activeCoord->resolveIconIndex(entry) : 0;
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
    case 4:
      writeCellText(*disp, formatAttributesForEntry(entry));
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
  syncAddressBar();
}

void MainWindow::handleColumnClick(NMHDR* hdr) {
  if (hdr == nullptr || !paneManager_) {
    return;
  }
  std::size_t paneIdx = 0;
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    if (listViews_[i] == hdr->hwndFrom) {
      paneIdx = i;
      break;
    }
  }
  if (paneIdx >= paneManager_->count() ||
      listViews_[paneIdx] == nullptr) {
    return;
  }
  auto* nmlv = reinterpret_cast<NMLISTVIEW*>(hdr);
  fast_explorer::core::SortKey key;
  if (!columnIndexToSortKey(nmlv->iSubItem, key)) {
    return;
  }
  PaneController& target = paneManager_->at(paneIdx);
  const auto dispatch = target.requestSort(key);
  if (dispatch == fast_explorer::ui::SortDispatch::Rejected) {
    return;
  }
  if (dispatch == fast_explorer::ui::SortDispatch::AppliedSync) {
    finalizeSortApply(paneIdx);
  } else {
    const auto spec = target.currentSortSpec();
    updateSortIndicator(listViews_[paneIdx], sortKeyToColumnIndex(spec.key),
                        spec.direction);
  }
}

}  // namespace fast_explorer::ui
