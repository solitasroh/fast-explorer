#pragma once

#include <windows.h>
#include <oleidl.h>

#include <array>
#include <memory>
#include <string>

#include "winui_lite/chrome/layout-preset.h"
#include "ui/cut-state-tracker.h"
#include "winui_lite/chrome/pane-layout.h"
#include "ui/search-popup.h"
#include "winui_lite/chrome/splitter-ratios.h"

namespace fast_explorer::core {
class ProcessMemoryService;
class PerfTracker;
struct FileEntry;
struct SessionState;
}

namespace fast_explorer::ui {

class AddressBarPopup;
class DispInfoHistogram;
class LabelEditController;
class ListViewGroupCallback;
class PaneController;
class PaneManager;
class PaneToolbarRow;
class SelectionSync;

class MainWindow {
 public:
  // `memory` is non-owning; the AppServices owner must outlive the window.
  MainWindow(fast_explorer::core::ProcessMemoryService& memory,
             fast_explorer::core::PerfTracker& perf) noexcept;
  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool create(HINSTANCE instance, int showCommand);
  HWND handle() const noexcept { return hwnd_; }

  // Exposes the per-window LVN_GETDISPINFO latency histogram so
  // the shutdown path can dump it to the logger. Returns nullptr
  // when create() has not yet run.
  const DispInfoHistogram* dispInfoHistogram() const noexcept {
    return dispInfoHist_.get();
  }

  // Forwards to the owned PaneController. Returns false if the window
  // is not yet created or the path is invalid.
  bool openFolder(const std::wstring& path);

  // Switches to the given preset, opening or closing slots as needed.
  // Implementation lands in Task 27. Currently a no-op stub so the
  // build links while Task 25/26 wire infrastructure.
  void enterLayout(fast_explorer::core::LayoutPreset target);

  // Sets the active pane index and updates the cached pane_ pointer.
  // Focuses the matching list-view so subsequent F2 / Delete /
  // Ctrl+Shift+N accelerators target the new active pane. No-op when
  // idx is out of range.
  void setActivePane(std::size_t idx);

  // Flips the seam between the two panes while staying in dual mode.
  // No-op when single-mode or when the orientation is already the
  // requested value. Does not destroy or recreate the panes; just
  // updates the cached orientation and triggers a relayout.
  void setLayoutOrientation(LayoutOrientation orientation);

  // The current seam orientation. Always reflects the layout regardless
  // of single/dual mode (used by session capture at WM_DESTROY).
  [[nodiscard]] LayoutOrientation layoutOrientation() const noexcept {
    return orientation_;
  }

  // Applies the persisted window position/size from a prior session.
  // Members equal to kSettingsUseDefault are skipped, so the OS picks
  // the slot for a first run or a corrupted settings file. No-op when
  // the window has not been created yet.
  void applyInitialState(const fast_explorer::core::SessionState& state);

  // Restores the persisted pane layout (Single/Dual) and the second-
  // pane folder from a prior session. Must run after the active pane
  // has been opened on lastPath so the fallback "open pane 1 on pane
  // 0's folder" yields a meaningful path. No-op for Single layout.
  void restoreLayoutFromSession(
      const fast_explorer::core::SessionState& state);

