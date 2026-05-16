#pragma once

#include <windows.h>

#include <memory>
#include <string>

namespace fast_explorer::core {
class ProcessMemoryService;
class PerfTracker;
struct FileEntry;
}

namespace fast_explorer::ui {

class DispInfoHistogram;
class PaneController;

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

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  // Per-message handlers split out of handleMessage so the dispatch
  // switch reads as a flat routing table. Each helper returns the
  // value handleMessage should return for that case. Argument policy:
  // handlers that can fall back to DefWindowProcW (standard WM_*)
  // take (HWND, UINT, WPARAM, LPARAM) so msg can be replayed; the
  // kWmFe* user-message handlers take only the params they actually
  // need.
  LRESULT onCreate(HWND hwnd);
  LRESULT onDpiChanged(HWND hwnd, WPARAM wParam, LPARAM lParam);
  LRESULT onSize(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT onCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT onTimer(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT onEnumBatch(WPARAM wParam, LPARAM lParam);
  LRESULT onEnumComplete(WPARAM wParam);
  LRESULT onEnumError(WPARAM wParam, LPARAM lParam);
  LRESULT onSortComplete(WPARAM wParam);
  LRESULT onIconBatch();
  LRESULT onOperationResult();
  LRESULT onLowMemory();
  LRESULT onFsChange(HWND hwnd);

  // Forces a repaint of every published row through LVN_GETDISPINFO.
  // Called by the icon-batch and low-memory paths after the
  // coordinator updates the ImageList / extension cache state.
  void redrawVisibleRows();

  LRESULT handleListViewNotify(NMHDR* hdr);
  bool isStaleGeneration(WPARAM wParam) const;
  void handleGetDispInfo(NMHDR* hdr);
  void handleGetDispInfoBody(NMHDR* hdr);
  void handleColumnClick(NMHDR* hdr);
  void handleItemActivate(NMHDR* hdr);
  void handleItemChanged(NMHDR* hdr);
  void reapplySelectionFromPane();
  // Shared tail of every sort-apply path: refresh the column-header
  // arrow, repaint the selection, and force LVN_GETDISPINFO to fetch
  // cells in the new order. Used by both the synchronous click path
  // and the kWmFeSortComplete handler.
  void finalizeSortApply();
  LRESULT handleCustomDraw(NMHDR* hdr);
  void handleAddressCommit();
  // Queues a recycle-bin delete on the focused list-view row, if any.
  // Bound to the Delete accelerator. No-op when the list has no
  // focused item.
  void deleteFocusedItem();
  // Starts an in-place edit on the focused list-view row. Bound to
  // the F2 accelerator. The actual rename is dispatched from
  // handleEndLabelEdit when the user commits.
  void beginRenameFocusedItem();
  LRESULT handleBeginLabelEdit();
  LRESULT handleEndLabelEdit(NMHDR* hdr);
  // Bound to Ctrl+Shift+N. Queues the create, then sets
  // pendingEditFolderName_ so the next onEnumComplete can start
  // an in-place edit on the new row.
  void beginCreateSubfolder();
  void maybeStartPendingFolderEdit();
  static LRESULT CALLBACK addressBarSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                                 UINT_PTR, DWORD_PTR);

  static constexpr const wchar_t* kClassName = L"FastExplorer.MainWindow";
  static constexpr int kDefaultWidth = 1280;
  static constexpr int kDefaultHeight = 800;

  void setStatusText(const wchar_t* text);

  fast_explorer::core::ProcessMemoryService& memory_;
  fast_explorer::core::PerfTracker& perf_;
  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND listView_ = nullptr;
  HWND statusBar_ = nullptr;
  HWND addressBar_ = nullptr;
  bool firstBatchSeen_ = false;
  // Empty when no create is in flight. Cleared by the next
  // onEnumComplete so a later unrelated refresh cannot trigger
  // the auto-edit.
  std::wstring pendingEditFolderName_;
  // Reentrancy guard for our own SetItemState calls inside
  // reapplySelectionFromPane(): the list-view fires LVN_ITEMCHANGED
  // for every state change including the ones we drive, and routing
  // those back into PaneController would clobber the selection we
  // just restored.
  bool reapplyingSelection_ = false;
  std::unique_ptr<PaneController> pane_;
  std::unique_ptr<class FormatCache> formatCache_;
  std::unique_ptr<class IconCacheCoordinator> iconCoord_;
  std::unique_ptr<class DispInfoHistogram> dispInfoHist_;
  std::uint64_t qpcFrequencyHz_ = 0;
};

}  // namespace fast_explorer::ui
