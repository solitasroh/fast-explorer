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
#include "../../resources/resource-ids.h"
#include "ui/clipboard-ops.h"
#include "ui/com-raii.h"
#include "ui/drop-source.h"
#include "ui/drop-target.h"
#include "ui/selection-sync.h"
#include "ui/shell-bind.h"
#include "ui/shell-context-menu.h"
#include "ui/status-text.h"

namespace fast_explorer::ui {

namespace {

constexpr std::size_t kMaxPanes = 2;
// One timer id per pane so dual-mode debounce windows do not collide.
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
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = className;
  // App icon: large for Alt+Tab / taskbar, small for window caption.
  // MAKEINTRESOURCEW(IDI_APP) = IDI_APP in resources/resource-ids.h.
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
  wc.hIconSm = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
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

void MainWindow::enterDualMode(const std::wstring& secondPath,
                               LayoutOrientation orientation) {
  if (hwnd_ == nullptr || !paneManager_) {
    return;
  }
  if (paneManager_->isDual()) {
    return;
  }
  orientation_ = orientation;
  // Capture before openSecond mutates active-pane state.
  const std::wstring fallback =
      paneManager_->active().currentPath();
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
  if (paneManager_) {
    auto* dt = new (std::nothrow) PaneDropTarget(second, paneManager_.get(), 1);
    if (dt) {
      if (SUCCEEDED(RegisterDragDrop(second, dt))) {
        dropTargets_[1] = dt;
      } else {
        dt->Release();
      }
    }
  }
  addressBars_[1] = createAddressBar(hwnd_, instance_);
  if (addressBars_[1]) {
    HWND innerEdit = reinterpret_cast<HWND>(
        SendMessageW(addressBars_[1], CBEM_GETEDITCONTROL, 0, 0));
    if (innerEdit != nullptr) {
      SetWindowSubclass(innerEdit, &MainWindow::addressBarSubclassProc, 0,
                        static_cast<DWORD_PTR>(1));
    }
  }
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
  if (!openIn.empty() && paneManager_->at(1).openFolder(openIn)) {
    clearListViewForNavigation(1);
  }
  // openFolder posts WM_FE_* asynchronously; the address bar text is
  // a pure synchronous read of currentPath() and would otherwise stay
  // blank until the first navigation event.
  syncAddressBar(1);
  applyActivePaneAppearance();
  relayout();
}

void MainWindow::enterSingleMode() {
  if (hwnd_ == nullptr || !paneManager_ || !paneManager_->isDual()) {
    return;
  }
  // Hide the popup before any pane-1 HWNDs go away — its mouse hook
  // and pending pick payloads anchor on those windows.
  if (addressBarPopup_) {
    addressBarPopup_->hide();
  }
  // Release per-pane coordinators first so the second pane's worker
  // threads (icon STA, shell STA) join before we tear down the
  // PaneController they reference.
  labelEdits_[1].reset();
  selectionSyncs_[1].reset();
  iconCoords_[1].reset();
  paneManager_->closeSecond();
  pane_ = &paneManager_->active();
  if (dropTargets_[1] != nullptr && listViews_[1] != nullptr) {
    RevokeDragDrop(listViews_[1]);
    dropTargets_[1]->Release();
    dropTargets_[1] = nullptr;
  }
  if (listViews_[1] != nullptr) {
    DestroyWindow(listViews_[1]);
    listViews_[1] = nullptr;
  }
  if (addressBars_[1] != nullptr) {
    DestroyWindow(addressBars_[1]);
    addressBars_[1] = nullptr;
  }
  applyActivePaneAppearance();
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
  if (addressBarPopup_) {
    addressBarPopup_->hide();
    addressBarPopup_->setActivePane(idx);
  }
  if (statusBar_) {
    const wchar_t* label =
        idx == 0 ? L"활성: 왼쪽" : L"활성: 오른쪽";
    SendMessageW(statusBar_, SB_SETTEXTW, 0,
                 reinterpret_cast<LPARAM>(label));
  }
  applyActivePaneAppearance();
  syncAddressBar(idx);
}

void MainWindow::setLayoutOrientation(LayoutOrientation orientation) {
  if (orientation_ == orientation) {
    return;
  }
  orientation_ = orientation;
  if (paneManager_ && paneManager_->isDual()) {
    relayout();
  }
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
  // Even in single mode we adopt the persisted orientation so the
  // next Alt+V / Alt+H press lands in the user's last-used seam
  // (and so a saved file with layout_mode=single + orientation=
  // horizontal is meaningful — pressing Alt+V from there enters
  // dual in horizontal, which is the natural "last-mode wins" UX).
  orientation_ = state.orientation;
  if (state.layoutMode == fast_explorer::core::LayoutMode::Dual) {
    enterDualMode(state.secondPath, state.orientation);
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
  const std::size_t activeIdx =
      paneManager_ ? paneManager_->activeIndex() : 0;
  if (activeIdx < firstBatchSeen_.size()) {
    firstBatchSeen_[activeIdx] = false;
  }
  if (!pane_->openFolder(path)) {
    return false;
  }
  clearListViewForNavigation(activeIdx);
  syncAddressBar(paneManager_ ? paneManager_->activeIndex() : 0);
  const std::wstring text = loadingStatusText(path);
  setStatusText(text.c_str());
  return true;
}

void MainWindow::syncAddressBar(std::size_t paneIdx) {
  if (!paneManager_ || paneIdx >= paneManager_->count() ||
      paneIdx >= addressBars_.size() ||
      addressBars_[paneIdx] == nullptr) {
    return;
  }
  const std::wstring& path = paneManager_->at(paneIdx).currentPath();
  SetWindowTextW(addressBars_[paneIdx], path.c_str());
  // Popup is shared; only mirror the active pane to avoid stealing
  // the dropdown highlight during an inactive-pane navigation.
  if (addressBarPopup_ && paneManager_->activeIndex() == paneIdx) {
    addressBarPopup_->reflectCurrentPath(path);
  }
}

void MainWindow::clearListViewForNavigation(std::size_t paneIdx) noexcept {
  if (paneIdx >= listViews_.size() || listViews_[paneIdx] == nullptr) {
    return;
  }
  HWND lv = listViews_[paneIdx];
  // LVS_OWNERDATA keeps LVIS_SELECTED/LVIS_FOCUSED bits per-index
  // internally; SetItemCountEx(0) alone does NOT clear them. Without
  // this broadcast, the new folder's rows inherit the old folder's
  // selection bits at their old indices.
  ListView_SetItemState(lv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  ListView_SetItemCountEx(lv, 0, 0);
  InvalidateRect(lv, nullptr, TRUE);
}

void MainWindow::applyActivePaneAppearance() noexcept {
  if (!paneManager_) return;
  const std::size_t active = paneManager_->activeIndex();
  // Inactive pane gets the dialog face colour so the focused pane is
  // visually obvious in dual mode; single mode skips the dim since
  // there is no other pane to contrast against.
  const bool dual = paneManager_->isDual();
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    HWND lv = listViews_[i];
    if (lv == nullptr) continue;
    const COLORREF bg = (!dual || i == active)
                           ? GetSysColor(COLOR_WINDOW)
                           : GetSysColor(COLOR_BTNFACE);
    ListView_SetBkColor(lv, bg);
    ListView_SetTextBkColor(lv, bg);
    InvalidateRect(lv, nullptr, TRUE);
  }
}

bool MainWindow::paneIndexFromListView(HWND lv,
                                       std::size_t& outIdx) const noexcept {
  if (lv == nullptr) return false;
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    if (listViews_[i] != nullptr && listViews_[i] == lv) {
      outIdx = i;
      return true;
    }
  }
  return false;
}

bool MainWindow::addressBarPaneIndex(HWND ctrl,
                                     std::size_t& outIdx) const noexcept {
  if (ctrl == nullptr) return false;
  for (std::size_t i = 0; i < addressBars_.size(); ++i) {
    HWND bar = addressBars_[i];
    if (bar == nullptr) continue;
    if (bar == ctrl) {
      outIdx = i;
      return true;
    }
    HWND innerEdit = reinterpret_cast<HWND>(
        SendMessageW(bar, CBEM_GETEDITCONTROL, 0, 0));
    if (innerEdit != nullptr && innerEdit == ctrl) {
      outIdx = i;
      return true;
    }
  }
  return false;
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
    UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, &MainWindow::addressBarSubclassProc, 0);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_GETDLGCODE && wParam == VK_RETURN) {
    return DLGC_WANTMESSAGE;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) {
      SendMessageW(root, kWmFeAddressCommit,
                   static_cast<WPARAM>(dwRefData), 0);
    }
    return 0;
  }
  if (msg == WM_CHAR && wParam == VK_RETURN) {
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MainWindow::deleteFocusedItem() {
  if (!paneManager_) return;
  // VK_DELETE accelerator fires globally; route it only when a
  // list-view actually owns focus so editing in the address bar or
  // an in-place label edit is not hijacked.
  HWND focusedCtrl = GetFocus();
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(focusedCtrl, paneIdx) ||
      paneIdx >= paneManager_->count()) {
    return;
  }
  const int focused =
      ListView_GetNextItem(focusedCtrl, -1, LVNI_FOCUSED);
  if (focused < 0) {
    return;
  }
  paneManager_->at(paneIdx).deleteItem(static_cast<std::uint32_t>(focused));
}

