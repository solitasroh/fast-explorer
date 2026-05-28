#include "explorer/main-window.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <winsparkle.h>

// dwmapi.lib linkage handled via CMakeLists.txt entry.
#pragma comment(lib, "dwmapi.lib")

#include <algorithm>
#include <cstring>
#include <iterator>

#include "core/file-entry.h"
#include "core/file-grouping.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"
#include "core/perf-tracker.h"
#include "core/process-memory.h"
#include "core/settings-store.h"
#include "explorer/column-formatter.h"
#include "winui_lite/chrome/dispinfo-histogram.h"
#include "winui_lite/chrome/dpi-scale.h"
#include "explorer/format-cache.h"
#include "explorer/address-bar-popup.h"
#include "winui_lite/chrome/pane-toolbar-row.h"
#include "winui_lite/chrome/theme-watcher.h"
#include "explorer/icon-cache-coordinator.h"
#include "explorer/label-edit-controller.h"
#include "explorer/listview-group-callback.h"
#include "explorer/messages.h"
#include "explorer/pane-controller.h"
#include "winui_lite/chrome/pane-layout.h"
#include "winui_lite/chrome/pane-manager.h"
#include "winui_lite/chrome/pane-splitter.h"
#include "../../resources/resource-ids.h"
#include "winui_lite/widgets/address-input.h"
#include "explorer/adapters/shell-clipboard.h"
#include "explorer/adapters/shell-context-menu-adapter.h"
#include "explorer/adapters/shell-drag-drop.h"
#include "explorer/adapters/shell-item-dispatcher.h"
#include "explorer/adapters/shell-item-source.h"
#include "explorer/drop-target.h"
#include "explorer/selection-sync.h"
#include "explorer/shell-context-menu.h"
#include "explorer/status-text.h"