  // The session state observed at WM_DESTROY: pane's current path +
  // restored window rect (via GetWindowPlacement so a minimized
  // shutdown still records the visible position). Returns default
  // sentinels until WM_DESTROY has fired.
  const fast_explorer::core::SessionState& capturedSessionState() const noexcept;

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  LRESULT onCreate(HWND hwnd);
  LRESULT onDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam);
  // A8: query current system dark-mode setting + apply
  // DWMWA_USE_IMMERSIVE_DARK_MODE / DWMWA_SYSTEMBACKDROP_TYPE so
  // the title bar and Mica backdrop track Windows theme. Called
  // on create and on WM_SETTINGCHANGE("ImmersiveColorSet").
  void applySystemTheme();
  LRESULT onSize(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT onCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT onTimer(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT onEnumBatch(WPARAM wParam, LPARAM lParam);
  LRESULT onEnumComplete(WPARAM wParam);
  LRESULT onEnumError(WPARAM wParam, LPARAM lParam);
  LRESULT onSortComplete(WPARAM wParam);
  LRESULT onIconBatch(WPARAM wParam);
  // Active-pane accessors for accelerator-driven actions (F2, Ctrl+
  // Shift+N, sort apply). They return nullptr until onCreate populates
  // the slot — same defensive contract as pane_.
  SelectionSync* activeSelectionSync();
  LabelEditController* activeLabelEdit();
  LRESULT onOperationResult(WPARAM wParam);
  LRESULT onLowMemory();
  LRESULT onFsChange(HWND hwnd, WPARAM wParam);

  // Forces a repaint of every published row of pane `idx` through
  // LVN_GETDISPINFO. Called by the icon-batch and low-memory paths
  // after the coordinator updates the ImageList / extension cache
  // state. No-op when the pane index is out of range or the slot is
  // empty.
  void redrawVisibleRows(std::size_t idx);

  // Wires per-pane coordinators (icon / selection / label-edit) for
  // pane `idx` against the given list-view. Returns false if any
  // coordinator construction failed, leaving partial state for the
  // caller to roll back. Used by onCreate (idx=0) and installPaneAt
  // (idx>=1) so the construction sequence stays in one place.
  bool installPaneCoordinators(std::size_t idx, HWND listView);
  // Creates per-slot UI (listview, toolbar row, address bar, drop
  // target, coordinators) for the slot at `idx`. Assumes the
  // PaneController at idx already exists in paneManager_. Slot 0 is
  // special: its listview is created in onCreate, so installPaneAt(0)
  // is never called. Used by enterLayout when growing pane count.
  bool installPaneAt(std::size_t idx);

  // Tears down per-slot UI for slot `idx` (releases coordinators,
  // destroys listview/toolbar row HWNDs, etc.). Used by enterLayout
  // when shrinking. Slot 0 is never destroyed.
  void uninstallPaneAt(std::size_t idx);
  // Synchronous layout recompute. Equivalent to firing WM_SIZE but
  // avoids the asynchronous message-pump round-trip and the
  // misleading WM_SIZE lParam=0 that the prior implementation
  // posted.
  void relayout();
  LRESULT handleListViewNotify(NMHDR* hdr);
  bool isStaleGeneration(WPARAM wParam) const;
  // Decodes the pane index from `wParam` (packed via makePaneWParam)
  // and returns the corresponding pane, or nullptr if the manager is
  // not yet initialized or the index is out of range.
  PaneController* paneForWParam(WPARAM wParam) const;
  void handleGetDispInfo(NMHDR* hdr);
  void handleGetDispInfoBody(NMHDR* hdr);
  // Rebuilds the LVS_OWNERDATA ListView's group definitions to match
  // the pane's current groupBy. No-op for GroupKey::None (disables
  // group view). Caller invokes after sort completes so visibleOrder
  // is already group-clustered.
  void applyListViewGroups(std::size_t paneIdx);
  void handleColumnClick(NMHDR* hdr);
  void handleItemActivate(NMHDR* hdr);
  // LVN_ODFINDITEM under LVS_OWNERDATA — Win Explorer parity type-to-
  // navigate. Common-controls handles the keystroke accumulator and
  // ~1s reset; we return the row index whose displayed Name column
  // text starts with `lvfi.psz` (case- and width-insensitive), or -1
  // if nothing matches.
  LRESULT handleOdFindItem(NMHDR* hdr);
  // Shared tail of every sort-apply path: refresh the column-header
  // arrow, repaint the selection, and force LVN_GETDISPINFO to fetch
  // cells in the new order. Used by both the synchronous click path
  // and the kWmFeSortComplete handler.
  void finalizeSortApply(std::size_t paneIdx);
  LRESULT handleCustomDraw(NMHDR* hdr);
  void handleListViewRightClick(NMHDR* hdr);
  void handleBeginDrag(NMHDR* hdr);
  void handleClipboardCopy(bool cut);
  void handleClipboardPaste();
  void applyCutStateToListView(std::size_t paneIdx) noexcept;
  void clearCutState() noexcept;
  void handleAddressCommit(std::size_t paneIdx);
  void syncAddressBar(std::size_t paneIdx);
  bool addressBarPaneIndex(HWND ctrl, std::size_t& outIdx) const noexcept;
  bool paneIndexFromListView(HWND lv, std::size_t& outIdx) const noexcept;
  void clearListViewForNavigation(std::size_t paneIdx) noexcept;
  void applyActivePaneAppearance() noexcept;
  void initRatiosToDefaults() noexcept;
  // v0.2.6: applies dark-mode theme classes + colors to every
  // listview alongside the existing active-pane appearance pass.
  // Called from onCreate, applyInitialState, the active-pane
  // toggle, and WM_SETTINGCHANGE so a runtime light↔dark flip
  // takes effect without restart.
  void applyListViewTheme(HWND lv) noexcept;
  // Pushes the pane's canGoBack/Forward/Up to its toolbar so the
  // buttons gray out at history boundaries. Cheap (no enum / no
  // alloc), safe to call from any nav-affecting path.
  void updateNavButtonStates(std::size_t paneIdx) noexcept;
  // T4/T5/T6: show the hamburger popup menu for the given pane. The
  // anchor is the hamburger button HWND so the menu opens flush with
  // its lower-left corner.
  void showToolMenuForPane(std::size_t paneIdx);
  // Queues a recycle-bin delete on the focused list-view row, if any.
  // Bound to the Delete accelerator. No-op when the list has no
  // focused item.
  void deleteFocusedItem();
  static LRESULT CALLBACK addressBarSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                                 UINT_PTR, DWORD_PTR);

  static constexpr const wchar_t* kClassName = L"FastExplorer.MainWindow";
  static constexpr int kDefaultWidth = 1280;
  static constexpr int kDefaultHeight = 800;

  // Writes `text` into the status-bar part owned by `paneIdx`. In
  // single mode the status bar has one part: writes to pane 0 land
  // in it, writes to pane 1 are dropped (the second pane does not
  // exist). In dual mode parts 0 / 1 are the left / right halves.
  void setPaneStatusText(std::size_t paneIdx, const wchar_t* text);

  // Resyncs the status-bar SB_SETPARTS layout with the current pane
  // count + client width. Called by onSize so the part edges follow
  // window resizes in both single and dual mode.
  void applyStatusParts(int clientWidth);

  // Recomputes the selection summary for `paneIdx` and writes it to
  // that pane's status-bar part. Driven by the debounced selection
  // timer in onTimer so a Ctrl+A storm collapses to a single update.
  void refreshSelectionSummary(std::size_t paneIdx);

  // Reads the current SearchPopup text and applies it as a filter
  // to the given pane. Called from the debounced filter timer.
  void applyFilterFromPopup(std::size_t paneIdx);

  fast_explorer::core::ProcessMemoryService& memory_;
  fast_explorer::core::PerfTracker& perf_;
  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  // listViews_[0] is the original list-view created in onCreate; alias
  // listView_ keeps the single-pane code paths unchanged. listViews_
  // [1..3] are created on demand by enterLayout() / installPaneAt()
  // and destroyed by uninstallPaneAt().
  HWND listView_ = nullptr;
  std::array<HWND, 4> listViews_{nullptr, nullptr, nullptr, nullptr};
  HWND statusBar_ = nullptr;
  std::array<HWND, 4> addressBars_{nullptr, nullptr, nullptr, nullptr};
  std::array<HWND, 4> addressDropdownBtns_{nullptr, nullptr, nullptr, nullptr};
  // The toolbar row hosts the nav buttons + address bar + hamburger
  // for each pane; addressBars_[i] is created as a child of
  // paneToolbarRows_[i]->handle() so WM_SIZE on the row positions
  // everything together.
  std::array<std::unique_ptr<PaneToolbarRow>, 4> paneToolbarRows_;
  std::array<IDropTarget*, 4> dropTargets_{nullptr, nullptr, nullptr, nullptr};
  CutStateTracker cutState_;
  std::unique_ptr<AddressBarPopup> addressBarPopup_;
  std::array<bool, 4> firstBatchSeen_{false, false, false, false};
  std::unique_ptr<PaneManager> paneManager_;
  // Cached pointer to the manager's currently active pane. Refreshed
  // by onCreate (and by the active-pane switch handler in a later M9
  // atom). Never owns; the PaneController itself is owned by
  // paneManager_.
  PaneController* pane_ = nullptr;
  std::unique_ptr<class FormatCache> formatCache_;
  // Per-pane coordinators indexed by pane index. Slot 0 is populated
  // by onCreate; slots 1-3 stay empty until the active-pane / multi-
  // pane entry in a later M9 atom creates additional panes.
  std::array<std::unique_ptr<class IconCacheCoordinator>, 4> iconCoords_;
  std::array<std::unique_ptr<SelectionSync>, 4> selectionSyncs_;
  std::array<std::unique_ptr<LabelEditController>, 4> labelEdits_;
  // Per-pane LVS_OWNERDATA group callback. Raw pointer because the
  // listview holds a ref via IListView::SetOwnerDataCallback — we
  // AddRef once at install time and Release once at uninstall. The
  // callback's destructor runs whenever the final ref drops, which
  // may be from comctl32's side after DestroyWindow.
  std::array<ListViewGroupCallback*, 4> groupCallbacks_{
      nullptr, nullptr, nullptr, nullptr};
  std::unique_ptr<class DispInfoHistogram> dispInfoHist_;
  std::uint64_t qpcFrequencyHz_ = 0;
  std::unique_ptr<fast_explorer::core::SessionState> capturedState_;
  LayoutOrientation orientation_ = LayoutOrientation::Vertical;
  fast_explorer::core::LayoutPreset preset_ =
      fast_explorer::core::LayoutPreset::Single;
  fast_explorer::core::LayoutPreset lastDualPreset_ =
      fast_explorer::core::LayoutPreset::Dual_V;
  std::array<fast_explorer::ui::SplitterRatios,
             fast_explorer::core::kLayoutPresetCount> ratiosPerPreset_{};
  std::array<HWND, 3> splitterHwnds_{nullptr, nullptr, nullptr};
  std::unique_ptr<SearchPopup> searchPopup_;
  // v0.2 view toggles. Defaults match SessionState defaults; the real
  // values are populated by applyInitialState from the loaded settings
  // file and re-persisted by the WM_CLOSE handler. T8 wires these into
  // the enumerate + format paths so they actually shape the visible
  // file list.
  bool showHidden_ = false;
  bool showExtensions_ = true;
};

}  // namespace fast_explorer::ui