void MainWindow::handleAddressCommit(std::size_t paneIdx) {
  if (paneIdx >= addressBars_.size() ||
      addressBars_[paneIdx] == nullptr || !paneManager_ ||
      paneIdx >= paneManager_->count()) {
    return;
  }
  HWND bar = addressBars_[paneIdx];
  const int len = GetWindowTextLengthW(bar);
  std::wstring text(static_cast<size_t>(len), L'\0');
  if (len > 0) {
    GetWindowTextW(bar, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
  }
  if (text.empty()) {
    return;
  }
  // Inactive-pane commit must navigate the source pane, not the
  // active one, so flip activeness to the edited pane first.
  setActivePane(paneIdx);
  if (paneManager_->at(paneIdx).openFolder(text)) {
    clearListViewForNavigation(paneIdx);
    syncAddressBar(paneIdx);
  }
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
    // C++ exception escaping wndProc is UB; cap it here so the OS
    // still gets a well-defined LRESULT. SEH not caught (under /EHsc
    // catch(...) does not see hardware faults).
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
    case kWmFeAddressCommit:
      handleAddressCommit(static_cast<std::size_t>(wParam));
      return 0;
    case kWmFeAddressPopupPick: {
      auto* payload = reinterpret_cast<std::wstring*>(wParam);
      if (payload && paneManager_) {
        std::wstring path = std::move(*payload);
        delete payload;
        const std::size_t idx = static_cast<std::size_t>(lParam);
        if (idx < paneManager_->count()) {
          setActivePane(idx);
          if (paneManager_->at(idx).openFolder(path)) {
            clearListViewForNavigation(idx);
            syncAddressBar(idx);
          }
        }
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
      if (paneManager_) {
        capturedState_->lastPath = paneManager_->active().currentPath();
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
      capturedState_->orientation = orientation_;
      if (addressBarPopup_) {
        addressBarPopup_->hide();
        addressBarPopup_.reset();
      }
      for (std::size_t i = 0; i < dropTargets_.size(); ++i) {
        if (dropTargets_[i] != nullptr && listViews_[i] != nullptr) {
          RevokeDragDrop(listViews_[i]);
          dropTargets_[i]->Release();
          dropTargets_[i] = nullptr;
        }
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
  // dropTargets_[0] is registered after paneManager_ exists; see below.
  addressBars_[0] = createAddressBar(hwnd, instance_);
  if (addressBars_[0]) {
    HWND innerEdit = reinterpret_cast<HWND>(
        SendMessageW(addressBars_[0], CBEM_GETEDITCONTROL, 0, 0));
    if (innerEdit != nullptr) {
      SetWindowSubclass(innerEdit, &MainWindow::addressBarSubclassProc, 0,
                        static_cast<DWORD_PTR>(0));
    }
    addressBarPopup_ = std::make_unique<AddressBarPopup>(hwnd);
  }
  paneManager_ = std::make_unique<PaneManager>();
  paneManager_->openInitial(hwnd);
  pane_ = &paneManager_->active();
  {
    auto* dt =
        new (std::nothrow) PaneDropTarget(listView_, paneManager_.get(), 0);
    if (dt) {
      if (SUCCEEDED(RegisterDragDrop(listView_, dt))) {
        dropTargets_[0] = dt;
      } else {
        dt->Release();
      }
    }
  }
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
  for (HWND lv : listViews_) {
    if (lv != nullptr) {
      rescaleColumnWidths(lv, LOWORD(wParam));
    }
  }
  return 0;
}

LRESULT MainWindow::onSize(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (statusBar_) {
    SendMessageW(statusBar_, WM_SIZE, 0, 0);
  }
  if (hwnd_ != nullptr) {
    RECT client;
    GetClientRect(hwnd, &client);
    int statusH = 0;
    if (statusBar_) {
      RECT sb;
      GetWindowRect(statusBar_, &sb);
      statusH = sb.bottom - sb.top;
    }
    const int addressH =
        addressBars_[0] ? scaleForDpi(28, GetDpiForWindow(hwnd)) : 0;
    const int clientW = client.right - client.left;
    const int clientH = client.bottom - client.top;
    // Each pane carries its own address bar; sliced out of each
    // pane rect below, so computePaneRects gets zero global top.
    const std::size_t paneCount = paneManager_ ? paneManager_->count() : 1;
    const auto rects = computePaneRects(clientW, clientH, 0, statusH,
                                        paneCount, orientation_);
    for (std::size_t i = 0; i < listViews_.size(); ++i) {
      if (listViews_[i] == nullptr) continue;
      const RECT& r = rects.panes[i];
      const int w = r.right - r.left;
      const int h = r.bottom - r.top;
      if (w <= 0 || h <= 0) {
        if (addressBars_[i]) ShowWindow(addressBars_[i], SW_HIDE);
        ShowWindow(listViews_[i], SW_HIDE);
        continue;
      }
      const int barH = (addressBars_[i] != nullptr) ? addressH : 0;
      if (addressBars_[i]) {
        SetWindowPos(addressBars_[i], nullptr, r.left, r.top, w, barH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(addressBars_[i], SW_SHOW);
      }
      ShowWindow(listViews_[i], SW_SHOW);
      SetWindowPos(listViews_[i], nullptr, r.left, r.top + barH, w, h - barH,
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
  std::size_t srcPaneIdx = 0;
  const bool fromAddressBar = addressBarPaneIndex(srcCtrl, srcPaneIdx);
  if (fromAddressBar) {
    const WORD notif = HIWORD(wParam);
    if (notif == CBN_DROPDOWN) {
      HWND bar = addressBars_[srcPaneIdx];
      SendMessageW(bar, CB_SHOWDROPDOWN, FALSE, 0);
      if (addressBarPopup_ && paneManager_ &&
          srcPaneIdx < paneManager_->count()) {
        addressBarPopup_->setActivePane(srcPaneIdx);
        addressBarPopup_->showFor(
            bar, paneManager_->at(srcPaneIdx).currentPath());
      }
      return 0;
    }
  }
  if (HIWORD(wParam) == 1) {
    const std::size_t activeIdx =
        paneManager_ ? paneManager_->activeIndex() : 0;
    switch (LOWORD(wParam)) {
      case kAccelFocusAddress:
        if (activeIdx < addressBars_.size() && addressBars_[activeIdx]) {
          HWND bar = addressBars_[activeIdx];
          SetFocus(bar);
          SendMessageW(bar, EM_SETSEL, 0, -1);
        }
        return 0;
      case kAccelNavBack:
        if (pane_ && pane_->back()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        return 0;
      case kAccelNavForward:
        if (pane_ && pane_->forward()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        return 0;
      case kAccelNavUp:
        if (pane_ && pane_->up()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        return 0;
      case kAccelRefresh:
        if (pane_ && pane_->refresh()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
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
      case kAccelLayoutVerticalToggle:
      case kAccelLayoutHorizontalToggle: {
        const LayoutOrientation pressed =
            LOWORD(wParam) == kAccelLayoutHorizontalToggle
                ? LayoutOrientation::Horizontal
                : LayoutOrientation::Vertical;
        const bool dual = paneManager_ && paneManager_->isDual();
        const auto t = resolveLayoutToggle(dual, orientation_, pressed);
        switch (t.action) {
          case LayoutAction::EnterDual:
            enterDualMode({}, t.target);
            break;
          case LayoutAction::ExitToSingle:
            enterSingleMode();
            break;
          case LayoutAction::SwitchOrientation:
            setLayoutOrientation(t.target);
            break;
          // No default: every enumerator is handled. A future addition
          // to LayoutAction should surface here as a compiler warning
          // (/W4 -Wswitch) rather than silently falling through.
        }
        return 0;
      }
      case kAccelCopy:
        handleClipboardCopy(false);
        return 0;
      case kAccelCut:
        handleClipboardCopy(true);
        return 0;
      case kAccelPaste:
        handleClipboardPaste();
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
      wParam < kTimerFsCoalesceBase + kMaxPanes) {
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
  if (idx < firstBatchSeen_.size() && !firstBatchSeen_[idx]) {
    perf_.record(fast_explorer::core::PerfTracker::EventId::PaneFirstBatch,
                 count);
    fast_explorer::core::recordMemoryProbe(perf_);
    firstBatchSeen_[idx] = true;
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
  // Reapply the persisted sort spec before reading itemCount so the
  // user-visible row count + header arrow + first paint all reflect
  // the sorted permutation in one shot.
  target->reapplyPersistedSort();
  const size_t finalCount = target->store().itemCount();
  fast_explorer::core::recordMemoryProbe(perf_);
  const std::wstring text = readyStatusText(finalCount);
  setStatusText(text.c_str());
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx < listViews_.size() && listViews_[idx] != nullptr) {
    ListView_SetItemCountEx(listViews_[idx], static_cast<int>(finalCount),
                            LVSICF_NOSCROLL);
    InvalidateRect(listViews_[idx], nullptr, TRUE);
  }
  // Header arrow + dimmed-row repaint after the sort re-apply above.
  finalizeSortApply(idx);
  if (idx < selectionSyncs_.size() && selectionSyncs_[idx]) {
    selectionSyncs_[idx]->reapplyFromPane();
  }
  // Re-stamp LVIS_CUT when navigating back into the cut source folder.
  applyCutStateToListView(idx);
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
  if (idx >= kMaxPanes) return 0;
  const UINT_PTR timerId = kTimerFsCoalesceBase + idx;
  SetTimer(hwnd, timerId, kFsCoalesceMs, nullptr);
  return 0;
}

namespace {

std::vector<std::wstring> collectSelectedLeaves(HWND lv,
                                                const PaneController& pane) {
  std::vector<std::wstring> out;
  if (lv == nullptr) return out;
  const auto& store = pane.store();
  int row = -1;
  while ((row = ListView_GetNextItem(lv, row, LVNI_SELECTED)) >= 0) {
    const auto idx = static_cast<std::size_t>(row);
    if (idx >= store.publishedCount()) continue;
    const auto& entry = store.visibleAt(idx);
    out.emplace_back(entry.namePtr, entry.nameLength);
  }
  return out;
}

}  // namespace

void MainWindow::handleClipboardCopy(bool cut) {
  if (!paneManager_) return;
  const std::size_t idx = paneManager_->activeIndex();
  if (idx >= listViews_.size() || listViews_[idx] == nullptr) return;
  PaneController& pane = paneManager_->at(idx);
  auto leaves = collectSelectedLeaves(listViews_[idx], pane);
  if (leaves.empty()) return;
  if (!ClipboardOps::copy(pane.currentPath(), leaves, cut)) return;
  clearCutState();
  if (cut) {
    cutState_.mark(pane.currentPath(), std::move(leaves));
    for (std::size_t i = 0; i < listViews_.size(); ++i) {
      applyCutStateToListView(i);
    }
  }
}

void MainWindow::handleClipboardPaste() {
  if (!paneManager_) return;
  const std::size_t idx = paneManager_->activeIndex();
  if (idx >= listViews_.size() || listViews_[idx] == nullptr) return;
  PaneController& pane = paneManager_->at(idx);
  if (pane.currentPath().empty()) return;
  if (ClipboardOps::paste(pane.currentPath(), listViews_[idx]) ==
      PasteResult::Success) {
    // Move consumed the cut data object; drop the ghost.
    clearCutState();
  }
}

void MainWindow::applyCutStateToListView(std::size_t paneIdx) noexcept {
  if (paneIdx >= listViews_.size() || listViews_[paneIdx] == nullptr ||
      !paneManager_ || paneIdx >= paneManager_->count()) {
    return;
  }
  HWND lv = listViews_[paneIdx];
  ListView_SetItemState(lv, -1, 0, LVIS_CUT);
  if (cutState_.empty()) return;
  PaneController& pane = paneManager_->at(paneIdx);
  if (pane.currentPath() != cutState_.folder()) return;
  const auto& store = pane.store();
  const auto count = store.publishedCount();
  for (std::size_t row = 0; row < count; ++row) {
    const auto& entry = store.visibleAt(row);
    std::wstring leaf(entry.namePtr, entry.nameLength);
    if (cutState_.contains(leaf)) {
      ListView_SetItemState(lv, static_cast<int>(row), LVIS_CUT, LVIS_CUT);
    }
  }
}

void MainWindow::clearCutState() noexcept {
  cutState_.clear();
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    if (listViews_[i] != nullptr) {
      ListView_SetItemState(listViews_[i], -1, 0, LVIS_CUT);
    }
  }
}

void MainWindow::handleBeginDrag(NMHDR* hdr) {
  if (hdr == nullptr || !paneManager_) return;
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(hdr->hwndFrom, paneIdx) ||
      paneIdx >= paneManager_->count()) {
    return;
  }
  PaneController& pane = paneManager_->at(paneIdx);
  const std::wstring& folderPath = pane.currentPath();
  if (folderPath.empty()) return;
  HWND lv = hdr->hwndFrom;
  auto leaves = collectSelectedLeaves(lv, pane);
  if (leaves.empty()) return;

  ComPtr<IShellFolder> folder = bindFolderByPath(folderPath);
  if (!folder) return;
  std::vector<PidlOwner> childPidls;
  childPidls.reserve(leaves.size());
  std::vector<LPCITEMIDLIST> rawPidls;
  rawPidls.reserve(leaves.size());
  for (const auto& leaf : leaves) {
    LPITEMIDLIST child = nullptr;
    ULONG eaten = 0;
    SFGAOF attrs = 0;
    if (FAILED(folder->ParseDisplayName(nullptr, nullptr,
                                         const_cast<LPWSTR>(leaf.c_str()),
                                         &eaten, &child, &attrs)) ||
        child == nullptr) {
      return;
    }
    childPidls.emplace_back(child);
    rawPidls.push_back(childPidls.back().get());
  }

  ComPtr<IDataObject> dataObj;
  if (FAILED(folder->GetUIObjectOf(lv, static_cast<UINT>(rawPidls.size()),
                                    rawPidls.data(), IID_IDataObject, nullptr,
                                    reinterpret_cast<void**>(dataObj.put()))) ||
      !dataObj) {
    return;
  }

  ComPtr<IDropSource> dropSource;
  dropSource.attach(new (std::nothrow) FileDropSource());
  if (!dropSource) return;
  DWORD effect = 0;
  DoDragDrop(dataObj.get(), dropSource.get(),
             DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
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
    case LVN_ITEMCHANGED: {
      std::size_t idx = 0;
      if (paneIndexFromListView(hdr->hwndFrom, idx) &&
          idx < selectionSyncs_.size() && selectionSyncs_[idx]) {
        selectionSyncs_[idx]->handleItemChanged(hdr);
      }
      return 0;
    }
    case LVN_BEGINLABELEDITW: {
      std::size_t idx = 0;
      if (!paneIndexFromListView(hdr->hwndFrom, idx) ||
          idx >= labelEdits_.size() || !labelEdits_[idx]) {
        return FALSE;
      }
      return labelEdits_[idx]->handleBeginEdit();
    }
    case LVN_ENDLABELEDITW: {
      std::size_t idx = 0;
      if (!paneIndexFromListView(hdr->hwndFrom, idx) ||
          idx >= labelEdits_.size() || !labelEdits_[idx]) {
        return FALSE;
      }
      return labelEdits_[idx]->handleEndEdit(hdr);
    }
    case LVN_ODCACHEHINT:
    case LVN_ODSTATECHANGED:
      return 0;
    case LVN_BEGINDRAG:
    case LVN_BEGINRDRAG:
      handleBeginDrag(hdr);
      return 0;
    case NM_CUSTOMDRAW:
      return handleCustomDraw(hdr);
    case NM_RCLICK:
      handleListViewRightClick(hdr);
      return 0;
    case NM_SETFOCUS: {
      std::size_t idx = 0;
      if (paneIndexFromListView(hdr->hwndFrom, idx) &&
          paneManager_ && paneManager_->activeIndex() != idx) {
        // Click on either pane (including its empty area) reroutes
        // accelerators (Alt+Up, F5, F2, Del, Ctrl+Shift+N) to that
        // pane by flipping active.
        setActivePane(idx);
      }
      return 0;
    }
    default:
      return 0;
  }
}

void MainWindow::handleListViewRightClick(NMHDR* hdr) {
  if (hdr == nullptr || paneManager_ == nullptr) return;
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(hdr->hwndFrom, paneIdx) ||
      paneIdx >= paneManager_->count()) {
    return;
  }
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
      std::size_t paneIdx = 0;
      if (!paneManager_ ||
          !paneIndexFromListView(cd->nmcd.hdr.hwndFrom, paneIdx) ||
          paneIdx >= paneManager_->count()) {
        return CDRF_DODEFAULT;
      }
      const auto& store = paneManager_->at(paneIdx).store();
      const auto row = static_cast<std::size_t>(cd->nmcd.dwItemSpec);
      if (row >= store.publishedCount()) {
        return CDRF_DODEFAULT;
      }
      if (!shouldRenderDimmed(store.visibleAt(row))) {
        return CDRF_DODEFAULT;
      }
      // CDRF_NEWFONT honours clrText changes without a font swap.
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
  if (hdr == nullptr || !paneManager_) {
    return;
  }
  // The dispatcher routes every list-view's notifications through
  // this body, so resolve the owning pane from hwndFrom instead of
  // the active-pane cache to keep the inactive pane's rows correct.
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(hdr->hwndFrom, paneIdx) ||
      paneIdx >= paneManager_->count()) {
    return;
  }
  PaneController& sourcePane = paneManager_->at(paneIdx);
  auto* disp = reinterpret_cast<NMLVDISPINFOW*>(hdr);
  if (disp->item.iItem < 0) {
    return;
  }
  if ((disp->item.mask & (LVIF_TEXT | LVIF_IMAGE)) == 0) {
    return;
  }
  const auto& store = sourcePane.store();
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
    auto& coord = iconCoords_[paneIdx];
    disp->item.iImage = coord ? coord->resolveIconIndex(entry) : 0;
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
  if (hdr == nullptr || !paneManager_) {
    return;
  }
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(hdr->hwndFrom, paneIdx) ||
      paneIdx >= paneManager_->count()) {
    return;
  }
  auto* nmia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
  if (nmia->iItem < 0) {
    return;
  }
  // Activate must navigate the clicked pane, not the cached active
  // one; flipping activeness keeps subsequent accelerators on the
  // source pane.
  setActivePane(paneIdx);
  auto& target = paneManager_->at(paneIdx);
  const auto row = static_cast<std::uint32_t>(nmia->iItem);
  // Activating a file shells out (ShellExecuteExW) without navigating
  // the pane, so clearing the list-view / wiping the address bar
  // afterwards would discard the user's selection and current path.
  const bool willNavigate =
      row < target.store().publishedCount() &&
      fast_explorer::core::isDirectory(target.store().visibleAt(row));
  target.openItem(row);
  if (willNavigate) {
    clearListViewForNavigation(paneIdx);
    syncAddressBar(paneIdx);
  }
}

void MainWindow::handleColumnClick(NMHDR* hdr) {
  if (hdr == nullptr || !paneManager_) {
    return;
  }
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(hdr->hwndFrom, paneIdx) ||
      paneIdx >= paneManager_->count() ||
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