namespace fast_explorer::ui {

namespace {

constexpr std::size_t kMaxPanes = 4;
static_assert(kMaxPanes == fast_explorer::core::kMaxPanes,
              "main-window kMaxPanes must match core::kMaxPanes");
// One timer id per pane so dual-mode debounce windows do not collide.
constexpr UINT_PTR kTimerFsCoalesceBase = 1;
constexpr UINT kFsCoalesceMs = 100;
// Separate timer band so a selection-storm (Ctrl+A on a 100k folder
// fires LVN_ITEMCHANGED once per row) collapses into a single
// status-bar refresh instead of N format calls. 80 ms is one
// vsync past the 60 Hz frame budget — fast enough to feel live,
// slow enough to absorb every selection burst observed so far.
constexpr UINT_PTR kTimerSelSummaryBase = kTimerFsCoalesceBase + kMaxPanes;
constexpr UINT kSelSummaryDebounceMs = 80;
// Filter debounce timer band: a Spotlight-style search box receives
// one EN_CHANGE per keystroke; 80 ms collapses the storm so the
// O(N) match pass over the pane only runs once after typing pauses.
constexpr UINT_PTR kTimerFilterDebounceBase = kTimerSelSummaryBase + kMaxPanes;
constexpr UINT kFilterDebounceMs = 80;

// FastExplorer-specific composition for the per-pane toolbar row.
// PaneToolbarRow (in winui_lite/chrome) is glyph-/label-agnostic; the
// host owns the table of buttons, their Fluent / MDL2 codepoints,
// Korean accessible names, and "label (shortcut)" tooltip strings.
// The pointers below have static lifetime so the spans handed to the
// row stay valid for as long as any PaneToolbarRow exists.
//   E72B Back, E72A Forward, E74A Up, E72C Refresh,
//   E712 Hamburger / More, E70D ChevronDown
constexpr wchar_t kGlyphBack[]       = {0xE72B, 0};
constexpr wchar_t kGlyphForward[]    = {0xE72A, 0};
constexpr wchar_t kGlyphUp[]         = {0xE74A, 0};
constexpr wchar_t kGlyphRefresh[]    = {0xE72C, 0};
constexpr wchar_t kGlyphHamburger[]  = {0xE712, 0};
constexpr wchar_t kGlyphChevronDown[] = {0xE70D, 0};

constexpr NavButtonSpec kPaneNavButtons[] = {
    {kTbBack,    kGlyphBack,    L"뒤로",      L"뒤로 (Alt+←)"},
    {kTbForward, kGlyphForward, L"앞으로",    L"앞으로 (Alt+→)"},
    {kTbUp,      kGlyphUp,      L"위로",      L"위로 (Alt+↑)"},
    {kTbRefresh, kGlyphRefresh, L"새로 고침", L"새로 고침 (F5)"},
};

constexpr AuxSlotSpec kPaneHamburgerSlot{
    /*id*/      kTbHamburger,
    /*glyph*/   kGlyphHamburger,
    /*label*/   L"메뉴",
    /*tooltip*/ L"메뉴 (Alt+M)",
};

constexpr AuxSlotSpec kPaneAddressDropdownSlot{
    /*id*/      kTbAddressDropdown,
    /*glyph*/   kGlyphChevronDown,
    /*label*/   nullptr,
    /*tooltip*/ nullptr,
};

PaneToolbarRowConfig makePaneToolbarRowConfig() noexcept {
  PaneToolbarRowConfig cfg{};
  cfg.navButtons = std::span<const NavButtonSpec>(kPaneNavButtons);
  cfg.hamburger = kPaneHamburgerSlot;
  cfg.addressDropdown = kPaneAddressDropdownSlot;
  return cfg;
}

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

HWND createAddressDropdownBtn(HWND parent, HINSTANCE instance,
                               std::size_t paneIdx) {
  // BS_OWNERDRAW so PaneToolbarRow can paint the ˅ chevron itself
  // and override the button's bg for dark mode. The HMENU param
  // packs the kTbAddressDropdown + paneIdx the same way nav-bar
  // buttons do, so WM_COMMAND lands in the existing routing.
  return CreateWindowExW(
      0, WC_BUTTONW, L"",
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
      0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(
          static_cast<INT_PTR>(packCmd(kTbAddressDropdown, paneIdx))),
      instance, nullptr);
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

// ListView subclass that catches WM_NOTIFY from the listview's own
// header child and over-paints column titles in dark mode. The header
// honours SetWindowTheme(L"DarkMode_ItemsView") for the chevron and
// sort glyphs but ignores it for text colour, so we own the text
// rendering when dark. Light mode short-circuits to DefSubclassProc.
struct HeaderDarkState {
  HBRUSH bgBrush = nullptr;
  COLORREF bgColor = 0;
  COLORREF textColor = 0;
  COLORREF sepColor = 0;
  bool valid = false;
  void refresh() noexcept {
    bgColor   = RGB(45, 45, 48);    // header strip, slightly above row bg
    textColor = RGB(225, 225, 225);
    sepColor  = RGB(70, 70, 70);
    if (bgBrush != nullptr) DeleteObject(bgBrush);
    bgBrush = CreateSolidBrush(bgColor);
    valid = true;
  }
};
LRESULT CALLBACK listViewHeaderColorSubclass(HWND, UINT, WPARAM, LPARAM,
                                              UINT_PTR, DWORD_PTR);

// Ctrl+C/X/V are registered as global accelerators (file-clipboard ops)
// so TranslateAcceleratorW would otherwise eat them before an Edit
// control (address bar, in-place rename) sees them. When focus is on
// a plain Edit, forward the equivalent standard WM_* clipboard message
// to it and report it as handled. Returns false to let the file-level
// handler run.
bool routeEditClipboardIfFocused(UINT editMsg) noexcept {
  HWND focused = GetFocus();
  if (focused == nullptr) return false;
  wchar_t cls[8] = {0};
  if (GetClassNameW(focused, cls, ARRAYSIZE(cls)) == 0) return false;
  if (_wcsicmp(cls, L"Edit") != 0) return false;
  SendMessageW(focused, editMsg, 0, 0);
  return true;
}

// Ctrl+A parity: single-line Win32 Edit does not implement select-all
// natively (only RichEdit does). When focus is on an Edit (address bar,
// inline rename, filter box), forward EM_SETSEL(0, -1) so the text is
// fully selected. Returns false so the caller can fall back to
// list-view select-all.
bool routeEditSelectAllIfFocused() noexcept {
  HWND focused = GetFocus();
  if (focused == nullptr) return false;
  wchar_t cls[8] = {0};
  if (GetClassNameW(focused, cls, ARRAYSIZE(cls)) == 0) return false;
  if (_wcsicmp(cls, L"Edit") != 0) return false;
  SendMessageW(focused, EM_SETSEL, 0, static_cast<LPARAM>(-1));
  return true;
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
  // Register the LVS_OWNERDATA group callback. Without this, comctl32
  // silently ignores iGroupId for virtual list-views and group-view
  // never engages. The callback is refcounted; comctl32 holds the ref
  // until DestroyWindow, and we additionally hold one ref in
  // groupCallbacks_[idx] so applyListViewGroups can call rebuild().
  if (groupCallbacks_[idx] == nullptr) {
    auto* cb = new (std::nothrow) ListViewGroupCallback();
    if (cb != nullptr) {
      IListView_FE* lv2 = nullptr;
      // LVM_QUERYINTERFACE returns non-zero on success and writes the
      // IListView pointer into the variable lParam addresses. The Win7+
      // comctl32 6.10 IID is mandatory — the Vista GUID
      // {2FFE2979-...} is rejected by current Windows.
      SendMessageW(listView, kLvmQueryInterface,
                   reinterpret_cast<WPARAM>(&IID_IListView_FE),
                   reinterpret_cast<LPARAM>(&lv2));
      if (lv2 != nullptr) {
        cb->AddRef();  // ref held by the listview
        lv2->SetOwnerDataCallback(static_cast<IOwnerDataCallback*>(cb));
        lv2->Release();
        groupCallbacks_[idx] = cb;  // first ref kept by MainWindow
      } else {
        cb->Release();  // toss the no-IListView case (shouldn't happen on Win7+)
      }
    }
  }
  return true;
}

void MainWindow::relayout() {
  if (hwnd_ == nullptr) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;

  const int statusH = statusBar_.height();
  applyStatusParts(clientW);

  const auto& ratios =
      ratiosPerPreset_[static_cast<std::size_t>(preset_)];
  const auto result = computePaneLayout(preset_, ratios,
                                        clientW, clientH,
                                        /*reservedTop*/ 0, statusH);

  // Position slot HWNDs. Each slot's RECT contains its toolbar row at
  // the top and the listview below.
  const int rowH = scaleForDpi(42, GetDpiForWindow(hwnd_));
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    const RECT& r = result.slots[i];
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    const bool active = i < result.slotCount;
    HWND rowHwnd = paneToolbarRows_[i] ? paneToolbarRows_[i]->handle()
                                       : addressBars_[i];

    if (!active || w <= 0 || h <= 0) {
      if (rowHwnd) ShowWindow(rowHwnd, SW_HIDE);
      if (listViews_[i]) ShowWindow(listViews_[i], SW_HIDE);
      continue;
    }
    if (rowHwnd) {
      SetWindowPos(rowHwnd, nullptr, r.left, r.top, w, rowH,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      ShowWindow(rowHwnd, SW_SHOWNA);
    }
    if (listViews_[i]) {
      SetWindowPos(listViews_[i], nullptr, r.left, r.top + rowH,
                   w, h - rowH, SWP_NOZORDER | SWP_NOACTIVATE);
      ShowWindow(listViews_[i], SW_SHOWNA);
    }
  }

  // Position splitter HWNDs; hide unused ones.
  // Axis math for cursor->ratio mapping must match computePaneLayout:
  //   * Vertical splitters use `W * ratio`  → origin=0, length=clientW.
  //   * Horizontal splitters use `top + totalH * ratio`
  //     → origin=reservedTop (=0 today), length=totalH=clientH-statusH.
  // Using full clientH for horizontal splitters would be off by statusH,
  // which made drags feel slightly miscalibrated in v0.4.0.
  const int totalH = clientH - statusH;  // reservedTop=0
  for (std::size_t i = 0; i < splitterHwnds_.size(); ++i) {
    HWND s = splitterHwnds_[i];
    if (!s) continue;
    if (i >= result.splitterCount) {
      ShowWindow(s, SW_HIDE);
      continue;
    }
    const auto& sp = result.splitters[i];
    auto* ctx = reinterpret_cast<SplitterContext*>(
        GetWindowLongPtrW(s, GWLP_USERDATA));
    if (ctx) {
      ctx->orient = sp.orient;
      ctx->ratioId = sp.ratioId;
      ctx->ratios = &ratiosPerPreset_[static_cast<std::size_t>(preset_)];
      if (sp.orient == SplitterOrientation::Vertical) {
        ctx->axisOriginInParent = 0;
        ctx->axisLengthForRatio = clientW;
        // The ghost line spans this splitter's vertical extent only.
        ctx->perpLow  = sp.visualRect.top;
        ctx->perpHigh = sp.visualRect.bottom;
      } else {
        ctx->axisOriginInParent = 0;          // reservedTop
        ctx->axisLengthForRatio = totalH;
        // The ghost line spans this splitter's horizontal extent only —
        // important for inner splitters (e.g. Tri_A's right-column
        // splitter) that don't span the full client width.
        ctx->perpLow  = sp.visualRect.left;
        ctx->perpHigh = sp.visualRect.right;
      }
    }
    SetWindowPos(s, HWND_TOP,
                 sp.hitRect.left, sp.hitRect.top,
                 sp.hitRect.right - sp.hitRect.left,
                 sp.hitRect.bottom - sp.hitRect.top,
                 SWP_NOACTIVATE);
    ShowWindow(s, SW_SHOWNA);
    InvalidateRect(s, nullptr, FALSE);
  }
}

void MainWindow::initRatiosToDefaults() noexcept {
  using fast_explorer::core::LayoutPreset;
  for (std::size_t i = 0; i < ratiosPerPreset_.size(); ++i) {
    ratiosPerPreset_[i] =
        fast_explorer::ui::defaultRatiosFor(static_cast<LayoutPreset>(i));
  }
}

bool MainWindow::installPaneAt(std::size_t idx) {
  // Slot 0 is set up in onCreate; never re-installed from here.
  if (idx == 0) return true;
  if (idx >= listViews_.size()) return false;
  if (!paneManager_ || idx >= paneManager_->count()) return false;
  if (hwnd_ == nullptr) return false;

  // Create the listview (mirror the slot-0 creation in onCreate).
  HWND lv = createListView(hwnd_, instance_);
  if (lv == nullptr) {
    return false;
  }
  if (!addColumns(lv, GetDpiForWindow(hwnd_))) {
    DestroyWindow(lv);
    return false;
  }
  ListView_SetItemCountEx(lv, 0, 0);
  listViews_[idx] = lv;

  // Drop target.
  auto* dt = new (std::nothrow) PaneDropTarget(lv, paneManager_.get(), idx);
  if (dt) {
    if (SUCCEEDED(RegisterDragDrop(lv, dt))) {
      dropTargets_[idx] = dt;
    } else {
      dt->Release();
    }
  }

  // Toolbar row + address bar + dropdown.
  paneToolbarRows_[idx] = std::make_unique<PaneToolbarRow>();
  if (!paneToolbarRows_[idx]->create(hwnd_, instance_, idx,
                                      makePaneToolbarRowConfig())) {
    paneToolbarRows_[idx].reset();
  }
  HWND addressParent = paneToolbarRows_[idx]
                           ? paneToolbarRows_[idx]->handle()
                           : hwnd_;
  addressBars_[idx] = AddressInput::create(addressParent, instance_);
  addressDropdownBtns_[idx] =
      createAddressDropdownBtn(addressParent, instance_, idx);
  if (addressBars_[idx]) {
    if (paneToolbarRows_[idx]) {
      paneToolbarRows_[idx]->setAddressBar(addressBars_[idx]);
      paneToolbarRows_[idx]->setAddressDropdownBtn(addressDropdownBtns_[idx]);
    }
    SetWindowSubclass(addressBars_[idx], &MainWindow::addressBarSubclassProc, 0,
                      static_cast<DWORD_PTR>(idx));
  }

  // Inherit current view toggle so the freshly opened pane's first
  // enumerate honours the user's saved/active preference.
  paneManager_->at(idx).setIncludeHidden(showHidden_);

  if (!installPaneCoordinators(idx, lv)) {
    // Coordinator construction failed mid-flight. Roll back the
    // visible UI so we do not leave a listview backed by a partial
    // coordinator chain. Caller (enterLayout) is responsible for the
    // corresponding paneManager_->closePane() if needed.
    if (dropTargets_[idx] != nullptr) {
      RevokeDragDrop(lv);
      dropTargets_[idx]->Release();
      dropTargets_[idx] = nullptr;
    }
    if (addressBars_[idx]) {
      DestroyWindow(addressBars_[idx]);
      addressBars_[idx] = nullptr;
    }
    if (addressDropdownBtns_[idx]) {
      DestroyWindow(addressDropdownBtns_[idx]);
      addressDropdownBtns_[idx] = nullptr;
    }
    paneToolbarRows_[idx].reset();
    DestroyWindow(lv);
    listViews_[idx] = nullptr;
    return false;
  }
  // openFolder (if any) is driven by the caller; sync address bar so
  // the slot is not blank between create and the first openFolder.
  syncAddressBar(idx);
  // Construct port adapters last so a half-built slot never leaves an
  // adapter pointing at a torn-down PaneController. If anything above
  // fails (return false), the adapters_[idx] slot stays empty.
  activeForPane_[idx] = &paneManager_->at(idx);  // bridge: same owner
  itemSources_[idx] = std::make_unique<adapters::ShellItemSource>(
      activeForPane_[idx]);
  itemDispatchers_[idx] =
      std::make_unique<adapters::ShellItemDispatcher>(activeForPane_[idx]);
  clipboards_[idx] =
      std::make_unique<adapters::ShellClipboard>(activeForPane_[idx], lv);
  dragDrops_[idx] =
      std::make_unique<adapters::ShellDragDrop>(activeForPane_[idx], lv);
  contextMenus_[idx] =
      std::make_unique<adapters::ShellContextMenuAdapter>(activeForPane_[idx],
                                                         hwnd_);
  return true;
}

void MainWindow::uninstallPaneAt(std::size_t idx) {
  if (idx == 0) return;
  if (idx >= listViews_.size()) return;
  if (hwnd_ == nullptr) return;

  // Drop adapters first — they borrow non-owning pointers into the
  // PaneController that the rest of teardown (and the eventual
  // paneManager_->closePane) is about to invalidate. Reset the cell
  // AFTER all adapters are gone so no adapter destructor can
  // dereference a stale cell.
  contextMenus_[idx].reset();
  dragDrops_[idx].reset();
  clipboards_[idx].reset();
  itemDispatchers_[idx].reset();
  itemSources_[idx].reset();
  activeForPane_[idx] = nullptr;

  // Hide the popup before any pane HWNDs go away — its mouse hook
  // and pending pick payloads anchor on those windows.
  if (addressBarPopup_) {
    addressBarPopup_->hide();
  }
  // Stop any pending per-pane timers BEFORE tearing the pane down so
  // a queued WM_TIMER cannot reference the just-destroyed slot.
  KillTimer(hwnd_, kTimerFsCoalesceBase + idx);
  KillTimer(hwnd_, kTimerSelSummaryBase + idx);
  KillTimer(hwnd_, kTimerFilterDebounceBase + idx);
  // Release per-pane coordinators first so the slot's worker threads
  // (icon STA, shell STA) join before we tear down the PaneController
  // they reference.
  labelEdits_[idx].reset();
  selectionSyncs_[idx].reset();
  iconCoords_[idx].reset();
  // Clear the IListView->callback wiring before DestroyWindow so
  // comctl32 stops querying a callback whose backing data is about
  // to disappear. Then drop our own ref; if comctl32 still holds one
  // (it should, until DestroyWindow), the object survives until
  // common-controls releases it during the listview teardown.
  if (groupCallbacks_[idx] != nullptr && listViews_[idx] != nullptr) {
    IListView_FE* lv2 = nullptr;
    SendMessageW(listViews_[idx], kLvmQueryInterface,
                 reinterpret_cast<WPARAM>(&IID_IListView_FE),
                 reinterpret_cast<LPARAM>(&lv2));
    if (lv2 != nullptr) {
      lv2->SetOwnerDataCallback(nullptr);
      lv2->Release();
    }
  }
  if (groupCallbacks_[idx] != nullptr) {
    groupCallbacks_[idx]->Release();
    groupCallbacks_[idx] = nullptr;
  }
  if (dropTargets_[idx] != nullptr && listViews_[idx] != nullptr) {
    RevokeDragDrop(listViews_[idx]);
    dropTargets_[idx]->Release();
    dropTargets_[idx] = nullptr;
  }
  if (listViews_[idx] != nullptr) {
    DestroyWindow(listViews_[idx]);
    listViews_[idx] = nullptr;
  }
  if (addressBars_[idx] != nullptr) {
    DestroyWindow(addressBars_[idx]);
    addressBars_[idx] = nullptr;
  }
  if (addressDropdownBtns_[idx] != nullptr) {
    DestroyWindow(addressDropdownBtns_[idx]);
    addressDropdownBtns_[idx] = nullptr;
  }
  paneToolbarRows_[idx].reset();
}

void MainWindow::enterLayout(fast_explorer::core::LayoutPreset target) {
  using fast_explorer::core::LayoutPreset;
  using fast_explorer::core::slotCountForPreset;
  if (hwnd_ == nullptr || !paneManager_) return;

  const std::size_t targetCount = slotCountForPreset(target);

  // Grow.
  while (paneManager_->count() < targetCount &&
         paneManager_->count() < PaneManager<PaneController>::kMaxPanes) {
    const std::wstring fallback = paneManager_->active().currentPath();
    paneManager_->openPane(hwnd_, L"");  // create slot only
    const std::size_t newIdx = paneManager_->count() - 1;
    if (!installPaneAt(newIdx)) {
      // installPaneAt rolled back its UI; release the now-orphan
      // PaneController slot so count() reflects reality.
      paneManager_->closePane();
      break;
    }
    // Drive the folder load on the freshly opened slot.
    if (!fallback.empty() && paneManager_->at(newIdx).openFolder(fallback)) {
      clearListViewForNavigation(newIdx);
      if (newIdx < firstBatchSeen_.size()) {
        firstBatchSeen_[newIdx] = false;
      }
      const std::wstring text = loadingStatusText(fallback);
      setPaneStatusText(newIdx, text.c_str());
    }
    syncAddressBar(newIdx);
  }

  // Shrink.
  while (paneManager_->count() > targetCount) {
    const std::size_t idx = paneManager_->count() - 1;
    uninstallPaneAt(idx);
    paneManager_->closePane();
  }
  pane_ = &paneManager_->active();

  preset_ = target;
  if (target == LayoutPreset::Dual_V) {
    orientation_ = LayoutOrientation::Vertical;
    lastDualPreset_ = target;
  } else if (target == LayoutPreset::Dual_H) {
    orientation_ = LayoutOrientation::Horizontal;
    lastDualPreset_ = target;
  }
  if (paneManager_->activeIndex() >= targetCount) {
    paneManager_->setActive(targetCount - 1);
    pane_ = &paneManager_->active();
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
  applyActivePaneAppearance();
  syncAddressBar(idx);
  refreshSelectionSummary(idx);
}

void MainWindow::setLayoutOrientation(LayoutOrientation orientation) {
  if (orientation_ == orientation) {
    return;
  }
  orientation_ = orientation;
  if (paneManager_ && (paneManager_->count() > 1)) {
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
  // v0.2: adopt persisted view toggles. Defaults already match if the
  // file predates schema v4 (lenient missing-key handling in
  // loadSessionState). Propagate showHidden_ into every live pane —
  // the pane is created in onCreate (before this runs) with the C++
  // default includeHidden=true, so without this push a settings file
  // with showHidden=false would silently re-show hidden items on the
  // first openFolder.
  showHidden_ = state.showHidden;
  showExtensions_ = state.showExtensions;
  if (paneManager_) {
    for (std::size_t i = 0; i < paneManager_->count(); ++i) {
      paneManager_->at(i).setIncludeHidden(showHidden_);
    }
  }
}

void MainWindow::restoreLayoutFromSession(
    const fast_explorer::core::SessionState& state) {
  using fast_explorer::core::LayoutPreset;
  using fast_explorer::core::slotCountForPreset;
  if (!paneManager_) return;

  // Pull persisted ratios into the live store so the first relayout
  // honours the user's prior splitter positions. Per-preset slots that
  // are all-zero (fresh SessionState{} from a missing settings file, or
  // a serialized but empty `ratios` object) keep the per-preset
  // defaults already populated by initRatiosToDefaults() — without
  // this guard a first launch (or settings.json delete) clobbers the
  // defaults to 0/0/0, parking every splitter at the window edge and
  // making the drag handle effectively unreachable.
  for (std::size_t i = 0; i < ratiosPerPreset_.size(); ++i) {
    const auto& src = state.ratiosPerPreset[i];
    if (src.ratios[0] == 0.0f && src.ratios[1] == 0.0f &&
        src.ratios[2] == 0.0f) {
      continue;
    }
    ratiosPerPreset_[i] = src;
  }

  // Even when restoring a single-pane preset we adopt the persisted
  // orientation so the next Alt+V / Alt+H press lands in the user's
  // last-used seam.
  orientation_ = state.orientation;

  const std::size_t targetCount = slotCountForPreset(state.preset);

  // Slot 0 was opened by onCreate (its path was loaded in main.cpp
  // from state.panePaths[0] / lastPath). Open slots 1..targetCount-1
  // with their persisted paths.
  for (std::size_t i = 1;
       i < targetCount && i < PaneManager<PaneController>::kMaxPanes; ++i) {
    paneManager_->openPane(hwnd_, L"");  // create slot
    if (!installPaneAt(i)) {
      paneManager_->closePane();
      break;
    }
    // Use the active tab's path for this pane (v6 schema).
    const std::wstring path =
        (!state.panes[i].tabs.empty())
            ? state.panes[i].tabs[state.panes[i].activeTab].path
            : std::wstring{};
    const std::wstring& openIn =
        !path.empty() ? path : paneManager_->active().currentPath();
    if (!openIn.empty() && paneManager_->at(i).openFolder(openIn)) {
      clearListViewForNavigation(i);
      if (i < firstBatchSeen_.size()) {
        firstBatchSeen_[i] = false;
      }
      const std::wstring text = loadingStatusText(openIn);
      setPaneStatusText(i, text.c_str());
    }
    syncAddressBar(i);
  }

  preset_ = state.preset;
  if (preset_ == LayoutPreset::Dual_V) {
    orientation_ = LayoutOrientation::Vertical;
    lastDualPreset_ = preset_;
  } else if (preset_ == LayoutPreset::Dual_H) {
    orientation_ = LayoutOrientation::Horizontal;
    lastDualPreset_ = preset_;
  }
  if (state.activePane < paneManager_->count()) {
    paneManager_->setActive(state.activePane);
    pane_ = &paneManager_->active();
  }
  applyActivePaneAppearance();
  relayout();
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
  setPaneStatusText(activeIdx, text.c_str());
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
  // Disable group view during the navigation churn so common-controls
  // doesn't try to render against stale group IDs while the store is
  // being repopulated. finalizeSortApply will re-enable groups via
  // applyListViewGroups once the new content's sort completes (the
  // reapplyAfterEnumeration path triggers kWmFeSortComplete which
  // finalizes through finalizeSortApply).
  if (paneIdx < listViews_.size() && listViews_[paneIdx] != nullptr) {
    ListView_EnableGroupView(listViews_[paneIdx], FALSE);
    ListView_RemoveAllGroups(listViews_[paneIdx]);
  }
  // Drop the row→group mapping so a stale answer can't survive into the
  // new folder's first paint. finalizeSortApply rebuilds before
  // EnableGroupView(TRUE) once the new content has been sorted.
  if (paneIdx < groupCallbacks_.size() && groupCallbacks_[paneIdx] != nullptr) {
    groupCallbacks_[paneIdx]->clear();
  }
}

void MainWindow::showToolMenuForPane(std::size_t paneIdx) {
  if (paneIdx >= paneToolbarRows_.size() || !paneToolbarRows_[paneIdx]) {
    return;
  }
  HWND anchor = paneToolbarRows_[paneIdx]->hamburger();
  if (anchor == nullptr) return;
  // Built fresh on each open so toggle ✓ marks reflect current global
  // state at click time (T7 wires the view toggles into the build).
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return;

  auto add = [&](WORD id, const wchar_t* label) {
    AppendMenuW(menu, MF_STRING, packCmd(id, paneIdx), label);
  };

  // Group 1 (T5) — accelerator-equivalent folder actions.
  // & mnemonics let the open menu be navigated with single-letter
  // shortcuts per Windows menu convention; TrackPopupMenuEx honours
  // them automatically.
  add(kMenuNewFolder, L"새 폴더(&N)\tCtrl+Shift+N");
  add(kMenuRefresh,   L"새로 고침(&R)\tF5");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // Group 2 (T6) — shell integrations scoped to the pane's folder.
  add(kMenuOpenExplorer, L"탐색기에서 열기(&E)");
  add(kMenuOpenTerminal, L"터미널에서 열기(&T)");
  add(kMenuCopyPath,     L"경로 복사(&C)\tCtrl+Shift+C");
  add(kMenuProperties,   L"폴더 속성(&P)\tAlt+Enter");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // Group 3 (T7) — global view toggles. ✓ mark reflects current state,
  // identical across both panes' hamburgers (D4: scope=global).
  AppendMenuW(menu,
              MF_STRING | (showHidden_ ? MF_CHECKED : MF_UNCHECKED),
              packCmd(kMenuShowHidden, paneIdx), L"숨김 항목 표시(&H)");
  AppendMenuW(menu,
              MF_STRING | (showExtensions_ ? MF_CHECKED : MF_UNCHECKED),
              packCmd(kMenuShowExt, paneIdx), L"확장자 표시(&X)");

  // Group 4 — manual update check. Distinct from the auto-check loop
  // because that one honours WinSparkle's 24h LastCheckTime debounce
  // and there's no other path to force a check.
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, packCmd(kMenuCheckUpdates, paneIdx),
              L"업데이트 확인(&U)");

  RECT rc{};
  GetWindowRect(anchor, &rc);
  // TPM_RETURNCMD would let us read the chosen ID synchronously, but
  // routing through WM_COMMAND keeps the menu items in the same
  // packed-ID switch as the toolbar buttons (single source of truth).
  TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom,
                   hwnd_, nullptr);
  DestroyMenu(menu);
}

void MainWindow::updateNavButtonStates(std::size_t paneIdx) noexcept {
  if (paneIdx >= paneToolbarRows_.size() || !paneToolbarRows_[paneIdx] ||
      !paneManager_ || paneIdx >= paneManager_->count()) {
    return;
  }
  const auto& pc = paneManager_->at(paneIdx);
  auto& row = *paneToolbarRows_[paneIdx];
  row.setNavButtonEnabled(0, pc.canGoBack());
  row.setNavButtonEnabled(1, pc.canGoForward());
  row.setNavButtonEnabled(2, pc.canGoUp());
  // Refresh (slot 3) stays always-enabled; no setter call needed.
}

namespace {

// Undocumented but stable since Windows 10 1809: uxtheme.dll ordinal 133
// is AllowDarkModeForWindow(HWND, BOOL). SetPreferredAppMode (ord 135)
// at process start enables dark themes globally, but the scrollbar
// children of a listview ignore the parent's SetWindowTheme cascade
// and stay light unless their HWND is also explicitly flagged via
// this per-window opt-in. Resolved lazily, cached for the process.
using AllowDarkModeForWindow_t = BOOL(WINAPI*)(HWND, BOOL);
AllowDarkModeForWindow_t resolveAllowDarkModeForWindow() noexcept {
  static AllowDarkModeForWindow_t pfn = []() -> AllowDarkModeForWindow_t {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (ux == nullptr) return nullptr;
    return reinterpret_cast<AllowDarkModeForWindow_t>(
        GetProcAddress(ux, MAKEINTRESOURCEA(133)));
  }();
  return pfn;
}

}  // namespace

void MainWindow::applyListViewTheme(HWND lv) noexcept {
  if (lv == nullptr) return;
  const bool dark = isAppInDarkMode();
  // "DarkMode_ItemsView" is the theme class Win11 Explorer uses on its
  // own listviews — unlike "DarkMode_Explorer" it themes the group
  // header band as well as the rows, so LVS_OWNERDATA group titles
  // ("폴더" / "파일" / "오늘") render in a readable light gray instead
  // of the accent-tinted dim caption colour. Falls back to "Explorer"
  // in light mode to keep the standard hot-track + hover pill.
  SetWindowTheme(lv, dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
  // SetWindowTheme on the listview themes the rows + group header band
  // beautifully (DarkMode_ItemsView is the same class Win11 Explorer
  // uses on its own listviews) but does NOT theme the scrollbar
  // children — they keep the white "ScrollBar" theme that uxtheme hands
  // out by default. Two earlier attempts to fix this failed:
  //   (1) Layering a part-scoped DarkMode_Explorer/ScrollBar override
  //       on top of the ItemsView body theme caused scrollbar gripper
  //       glyphs (":" / "..") to leak into row cells and the hover pill
  //       to pick up the wrong shape (commits f6b760d / 6674c39).
  //   (2) A WM_NCPAINT subclass that intercepted mouse messages broke
  //       hover behaviour (commits 6f735ad / 5fe0077).
  // The clean fix lives one level deeper, at the comctl32 → uxtheme
  // delay-load boundary: dark-scrollbar-hook.cpp patches the IAT thunk
  // for OpenNcThemeData so that any "ScrollBar" classList becomes
  // "Explorer::ScrollBar" before uxtheme resolves the atlas. That
  // single redirection darkens every scrollbar in the process — listview,
  // treeview, edit, popup — without touching SetWindowTheme on the
  // body, so group header legibility and hover behaviour both stay
  // intact. The hook is installed once in wWinMain before MainWindow
  // is created (see installDarkScrollBarHook).
  //
  // AllowDarkModeForWindow + WM_THEMECHANGED is still needed below
  // because the per-window dark flag (uxtheme ordinal 133) gates a few
  // sub-theme decisions that the hook alone does not flip.
  if (auto pfn = resolveAllowDarkModeForWindow()) {
    pfn(lv, dark ? TRUE : FALSE);
    SendMessageW(lv, WM_THEMECHANGED, 0, 0);
  }
  // Per-cell text colour. The active-pane background is set later
  // in applyActivePaneAppearance; we set ours here so that if the
  // pane is inactive (dual mode) the background colour still
  // dark-mode tracks, then applyActivePaneAppearance overrides it
  // with the active/inactive tint.
  ListView_SetTextColor(lv, dark ? RGB(241, 241, 241)
                                  : GetSysColor(COLOR_WINDOWTEXT));
  // Header (column titles) needs its own theme application —
  // listview's DarkMode_Explorer does not propagate to the child
  // header on Win10. Win11 honours it; the call is a no-op there.
  HWND header = ListView_GetHeader(lv);
  if (header != nullptr) {
    SetWindowTheme(header, dark ? L"DarkMode_ItemsView" : L"ItemsView",
                   nullptr);
  }
  // Theme alone leaves the header text rendering near-black in dark
  // mode (its DrawThemeText pulls colour from the theme, ignoring our
  // SetTextColor). Install a listview subclass that catches header
  // NM_CUSTOMDRAW and full-paints the items when dark. Idempotent —
  // SetWindowSubclass on an already-subclassed (hwnd, proc, id)
  // updates refData without re-attaching, so calling on every
  // applyActivePaneAppearance pass is safe.
  constexpr UINT_PTR kHeaderColorSubclassId = 0xFE10A0u;
  DWORD_PTR refData = 0;
  if (!GetWindowSubclass(lv, &listViewHeaderColorSubclass,
                         kHeaderColorSubclassId, &refData)) {
    auto* state = new HeaderDarkState();
    state->refresh();
    SetWindowSubclass(lv, &listViewHeaderColorSubclass,
                      kHeaderColorSubclassId,
                      reinterpret_cast<DWORD_PTR>(state));
  } else if (auto* state = reinterpret_cast<HeaderDarkState*>(refData)) {
    state->refresh();
  }
  if (header != nullptr) {
    InvalidateRect(header, nullptr, TRUE);
  }
}

void MainWindow::applyActivePaneAppearance() noexcept {
  if (!paneManager_) return;
  const std::size_t active = paneManager_->activeIndex();
  // Inactive pane gets the dialog face colour so the focused pane is
  // visually obvious in dual mode; single mode skips the dim since
  // there is no other pane to contrast against.
  const bool dual = (paneManager_->count() > 1);
  const bool dark = isAppInDarkMode();
  const COLORREF activeBg   = dark ? RGB(32, 32, 32)
                                    : GetSysColor(COLOR_WINDOW);
  const COLORREF inactiveBg = dark ? RGB(24, 24, 24)
                                    : GetSysColor(COLOR_BTNFACE);
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    HWND lv = listViews_[i];
    if (lv == nullptr) continue;
    applyListViewTheme(lv);  // refresh theme + text color on each pass
    const COLORREF bg = (!dual || i == active) ? activeBg : inactiveBg;
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
    if (addressBars_[i] != nullptr && addressBars_[i] == ctrl) {
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
  // Click on the address bar → trigger the same dropdown popup the
  // ˅ button would. Posts WM_COMMAND with the kTbAddressDropdown
  // packed id so MainWindow::onCommand's existing handler routes it
  // (no new code path). Fired on WM_LBUTTONDOWN; we still forward
  // to DefSubclassProc so the click positions the caret normally.
  if (msg == WM_LBUTTONDOWN) {
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root != nullptr) {
      const WPARAM cmdW = MAKEWPARAM(
          packCmd(kTbAddressDropdown, static_cast<std::size_t>(dwRefData)),
          0);
      PostMessageW(root, WM_COMMAND, cmdW, 0);
    }
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
  // Explorer parity: VK_DELETE acts on the ENTIRE selection, not just
  // the focused row. Walk the list-view's selection bits directly so
  // the leaf set matches what the user sees highlighted (this stays
  // correct under filters since visibleAt(row) already resolves the
  // displaySubset → visibleOrder mapping). Falls back to the focused
  // row when nothing is selected (e.g. Delete pressed after a
  // single-click without Shift/Ctrl).
  PaneController& pane = paneManager_->at(paneIdx);
  std::vector<int> rows;
  int r = -1;
  while ((r = ListView_GetNextItem(focusedCtrl, r, LVNI_SELECTED)) >= 0) {
    rows.push_back(r);
  }
  if (rows.empty()) {
    const int focused =
        ListView_GetNextItem(focusedCtrl, -1, LVNI_FOCUSED);
    if (focused < 0) {
      return;
    }
    rows.push_back(focused);
  }
  // Descending order so each deleteItem call indexes into a store
  // whose later rows have not yet been removed. The shell worker
  // queues each command and processes them in FIFO; the order on
  // the queue does not matter for IFileOperation's recycle-bin
  // delete, but resolving the row → path at request time DOES
  // require the row index to still be valid against the current
  // store snapshot. Since each deleteItem() call only enqueues
  // (does not mutate the store), processing order is unimportant
  // here, but iterating descending keeps the row indices stable
  // for any future synchronous deleteItem variant.
  std::sort(rows.begin(), rows.end(),
            [](int a, int b) noexcept { return a > b; });
  for (int row : rows) {
    pane.deleteItem(static_cast<std::uint32_t>(row));
  }
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
  // Route address-bar Enter through the ItemSource port. The shell
  // adapter forwards to PaneController::openFolder so behaviour is
  // identical to the direct call this replaced — the only change is
  // that the entry point now talks to the port abstraction. Step 9+
  // migrate the remaining navigation entry points (back / forward /
  // up / double-click activation) one by one.
  ports::ItemSource* source = (paneIdx < itemSources_.size())
                                  ? itemSources_[paneIdx].get()
                                  : nullptr;
  const bool ok =
      source ? source->navigateTo(text)
             : paneManager_->at(paneIdx).openFolder(text);
  if (ok) {
    clearListViewForNavigation(paneIdx);
    syncAddressBar(paneIdx);
  }
}

void MainWindow::setPaneStatusText(std::size_t paneIdx, const wchar_t* text) {
  if (text == nullptr) return;
  if (paneIdx >= kMaxPanes) return;
  if (!paneManager_ || paneIdx != paneManager_->activeIndex()) {
    return;  // Only active pane drives the (single) status bar.
  }
  statusBar_.setText(0, text);
}

void MainWindow::applyStatusParts(int clientWidth) {
  (void)clientWidth;
  // Always a single full-width part — the bar shows ACTIVE pane info
  // only, regardless of paneCount.
  statusBar_.applySinglePart();
}

bool MainWindow::create(HINSTANCE instance, int showCommand) {
  instance_ = instance;
  registerAccelHandlers();
  ClassSpec cs;
  cs.className = kClassName;
  // App icon: large for Alt+Tab / taskbar, small for window caption.
  // MAKEINTRESOURCEW(IDI_APP) = IDI_APP in resources/resource-ids.h.
  cs.icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
  cs.iconSmall = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
  WindowSpec ws;
  ws.title = L"Fast Explorer";
  ws.style = WS_OVERLAPPEDWINDOW;
  ws.width = kDefaultWidth;
  ws.height = kDefaultHeight;
  HWND hwnd = createWindow(instance, cs, ws);
  if (hwnd == nullptr) {
    return false;
  }
  ShowWindow(hwnd, showCommand);
  UpdateWindow(hwnd);
  return true;
}

// registerAccelHandlers definition lives in main-window-commands.cpp
// — it is bulky (~260 lines of router registrations) and keeping it
// next to the rest of the WM_ handlers here would push main-window.cpp
// well past the readable-in-one-sitting threshold. Member-function-
// in-a-second-TU is a pure code-locality move; the function is still
// part of class MainWindow and accesses private state through `this`.


LRESULT MainWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:           return onCreate(hwnd);
    case WM_NOTIFY:           return handleListViewNotify(
                                  reinterpret_cast<NMHDR*>(lParam));
    case WM_DPICHANGED:       return onDpiChanged(hwnd, wParam, lParam);
    case WM_SIZE:             return onSize(hwnd, msg, wParam, lParam);
    case WM_COMMAND:          return onCommand(hwnd, msg, wParam, lParam);
    case WM_APPCOMMAND: {
      // 5-button mice send WM_APPCOMMAND with BROWSER_BACKWARD /
      // FORWARD for the side thumb buttons. Route them to the
      // active pane's history the same way the toolbar buttons
      // do — synthesize a WM_COMMAND so all the
      // setActivePane / clearListViewForNavigation / address-bar
      // refresh + nav-state-update steps run via the existing
      // packed-id path. Must return TRUE so Windows knows the
      // command was handled (default would forward to parent).
      const WORD cmd = GET_APPCOMMAND_LPARAM(lParam);
      WORD btn = 0;
      if (cmd == APPCOMMAND_BROWSER_BACKWARD) btn = kTbBack;
      else if (cmd == APPCOMMAND_BROWSER_FORWARD) btn = kTbForward;
      if (btn != 0 && paneManager_) {
        const std::size_t active = paneManager_->activeIndex();
        SendMessageW(hwnd, WM_COMMAND,
                     MAKEWPARAM(packCmd(btn, active), 0), 0);
        return TRUE;
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_SETTINGCHANGE: {
      // Re-apply title-bar dark mode + Mica when the user toggles
      // the system theme. isThemeSettingChange filters the broadcast
      // down to the "ImmersiveColorSet" notification — other settings
      // (font scaling, accessibility) fire the same message and are
      // irrelevant here.
      if (isThemeSettingChange(wParam, lParam)) {
        applySystemTheme();
        // Re-apply listview + active-pane theming so the file
        // grid follows the system light↔dark flip without
        // restarting the app.
        applyActivePaneAppearance();
      }
      return 0;
    }
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
    case kWmFeFilterQuery: {
      // Free the heap-owned payload regardless of whether we use
      // it (paneIdx may now be out of range after a collapse).
      std::unique_ptr<std::wstring> text(
          reinterpret_cast<std::wstring*>(lParam));
      const std::size_t idx = static_cast<std::size_t>(wParam);
      if (idx < kMaxPanes) {
        SetTimer(hwnd_, kTimerFilterDebounceBase + idx,
                 kFilterDebounceMs, nullptr);
      }
      return 0;
    }
    case kWmFeFilterDismiss: {
      const std::size_t idx = static_cast<std::size_t>(wParam);
      if (paneManager_ && idx < paneManager_->count()) {
        paneManager_->at(idx).clearFilter();
        if (idx < listViews_.size() && listViews_[idx] != nullptr) {
          const std::size_t shown =
              paneManager_->at(idx).store().displayedCount();
          ListView_SetItemCountEx(listViews_[idx],
                                  static_cast<int>(shown),
                                  LVSICF_NOSCROLL);
          InvalidateRect(listViews_[idx], nullptr, TRUE);
        }
        refreshSelectionSummary(idx);
      }
      return 0;
    }
    case WM_TIMER:            return onTimer(hwnd, msg, wParam, lParam);
    case WM_DESTROY: {
      // Stop all pending timers before teardown so a queued
      // WM_TIMER does not race the pane-coordinator destruction
      // chain below. Win32 auto-purges per-HWND timers on destroy
      // but a message already pulled by DispatchMessage would
      // still fire — this explicit kill closes that window.
      for (std::size_t i = 0; i < kMaxPanes; ++i) {
        KillTimer(hwnd, kTimerFsCoalesceBase + i);
        KillTimer(hwnd, kTimerSelSummaryBase + i);
        KillTimer(hwnd, kTimerFilterDebounceBase + i);
      }
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
      // Reset all pane session data before repopulating.
      for (auto& p : capturedState_->panes) {
        p.tabs.clear();
        p.activeTab = 0;
      }
      if (paneManager_) {
        for (std::size_t i = 0;
             i < paneManager_->count() &&
             i < fast_explorer::core::kMaxPanes; ++i) {
          // v6: persist current path as a single tab (no multi-tab yet).
          fast_explorer::core::TabRecordV6 t;
          t.path = paneManager_->at(i).currentPath();
          capturedState_->panes[i].tabs.push_back(std::move(t));
          capturedState_->panes[i].activeTab = 0;
        }
        capturedState_->paneCount = paneManager_->count();
        capturedState_->activePane = paneManager_->activeIndex();
      } else {
        capturedState_->paneCount = 1;
        capturedState_->activePane = 0;
      }
      capturedState_->preset = preset_;
      capturedState_->ratiosPerPreset = ratiosPerPreset_;

      // Legacy v4 fields kept populated for any callers that still read them.
      capturedState_->lastPath =
          (!capturedState_->panes[0].tabs.empty())
              ? capturedState_->panes[0].tabs[0].path : std::wstring{};
      capturedState_->secondPath =
          (!capturedState_->panes[1].tabs.empty())
              ? capturedState_->panes[1].tabs[0].path : std::wstring{};
      capturedState_->layoutMode = capturedState_->paneCount > 1
                                       ? fast_explorer::core::LayoutMode::Dual
                                       : fast_explorer::core::LayoutMode::Single;
      capturedState_->orientation = orientation_;
      capturedState_->showHidden = showHidden_;
      capturedState_->showExtensions = showExtensions_;
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
      // Drop our refs on the group callbacks. comctl32 still holds its
      // own ref (set via SetOwnerDataCallback); that ref is released
      // when DestroyWindow tears the listview down, so the callback
      // object actually deletes some short time later from comctl32's
      // side. No need to call SetOwnerDataCallback(nullptr) — the
      // listview HWNDs are about to be destroyed anyway.
      for (std::size_t i = 0; i < groupCallbacks_.size(); ++i) {
        if (groupCallbacks_[i] != nullptr) {
          groupCallbacks_[i]->Release();
          groupCallbacks_[i] = nullptr;
        }
      }
      PostQuitMessage(0);
      return 0;
    }
    case WM_NCDESTROY:
      // Clear the registration so future low-memory events do not
      // PostMessage to a destroyed window. A notifier already
      // inside the prior callback keeps its captured HWND and the
      // PostMessage is benign (dropped if the window is gone). The
      // WindowBase dispatcher clears GWLP_USERDATA + hwnd_ after we
      // return.
      memory_.setLowMemoryCallback(nullptr);
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
  // StatusBar::create installs the dark-mode subclass internally so
  // the bar at the bottom of the window tints to match the chrome
  // at the top. The wrapper owns the subclass state (heap allocated,
  // freed at WM_NCDESTROY) so this call site stays one line.
  statusBar_.create(hwnd, instance_);
  // dropTargets_[0] is registered after paneManager_ exists; see below.
  paneToolbarRows_[0] = std::make_unique<PaneToolbarRow>();
  if (!paneToolbarRows_[0]->create(hwnd, instance_, 0,
                                    makePaneToolbarRowConfig())) {
    paneToolbarRows_[0].reset();
  }
  HWND addressParent0 = paneToolbarRows_[0]
                            ? paneToolbarRows_[0]->handle()
                            : hwnd;
  addressBars_[0] = AddressInput::create(addressParent0, instance_);
  addressDropdownBtns_[0] = createAddressDropdownBtn(addressParent0, instance_, 0);
  if (addressBars_[0]) {
    if (paneToolbarRows_[0]) {
      paneToolbarRows_[0]->setAddressBar(addressBars_[0]);
      paneToolbarRows_[0]->setAddressDropdownBtn(addressDropdownBtns_[0]);
    }
    SetWindowSubclass(addressBars_[0], &MainWindow::addressBarSubclassProc, 0,
                      static_cast<DWORD_PTR>(0));
    addressBarPopup_ = std::make_unique<AddressBarPopup>(hwnd);
    searchPopup_ = std::make_unique<SearchPopup>(hwnd);
  }
  paneManager_ = std::make_unique<PaneManager<PaneController>>();
  paneManager_->openInitial(hwnd);
  pane_ = &paneManager_->active();
  activeForPane_[0] = &paneManager_->at(0);  // bridge: same owner
  itemSources_[0] = std::make_unique<adapters::ShellItemSource>(
      activeForPane_[0]);
  itemDispatchers_[0] =
      std::make_unique<adapters::ShellItemDispatcher>(activeForPane_[0]);
  clipboards_[0] =
      std::make_unique<adapters::ShellClipboard>(activeForPane_[0], listView_);
  dragDrops_[0] =
      std::make_unique<adapters::ShellDragDrop>(activeForPane_[0], listView_);
  contextMenus_[0] =
      std::make_unique<adapters::ShellContextMenuAdapter>(activeForPane_[0],
                                                         hwnd);
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
  // A8: apply dark-mode title bar + Mica backdrop based on the
  // current Windows theme. Re-applied later if the user toggles
  // Settings → Personalization → Colors via WM_SETTINGCHANGE.
  applySystemTheme();

  initRatiosToDefaults();

  // Register the splitter class once (idempotent).
  PaneSplitter::registerClass(instance_);

  // Pre-create 3 splitter HWNDs in a hidden state. Visible/positioned
  // by relayout() based on the active preset's splitterCount.
  for (std::size_t i = 0; i < splitterHwnds_.size(); ++i) {
    SplitterContext ctx;
    ctx.orient = SplitterOrientation::Vertical;     // updated by relayout
    ctx.ratioId = 0;                                // updated by relayout
    ctx.ratios = &ratiosPerPreset_[static_cast<std::size_t>(preset_)];
    ctx.onCommit = [this]() { this->relayout(); };
    splitterHwnds_[i] = PaneSplitter::create(instance_, hwnd, std::move(ctx));
    if (splitterHwnds_[i]) ShowWindow(splitterHwnds_[i], SW_HIDE);
  }
  return 0;
}

namespace {
// Header NM_CUSTOMDRAW from the listview's child Header doesn't bubble
// past the listview itself — we have to subclass the listview to see
// it. In dark mode we take the full draw (fill rect + draw text);
// otherwise pass through so themed light-mode rendering stays
// pixel-perfect.
LRESULT CALLBACK listViewHeaderColorSubclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR idSubclass, DWORD_PTR refData) {
  auto* state = reinterpret_cast<HeaderDarkState*>(refData);
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, &listViewHeaderColorSubclass, idSubclass);
    if (state != nullptr) {
      if (state->bgBrush != nullptr) DeleteObject(state->bgBrush);
      delete state;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_NOTIFY && isAppInDarkMode() && state != nullptr) {
    auto* hdr = reinterpret_cast<NMHDR*>(lParam);
    HWND header = ListView_GetHeader(hwnd);
    if (hdr != nullptr && hdr->hwndFrom == header &&
        hdr->code == NM_CUSTOMDRAW) {
      auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(hdr);
      switch (cd->dwDrawStage) {
        case CDDS_PREPAINT:
          if (!state->valid) state->refresh();
          // Fill the whole header strip first so any gap past the
          // last column doesn't leak the system Btn3D Highlight.
          FillRect(cd->hdc, &cd->rc, state->bgBrush);
          return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
          if (!state->valid) state->refresh();
          wchar_t buf[256] = {0};
          HDITEMW hdi{};
          hdi.mask = HDI_TEXT;
          hdi.pszText = buf;
          hdi.cchTextMax = static_cast<int>(std::size(buf));
          Header_GetItem(header, cd->dwItemSpec, &hdi);
          FillRect(cd->hdc, &cd->rc, state->bgBrush);
          // Subtle 1px right separator so columns read as distinct.
          RECT sep = cd->rc;
          sep.left = sep.right - 1;
          HBRUSH sepBr = CreateSolidBrush(state->sepColor);
          FillRect(cd->hdc, &sep, sepBr);
          DeleteObject(sepBr);
          RECT text = cd->rc;
          text.left += 8;
          text.right -= 8;
          SetBkMode(cd->hdc, TRANSPARENT);
          SetTextColor(cd->hdc, state->textColor);
          HFONT fnt = reinterpret_cast<HFONT>(
              SendMessageW(header, WM_GETFONT, 0, 0));
          HGDIOBJ oldFnt = fnt ? SelectObject(cd->hdc, fnt) : nullptr;
          DrawTextW(cd->hdc, buf, -1, &text,
                    DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                        DT_END_ELLIPSIS);
          if (oldFnt) SelectObject(cd->hdc, oldFnt);
          return CDRF_SKIPDEFAULT;
        }
      }
    }
  }
  if ((msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE) &&
      state != nullptr) {
    state->valid = false;
    InvalidateRect(ListView_GetHeader(hwnd), nullptr, TRUE);
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

void MainWindow::applySystemTheme() {
  if (hwnd_ == nullptr) return;
  // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 on Win10 build 18985+ /
  // Win11. On earlier Win10 builds the same effect was on attribute
  // 19; trying both lets the app dark-tone the title bar across
  // supported targets without a runtime version check.
  const BOOL dark = isAppInDarkMode() ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
  DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof(dark));
  // DWMWA_SYSTEMBACKDROP_TYPE = 38, DWMSBT_MAINWINDOW = 2 (Mica).
  // Win11 22H2+; silently no-ops on older builds, so safe to call
  // unconditionally.
  const int backdrop = 2;
  DwmSetWindowAttribute(hwnd_, 38, &backdrop, sizeof(backdrop));
}

LRESULT MainWindow::onDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam) {
  const auto* rect = reinterpret_cast<const RECT*>(lParam);
  SetWindowPos(hwnd, nullptr, rect->left, rect->top,
               rect->right - rect->left, rect->bottom - rect->top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  const UINT newDpi = LOWORD(wParam);
  for (HWND lv : listViews_) {
    if (lv != nullptr) {
      rescaleColumnWidths(lv, newDpi);
    }
  }
  // Forward to each toolbar row so its icon + text fonts get
  // recreated at the new DPI. Without this the glyphs render at
  // the old DPI's pixel size and look wrong on the new monitor.
  for (auto& row : paneToolbarRows_) {
    if (row) row->onDpiChanged(newDpi);
  }
  return 0;
}

LRESULT MainWindow::onSize(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  statusBar_.forwardSize();
  if (hwnd_ != nullptr) {
    relayout();
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
  // HIWORD(wParam) == 0 carries everything that is NOT an accelerator:
  // toolbar BN_CLICKED + menu selections, both packed (kTb* / kMenu*
  // with a pane idx in the low 4 bits) and bare ids (kMenuGroupBy*).
  // The router resolves both through one dispatch: by_id_ first for
  // bare ids, then unpackButton lookup against packed_.
  if (HIWORD(wParam) == 0 && accelRouter_.dispatch(LOWORD(wParam))) {
    return 0;
  }
  if (HIWORD(wParam) == 1) {
    // Try the router first. After step 12 it carries the bulk of the
    // single-action accelerators (registerAccelHandlers). The fall-
    // through switch below is what's left: cases that need post-
    // navigation refresh (clearListView + syncAddressBar +
    // updateNavButtonStates), cases with non-trivial pre-dispatch
    // (routeEditClipboardIfFocused / routeEditSelectAllIfFocused),
    // and the dual / orientation toggle that branches on preset_.
    if (accelRouter_.dispatch(LOWORD(wParam))) return 0;
    const std::size_t activeIdx =
        paneManager_ ? paneManager_->activeIndex() : 0;
    switch (LOWORD(wParam)) {
      // kAccelFocusAddress migrated to accelRouter_ (step 12).
      case kAccelNavBack:
        if (pane_ && pane_->back()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        updateNavButtonStates(activeIdx);
        return 0;
      case kAccelNavForward:
        if (pane_ && pane_->forward()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        updateNavButtonStates(activeIdx);
        return 0;
      case kAccelNavUp:
        if (pane_ && pane_->up()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        updateNavButtonStates(activeIdx);
        return 0;
      case kAccelRefresh:
        if (pane_ && pane_->refresh()) {
          clearListViewForNavigation(activeIdx);
          syncAddressBar(activeIdx);
        }
        // Refresh stays enabled; nothing to update for the toolbar,
        // but back/forward state may shift if refresh actually
        // re-opens the same folder. Cheap to recompute.
        updateNavButtonStates(activeIdx);
        return 0;
      // kAccelDelete / Rename / CreateFolder / CopyPath / Properties
      // / ToolMenu / LayoutSingle all migrated to accelRouter_ (step 12).
      case kAccelLayoutDual: {
        using fast_explorer::core::LayoutPreset;
        if (preset_ == LayoutPreset::Dual_V ||
            preset_ == LayoutPreset::Dual_H) {
          // Same-key toggle: dual + matching seam => exit to single.
          // Alt+V / Alt+H handles the in-place seam flip.
          enterLayout(LayoutPreset::Single);
        } else {
          enterLayout(lastDualPreset_);
        }
        return 0;
      }
      // kAccelLayoutTri / Quad / Filter migrated to accelRouter_ (step 12).
      case kAccelLayoutVerticalToggle:
      case kAccelLayoutHorizontalToggle: {
        using fast_explorer::core::LayoutPreset;
        if (preset_ != LayoutPreset::Dual_V &&
            preset_ != LayoutPreset::Dual_H) {
          return 0;  // no-op outside dual
        }
        const LayoutOrientation pressed =
            LOWORD(wParam) == kAccelLayoutHorizontalToggle
                ? LayoutOrientation::Horizontal
                : LayoutOrientation::Vertical;
        const auto t = resolveLayoutToggle(true, orientation_, pressed);
        switch (t.action) {
          case LayoutAction::EnterDual:
            // Already dual; resolveLayoutToggle should not return
            // EnterDual when `dual=true`, but stay safe and re-enter
            // the matching seam.
            enterLayout(pressed == LayoutOrientation::Horizontal
                            ? LayoutPreset::Dual_H
                            : LayoutPreset::Dual_V);
            break;
          case LayoutAction::ExitToSingle:
            enterLayout(LayoutPreset::Single);
            break;
          case LayoutAction::SwitchOrientation:
            enterLayout(t.target == LayoutOrientation::Horizontal
                            ? LayoutPreset::Dual_H
                            : LayoutPreset::Dual_V);
            break;
          // No default: every enumerator is handled. A future addition
          // to LayoutAction should surface here as a compiler warning
          // (/W4 -Wswitch) rather than silently falling through.
        }
        return 0;
      }
      case kAccelCopy:
        if (!routeEditClipboardIfFocused(WM_COPY)) {
          handleClipboardCopy(false);
        }
        return 0;
      case kAccelCut:
        if (!routeEditClipboardIfFocused(WM_CUT)) {
          handleClipboardCopy(true);
        }
        return 0;
      case kAccelPaste:
        if (!routeEditClipboardIfFocused(WM_PASTE)) {
          handleClipboardPaste();
        }
        return 0;
      // kAccelPaneSwitch migrated to accelRouter_ (step 12).
      case kAccelSelectAll: {
        // Edit focus → select the Edit's text (single-line Edit has no
        // native Ctrl+A). Otherwise, select every row in the active
        // pane's list-view. SetItemState with -1 broadcasts to all
        // items; under LVS_OWNERDATA this still fires LVN_ODSTATECHANGED
        // so SelectionSync resyncs PaneController in one pass.
        if (routeEditSelectAllIfFocused()) {
          return 0;
        }
        HWND focused = GetFocus();
        std::size_t paneIdx = 0;
        HWND lv = nullptr;
        if (focused != nullptr &&
            paneIndexFromListView(focused, paneIdx)) {
          lv = focused;
        } else if (paneManager_) {
          const std::size_t active = paneManager_->activeIndex();
          if (active < listViews_.size()) lv = listViews_[active];
        }
        if (lv != nullptr) {
          ListView_SetItemState(lv, -1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
      }
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
  if (wParam >= kTimerSelSummaryBase &&
      wParam < kTimerSelSummaryBase + kMaxPanes) {
    const std::size_t idx = wParam - kTimerSelSummaryBase;
    KillTimer(hwnd, wParam);
    refreshSelectionSummary(idx);
    return 0;
  }
  if (wParam >= kTimerFilterDebounceBase &&
      wParam < kTimerFilterDebounceBase + kMaxPanes) {
    const std::size_t idx = wParam - kTimerFilterDebounceBase;
    KillTimer(hwnd, wParam);
    applyFilterFromPopup(idx);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::applyFilterFromPopup(std::size_t paneIdx) {
  if (!paneManager_ || paneIdx >= paneManager_->count() || !searchPopup_) {
    return;
  }
  const std::wstring text = searchPopup_->currentText();
  // Mode picked from the input itself: "r:" prefix routes to Regex,
  // any other text containing * or ? routes to Wildcard, plain
  // substring otherwise. Matches the documented detectFilterMode
  // contract and avoids a separate mode-toggle UI for now.
  const auto detected = detectFilterMode(text);
  FilterPattern pattern(detected.query, detected.mode);
  PaneController& pane = paneManager_->at(paneIdx);
  pane.setFilter(pattern);
  if (paneIdx < listViews_.size() && listViews_[paneIdx] != nullptr) {
    const std::size_t shown = pane.store().displayedCount();
    ListView_SetItemCountEx(listViews_[paneIdx], static_cast<int>(shown),
                            LVSICF_NOSCROLL);
    InvalidateRect(listViews_[paneIdx], nullptr, TRUE);
  }
  // Surface a regex syntax error directly in the pane's status
  // part so the user sees "invalid regex" without an extra dialog.
  // For Plain / Wildcard the pattern is always valid, so the
  // generic selection-summary line wins.
  if (detected.mode == FilterMode::Regex && !pattern.isValid()) {
    setPaneStatusText(paneIdx, L"invalid regex");
  } else {
    refreshSelectionSummary(paneIdx);
  }
}

void MainWindow::refreshSelectionSummary(std::size_t paneIdx) {
  if (!paneManager_ || paneIdx >= paneManager_->count()) {
    return;
  }
  const PaneController& pane = paneManager_->at(paneIdx);
  const std::size_t totalCount = pane.store().itemCount();
  const auto sum = pane.selectionSummary();
  const std::wstring text =
      formatSelectionSummary(totalCount, sum.selectedCount, sum.selectedBytes);
  setPaneStatusText(paneIdx, text.c_str());
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
  setPaneStatusText(idx, text.c_str());
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
  const std::size_t idx = paneIndexFromWParam(wParam);
  // Single source of truth for the per-pane status line: the
  // selection-aware refresh handles both no-selection ("N items")
  // and sticky-selection (refresh / fs-watch path) cases.
  refreshSelectionSummary(idx);
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
  updateNavButtonStates(idx);
  return 0;
}

LRESULT MainWindow::onEnumError(WPARAM wParam, LPARAM lParam) {
  if (isStaleGeneration(wParam)) {
    return 0;
  }
  const auto err = static_cast<fast_explorer::core::EnumerationError>(lParam);
  const std::wstring text = errorStatusText(err);
  // errorStatusText returns empty for Canceled (fires every
  // typeahead in the address bar) and None — skip the status
  // write rather than overwriting whatever is currently shown.
  if (text.empty()) {
    return 0;
  }
  setPaneStatusText(paneIndexFromWParam(wParam), text.c_str());
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
  // No generation gate here: ShellWorker publishes via ResultChannel
  // which packs gen=0 unconditionally (it has no enumeration token
  // to track against). The worst case a stale OperationResult from
  // a pre-navigation Delete causes is one transient line of status
  // text on the new folder — fine. Gating on gen=0 vs. the pane's
  // current (non-zero post-openFolder) generation would drop EVERY
  // result and silently kill the "Moved N items to Recycle Bin"
  // confirmation. Pane-existence + null-target checks below still
  // protect against pane teardown races.
  PaneController* target = paneForWParam(wParam);
  if (target == nullptr) {
    return 0;
  }
  auto results = target->drainShellResults();
  if (results.empty()) {
    return 0;
  }
  // Aggregate batches into a single line ("Moved 12 items to
  // Recycle Bin") rather than showing only the last filename —
  // multi-select recycle-bin delete is the common multi-result
  // case and the prior code silently lost N-1 outcomes.
  const std::wstring text = opResultBatchStatusText(results);
  if (text.empty()) {
    return 0;
  }
  setPaneStatusText(paneIndexFromWParam(wParam), text.c_str());
  return 0;
}

LRESULT MainWindow::onFsChange(HWND hwnd, WPARAM wParam) {
  // No generation gate here: FsWatcher packs gen=0 in its WPARAM
  // (see fs-watcher.cpp). Gating on gen=0 vs. the pane's current
  // (non-zero post-openFolder) generation would drop EVERY change
  // notification and silently disable auto-refresh after Delete /
  // external edits. The old-folder-event race the gate was meant
  // to catch is bounded another way: PaneController::navigateInternal
  // calls fsWatcher_.stop() before resetting the store, which joins
  // the old worker — any subsequent debounce timer fire just
  // refreshes the (correct) new folder. Pane-bounds + null-target
  // checks below cover pane teardown / count shrink races.
  const std::size_t idx = paneIndexFromWParam(wParam);
  if (idx >= kMaxPanes) return 0;
  if (!paneManager_ || idx >= paneManager_->count()) return 0;
  // Debounce: every event restarts the per-pane timer; the actual
  // refresh fires once after kFsCoalesceMs of quiet. We pack the
  // pane index into the timer id so two panes' debounce windows do
  // not collide.
  const UINT_PTR timerId = kTimerFsCoalesceBase + idx;
  SetTimer(hwnd, timerId, kFsCoalesceMs, nullptr);
  return 0;
}

void MainWindow::handleClipboardCopy(bool cut) {
  if (!paneManager_) return;
  const std::size_t idx = paneManager_->activeIndex();
  if (idx >= listViews_.size() || listViews_[idx] == nullptr) return;
  if (!clipboards_[idx]) return;
  PaneController& pane = paneManager_->at(idx);
  // Single listview scan produces both the id vector for the port
  // and the leaf vector for the host-side cut-state visuals. The
  // adapter will do its own id->leaf lookup against the store; that
  // is one extra hash-free index lookup per id, negligible at
  // typical selection sizes.
  std::vector<ports::ItemId> ids;
  std::vector<std::wstring> leaves;
  HWND lv = listViews_[idx];
  const auto& store = pane.store();
  int row = -1;
  while ((row = ListView_GetNextItem(lv, row, LVNI_SELECTED)) >= 0) {
    const auto r = static_cast<std::size_t>(row);
    if (r >= store.publishedCount()) continue;
    const auto& entry = store.visibleAt(r);
    if (entry.namePtr == nullptr || entry.nameLength == 0) continue;
    ids.push_back(static_cast<ports::ItemId>(r + 1));
    if (cut) leaves.emplace_back(entry.namePtr, entry.nameLength);
  }
  if (ids.empty()) return;
  if (!clipboards_[idx]->copyItems(ids, cut)) return;
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
  if (!clipboards_[idx]) return;
  PaneController& pane = paneManager_->at(idx);
  if (pane.currentPath().empty()) return;
  if (clipboards_[idx]->pasteInto(pane.currentPath()) ==
      ports::PasteOutcome::Success) {
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
  if (!dragDrops_[paneIdx]) return;
  // Collect ids from listview selection and hand them to the port.
  // The DragDropBackend adapter owns the shell-bind + GetUIObjectOf
  // + DoDragDrop sequence so this entry point stays Win32-agnostic.
  std::vector<ports::ItemId> ids;
  HWND lv = hdr->hwndFrom;
  const auto& store = paneManager_->at(paneIdx).store();
  int row = -1;
  while ((row = ListView_GetNextItem(lv, row, LVNI_SELECTED)) >= 0) {
    const auto r = static_cast<std::size_t>(row);
    if (r >= store.publishedCount()) continue;
    const auto& entry = store.visibleAt(r);
    if (entry.namePtr == nullptr || entry.nameLength == 0) continue;
    ids.push_back(static_cast<ports::ItemId>(r + 1));
  }
  if (ids.empty()) return;
  dragDrops_[paneIdx]->beginDrag(ids);
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
  applyListViewGroups(paneIdx);
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
        // Debounce status-summary updates: a single Ctrl+A on a
        // 100k folder fires ~100k LVN_ITEMCHANGED messages back to
        // back, but we only need one final summary repaint after
        // the storm settles. The timer is reset on every event.
        // Skip while a navigate is in flight (firstBatchSeen_ is
        // false from openFolder until the first enum batch) so
        // selection events from the cleared list-view don't
        // race the incoming enum's batches.
        const auto* nmlv = reinterpret_cast<const NMLISTVIEW*>(hdr);
        const bool enumerating =
            idx < firstBatchSeen_.size() && !firstBatchSeen_[idx];
        if (!enumerating && (nmlv->uChanged & LVIF_STATE)) {
          const UINT oldSel = nmlv->uOldState & LVIS_SELECTED;
          const UINT newSel = nmlv->uNewState & LVIS_SELECTED;
          if (oldSel != newSel) {
            SetTimer(hwnd_, kTimerSelSummaryBase + idx,
                     kSelSummaryDebounceMs, nullptr);
          }
        }
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
      return 0;
    case LVN_ODFINDITEMW:
      return handleOdFindItem(hdr);
    case LVN_ODSTATECHANGED: {
      // LVS_OWNERDATA reports batch deselects of previously-selected
      // rows via this notification, NOT per-row LVN_ITEMCHANGED.
      // Without routing it through SelectionSync, single-click
      // selection changes leak: every click adds rows to
      // PaneController::selectedRaws_ without ever removing the
      // previous selection.
      std::size_t idx = 0;
      if (paneIndexFromListView(hdr->hwndFrom, idx) &&
          idx < selectionSyncs_.size() && selectionSyncs_[idx]) {
        selectionSyncs_[idx]->handleOdStateChanged(hdr);
        // Debounce a summary repaint same as LVN_ITEMCHANGED — a
        // Ctrl+A reaches us as a single OdStateChanged with
        // [iFrom..iTo] spanning the whole visible range.
        SetTimer(hwnd_, kTimerSelSummaryBase + idx,
                 kSelSummaryDebounceMs, nullptr);
      }
      return 0;
    }
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
  if (nmia->iItem < 0) {
    // Mouse right-click on empty area, or keyboard Shift+F10 over
    // empty area. Show the shell folder-background menu (새로 만들기
    // / 붙여넣기 / 속성 / ...) with our 분류 방법 submenu appended at
    // the bottom — Win Explorer parity, plus the FastExplorer-specific
    // grouping affordance.
    POINT emptyPt = nmia->ptAction;
    if (emptyPt.x == -1 && emptyPt.y == -1) {
      // Keyboard-invoked → anchor to listview origin in screen coords.
      RECT lvRect{};
      GetWindowRect(hdr->hwndFrom, &lvRect);
      emptyPt.x = lvRect.left;
      emptyPt.y = lvRect.top;
    } else {
      ClientToScreen(hdr->hwndFrom, &emptyPt);
    }
    using fast_explorer::core::GroupKey;
    const auto cur = targetPane.groupBy();
    ShellContextMenu::ExtraSubmenu extra;
    extra.label = L"분류 방법";
    extra.items = {
        {kMenuGroupByNone,     L"(없음)"},
        {kMenuGroupByName,     L"이름"},
        {kMenuGroupByModified, L"수정한 날짜"},
        {kMenuGroupByType,     L"유형"},
    };
    extra.radioFirst = kMenuGroupByNone;
    extra.radioLast  = kMenuGroupByType;
    extra.radioChecked =
        cur == GroupKey::Name     ? kMenuGroupByName
      : cur == GroupKey::Modified ? kMenuGroupByModified
      : cur == GroupKey::Type     ? kMenuGroupByType
      :                             kMenuGroupByNone;
    ShellContextMenu::show(hwnd_, folderPath, /*leaves=*/{}, emptyPt,
                           &extra);
    return;
  }
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

  // Walk the list-view's own selection bits so the leaf set matches
  // exactly what the user sees as selected — this stays correct
  // regardless of whether a filter is active (displaySubset_ may
  // remap row indices vs. visibleOrder_, but visibleAt(row) already
  // knows which side to read).
  std::vector<int> selectedRows;
  {
    int r = -1;
    while ((r = ListView_GetNextItem(hdr->hwndFrom, r, LVNI_SELECTED)) >= 0) {
      selectedRows.push_back(r);
    }
  }
  const int clicked = nmia->iItem;
  const bool clickedRowSelected =
      clicked >= 0 &&
      std::find(selectedRows.begin(), selectedRows.end(), clicked) !=
          selectedRows.end();

  const auto& store = targetPane.store();
  const std::size_t shown = store.displayedCount();
  std::vector<ports::ItemId> ids;
  if (clicked >= 0) {
    if (clickedRowSelected && selectedRows.size() > 1) {
      // Multi-selection right-click on one of the selected rows:
      // address every selected item, matching Explorer's behavior
      // ("Delete", "Cut", "Properties" all act on the whole group).
      // Validate each entry's name pointer before appending so a
      // stale OWNERDATA selection bit (which can survive an
      // fs-watch-triggered refresh after a Delete) does not inject
      // an invalid id — the adapter's resolveLeaves drops them,
      // but trimming early keeps the id vector tight.
      ids.reserve(selectedRows.size());
      for (int row : selectedRows) {
        if (static_cast<std::size_t>(row) < shown) {
          const auto& e = store.visibleAt(static_cast<std::size_t>(row));
          if (e.namePtr != nullptr && e.nameLength > 0) {
            ids.push_back(static_cast<ports::ItemId>(row + 1));
          }
        }
      }
    } else {
      // Right-click outside the existing selection (or on a single
      // selected row): collapse to just the clicked row and
      // re-stamp the selection so the visual matches the menu
      // target. Explorer does the same — clicking an unselected
      // file replaces the selection set with just that file.
      if (!clickedRowSelected) {
        ListView_SetItemState(hdr->hwndFrom, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(hdr->hwndFrom, clicked,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
      }
      if (static_cast<std::size_t>(clicked) < shown) {
        const auto& e = store.visibleAt(static_cast<std::size_t>(clicked));
        if (e.namePtr != nullptr && e.nameLength > 0) {
          ids.push_back(static_cast<ports::ItemId>(clicked + 1));
        }
      }
    }
  }
  // clicked < 0 (empty area) → ids stays empty so the adapter
  // resolves to the folder's background menu (Open / Paste /
  // "New" / Properties). The augmented empty-area path (with the
  // group-by submenu) returned earlier in this function and never
  // reaches here.
  if (contextMenus_[paneIdx]) {
    contextMenus_[paneIdx]->show(ids, screenPt);
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
    case CDDS_ITEMPREPAINT: {
      // Group header pre-paint (Vista+): dwItemType == LVCDI_GROUP.
      // dwItemSpec then carries the group ID, not a row index — must
      // be handled before the row-based dimmed-file logic below.
      // System default uses a dark text colour that is unreadable on
      // dark backgrounds, so override with a high-contrast colour.
      // Group header pre-paint (Vista+, themed listviews often bypass
      // this — kept as a best-effort fallback in case the theme stack
      // ever yields to custom-draw). Dark-mode legibility for group
      // headers in themed listviews is handled at theme-application
      // time, not here.
      if (cd->dwItemType == LVCDI_GROUP) {
        return CDRF_DODEFAULT;
      }
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
      // GRAYTEXT system colour is near-black on dark backgrounds —
      // pick a brighter grey when the system theme is dark so the
      // dimmed-hidden-files cue stays legible.
      cd->clrText = isAppInDarkMode() ? RGB(140, 140, 140)
                                              : GetSysColor(COLOR_GRAYTEXT);
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
  if ((disp->item.mask & (LVIF_TEXT | LVIF_IMAGE | LVIF_GROUPID)) == 0) {
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
  // Group-id must be assigned before the LVIF_TEXT early-return below,
  // since callbacks frequently request both flags in the same dispatch.
  if ((disp->item.mask & LVIF_GROUPID) != 0) {
    const auto gk = sourcePane.groupBy();
    if (gk == fast_explorer::core::GroupKey::None) {
      disp->item.iGroupId = I_GROUPIDNONE;  // ListView's "no group" sentinel
    } else {
      disp->item.iGroupId = fast_explorer::core::groupIdForEntry(
          gk, entry, sourcePane.groupNow());
    }
  }
  if ((disp->item.mask & LVIF_TEXT) == 0) {
    return;
  }
  switch (disp->item.iSubItem) {
    case 0: {
      auto view = fast_explorer::core::nameView(entry);
      // T8: extension strip for files (directories keep their full
      // name even when they happen to contain a dot). The stem ends
      // at extensionOffset, which is kNoExtension for files without
      // any '.'; in that case the if-guard collapses to a no-op.
      if (!showExtensions_ && !fast_explorer::core::isDirectory(entry) &&
          entry.extensionOffset != fast_explorer::core::kNoExtension &&
          entry.extensionOffset < view.size()) {
        view = view.substr(0, entry.extensionOffset);
      }
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

void MainWindow::applyListViewGroups(std::size_t paneIdx) {
  if (!paneManager_ || paneIdx >= paneManager_->count() ||
      paneIdx >= listViews_.size() || listViews_[paneIdx] == nullptr) {
    return;
  }
  HWND lv = listViews_[paneIdx];
  PaneController& pane = paneManager_->at(paneIdx);
  const auto gk = pane.groupBy();
  // Defensive: turn group view off before mutating definitions, so
  // common-controls doesn't try to render in a half-rebuilt state.
  ListView_EnableGroupView(lv, FALSE);
  ListView_RemoveAllGroups(lv);
  if (gk == fast_explorer::core::GroupKey::None) {
    return;  // grouping disabled
  }
  const auto ids = fast_explorer::core::enumerateGroups(
      gk, pane.store(), pane.groupNow());
  // Populate the IOwnerDataCallback's row→group mapping BEFORE inserting
  // groups: under LVS_OWNERDATA the LVGROUP::cItems we set below has to
  // line up with the callback's per-group row counts, and the callback
  // is what computed those counts. Skipping cItems leaves comctl32
  // laying out empty group containers (the v0.4.x symptom — group view
  // engaged but rows never painted).
  ListViewGroupCallback* cb =
      (paneIdx < groupCallbacks_.size()) ? groupCallbacks_[paneIdx] : nullptr;
  if (cb != nullptr) {
    cb->rebuild(pane.store(), gk, pane.groupNow(), ids);
  }
  for (size_t i = 0; i < ids.size(); ++i) {
    const int32_t id = ids[i];
    LVGROUP grp{};
    grp.cbSize    = sizeof(grp);
    grp.mask      = LVGF_GROUPID | LVGF_HEADER | LVGF_STATE | LVGF_ITEMS;
    grp.iGroupId  = id;
    grp.stateMask = LVGS_COLLAPSIBLE;
    grp.state     = LVGS_COLLAPSIBLE;
    grp.cItems    = static_cast<UINT>(cb ? cb->countInGroup(i) : 0);
    std::wstring header = fast_explorer::core::groupTitleForId(
        gk, id, &pane.store(), formatCache_.get());
    grp.pszHeader = const_cast<wchar_t*>(header.c_str());
    grp.cchHeader = static_cast<int>(header.size());
    ListView_InsertGroup(lv, -1, &grp);  // -1 = append
  }
  ListView_EnableGroupView(lv, TRUE);
  // Force dispinfo re-fetch so newly-rendered rows pick up iGroupId.
  const int count = ListView_GetItemCount(lv);
  if (count > 0) {
    ListView_RedrawItems(lv, 0, count - 1);
  }
}

LRESULT MainWindow::handleOdFindItem(NMHDR* hdr) {
  if (hdr == nullptr || !paneManager_) {
    return -1;
  }
  auto* nmf = reinterpret_cast<NMLVFINDITEMW*>(hdr);
  // LVFI_STRING covers both prefix (LVFI_PARTIAL) and exact searches.
  // Without a string we have nothing to match against.
  if ((nmf->lvfi.flags & LVFI_STRING) == 0 || nmf->lvfi.psz == nullptr) {
    return -1;
  }
  std::size_t paneIdx = 0;
  if (!paneIndexFromListView(hdr->hwndFrom, paneIdx) ||
      paneIdx >= paneManager_->count()) {
    return -1;
  }
  const auto& store = paneManager_->at(paneIdx).store();
  const std::size_t total = store.publishedCount();
  if (total == 0) {
    return -1;
  }
  const wchar_t* needle = nmf->lvfi.psz;
  const int needleLen = static_cast<int>(std::wcslen(needle));
  if (needleLen <= 0) {
    return -1;
  }
  // iStart is the row AFTER which to begin searching for non-wrap calls;
  // common-controls passes the focused row. We always wrap so a fresh
  // letter keystroke that no longer matches the current row still finds
  // the next item from row 0 onward (Win Explorer behaviour).
  std::size_t start = 0;
  if (nmf->iStart >= 0 &&
      static_cast<std::size_t>(nmf->iStart) < total) {
    start = static_cast<std::size_t>(nmf->iStart);
  }
  for (std::size_t step = 0; step < total; ++step) {
    const std::size_t row = (start + step) % total;
    const auto& entry = store.visibleAt(row);
    auto view = fast_explorer::core::nameView(entry);
    // Mirror handleGetDispInfoBody's extension-strip so typing matches
    // what the user actually sees in the Name column.
    if (!showExtensions_ && !fast_explorer::core::isDirectory(entry) &&
        entry.extensionOffset != fast_explorer::core::kNoExtension &&
        entry.extensionOffset < view.size()) {
      view = view.substr(0, entry.extensionOffset);
    }
    if (static_cast<int>(view.size()) < needleLen) {
      continue;
    }
    // CompareStringW handles Unicode, IME-composed Korean syllables,
    // and half/full-width forms; locale-aware so the user's regional
    // collation drives ordering of equivalent code points.
    const int cmp = CompareStringW(
        LOCALE_USER_DEFAULT, NORM_IGNORECASE | NORM_IGNOREWIDTH,
        view.data(), needleLen,
        needle, needleLen);
    if (cmp == CSTR_EQUAL) {
      return static_cast<LRESULT>(row);
    }
  }
  return -1;
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

// ---- Phase 4 stubs (filled in by Phase 5 Task 24) ----------------------

void MainWindow::bindListViewToActiveTab(std::size_t /*paneIdx*/) {
  // Stub: Phase 5 Task 24 provides the real implementation.
}

void MainWindow::refreshPaneChrome(std::size_t /*paneIdx*/) {
  // Stub: Phase 5 Task 24 provides the real implementation.
}

}  // namespace fast_explorer::ui
