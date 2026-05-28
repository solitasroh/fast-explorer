# v0.7.0 Design — Per-Pane Tabs (Browser-Style, Win Explorer Parity)

Status: draft for review
Date: 2026-05-28
Source: brainstorming session 2026-05-28
Prior art: `docs/superpowers/specs/2026-05-20-multi-pane-splitter-design.md` (v0.4.0 multi-pane), `docs/handoffs/2026-05-28_winui-lite-refactor-shipped.md` (winui_lite refactor)

## Goal

Add browser-style tabs to each pane so a user can keep several folders open per pane and switch between them with Win Explorer-parity keyboard, mouse, and visual affordances. Tabs are eager (background tabs keep their state live, matching Win Explorer). The feature exploits the winui_lite refactor: the tab strip lives in `lib/winui_lite/widgets/`, the shell-bound tab coordination lives in `src/explorer/`, and the existing per-pane adapter array continues to point at one PaneController each — through a new indirection cell that the active-tab switch updates.

User request (paraphrased from 2026-05-28 handoff): "Tabs feature, per-pane, browser-style. PaneController is already structured to be the tab body."

## Decisions made in brainstorming

| Item | Decision |
|---|---|
| Tab strip placement | Per-pane, above the existing pane toolbar row |
| Keyboard | `Ctrl+T` new tab, `Ctrl+W` close, `Ctrl+Tab` / `Ctrl+Shift+Tab` cycle. Layout presets keep `Ctrl+1..4`. No `Ctrl+digit` tab-jump. |
| Close affordances | Full Win Explorer parity — hover X button + middle-click + `Ctrl+W` + right-click menu ("Close tab" / "Close other tabs" / "Close tabs to the right") |
| New tab default folder | Home = `%USERPROFILE%` (`SHGetKnownFolderPath(FOLDERID_Profile)`), fallback `C:\` on failure |
| Drag-to-reorder | Yes, in v1. Drop-on-position only, no live slide animation |
| Background tab lifecycle | Eager — every tab keeps its `PaneController` (FsWatcher + store + worker) alive. Tab switch = swap which store the listview points at |
| "Open in new tab" trigger | Middle-click on folder row + listview right-click menu entry. `Ctrl+click` keeps multi-select toggle semantics. |
| Adapter ↔ PaneController rewiring | Indirection cell — `MainWindow` owns `std::array<PaneController*, 4> activeForPane_`. Each adapter takes a `PaneController* const&` reference to its pane's cell. Tab switch writes the cell once; adapter calls dereference per invocation. |
| Cross-pane tab drag | Not in v1. Same-pane reorder only. |
| Last tab in a pane closed | Tab is reset to Home, pane stays open. Tabs vector never reaches size 0. |
| Closed-tab history (Ctrl+Shift+T) | Not in v1. No schema field reserved. |

## Architecture

### Component responsibilities

| Component | Responsibility | v0.7.0 change |
|---|---|---|
| `PaneController` | Owns one folder's state — currentPath, history, FsWatcher, store, sort/group/filter, ShellWorker | Unchanged. Becomes the "tab body" the v0.4.0 design called out. |
| `PaneManager<TPane>` | Owns the per-app pane slots (used by `winui_lite_demo`) | Unchanged. **Not used by `MainWindow` anymore** — `PaneTabHost`'s constructor takes three args (MainWindow*, paneIdx, cell&), which doesn't match `PaneManager`'s `TPane(HWND, size_t)` requirement. `MainWindow` manages `paneTabHosts_` and `activePane_` directly. The template stays in `lib/` for the demo. |
| `TabStrip` (new) | Win32 child window — owner-drawn tabs, hit-test, hover X, middle-click, drag-to-reorder, chevron overflow | New file `lib/winui_lite/widgets/tab-strip.{h,cpp}`. Shell-unaware. |
| `PaneTabHost` (new) | Per-pane coordinator — owns the tabs vector, drives TabStrip, updates the indirection cell, exposes new-tab / close-tab / move-tab / cycle commands | New file `src/explorer/pane-tab-host.{h,cpp}`. Shell-aware. |
| `MainWindow` | Window/message routing | Slot arrays of `unique_ptr<PaneController>` replaced by `unique_ptr<PaneTabHost>`. Adds `activeForPane_` cell array. Adapter construction takes the cell. Accel router gains Ctrl+T / Ctrl+W / Ctrl+Tab / Ctrl+Shift+Tab. Listview WM_MBUTTONUP + context menu entry route "open in new tab". |
| Shell adapters (6) | Adapt ports to shell + PaneController | Constructor takes `PaneController* const&` cell reference (was: `PaneController&`). Method bodies dereference the cell. |
| `settings-store` | Session persistence | Schema v5 → v6 with migration. `pane_paths[4]` → `panes[4].tabs[].path` + `panes[i].active_tab`. |

### winui_lite isolation rules (enforced)

- `TabStrip` lives in `lib/winui_lite/widgets/` and links no shell headers, references no shell tokens. Verified at build by `cmake/check-winui-lite-isolation.cmake`.
- `TabStrip` consumes a `TabModel` view-model and raises callbacks. It does not know what a "folder" is.
- `winui_lite_demo` adds an in-memory `MockTabHost` to show the strip working without shell.

## Components

### `TabStrip` (lib/winui_lite/widgets/tab-strip.h)

```cpp
namespace fast_explorer::ui {

struct TabModel {
  std::wstring title;     // host supplies — folder leaf, "Loading…", "Home"
  bool hasCloseX{true};
};

class TabStrip {
 public:
  TabStrip(HWND parent, std::size_t paneIdx);
  ~TabStrip();
  TabStrip(const TabStrip&) = delete;
  TabStrip& operator=(const TabStrip&) = delete;

  HWND handle() const noexcept;

  // Replace the full model. Called on tab open/close/reorder.
  void setTabs(std::span<const TabModel> models);
  void setActive(std::size_t idx);

  // Preferred height for layout (uses system metrics + DPI).
  int preferredHeight() const;

  // Host callbacks. Set once at construction time by the host.
  std::function<void(std::size_t)> onActivate;
  std::function<void(std::size_t)> onClose;
  std::function<void()> onNew;                       // "+" button click
  std::function<void(std::size_t /*from*/,
                     std::size_t /*to*/)> onReorder;
  std::function<void(std::size_t /*idx*/,
                     POINT /*screen*/)> onContextMenu;

 private:
  // owner-drawn paint state, drag-state, chevron scroll offset, ...
};

}  // namespace fast_explorer::ui
```

Implementation notes:
- Owner-drawn. Reuses `dark-scrollbar-hook` + `theme-watcher` for dark-mode parity.
- Drag tracking — capture on `WM_LBUTTONDOWN` after pixel threshold (e.g. 6 DIP). `WM_MOUSEMOVE` updates a drop-indicator line. `WM_LBUTTONUP` computes target index from mouse-x relative to tab boundaries and fires `onReorder(from, to)` if `from != to`. Tab visually stays in place during drag — only the drop indicator moves.
- Middle-click (`WM_MBUTTONUP`) on a tab fires `onClose(idx)`. On the strip background (no tab hit), no-op.
- Hover X — separate hit region inside each tab rect, drawn on hover (per Win11). Click fires `onClose`.
- Chevron overflow — when total tab width > strip width, draw left/right chevrons that scroll the visible window. Scroll state is local to the strip.
- "+" button — fixed at right edge after the last visible tab (or always visible if chevrons are present). Click fires `onNew`.
- `onContextMenu(idx, screen)` is raised on `WM_RBUTTONUP` over a tab. The host paints the menu — the strip never has shell vocabulary.

### `PaneTabHost` (src/explorer/pane-tab-host.h)

```cpp
namespace fast_explorer::ui {

class PaneTabHost {
 public:
  PaneTabHost(MainWindow* host,
              std::size_t paneIdx,
              PaneController*& activeCell);

  ~PaneTabHost();

  // Commands
  void openNewTab();                                // → Home
  void openInNewTab(const std::wstring& path);      // background
  void closeTab(std::size_t idx);
  void closeOtherTabs(std::size_t keepIdx);
  void closeTabsToRight(std::size_t idx);
  void activateTab(std::size_t idx);
  void moveTab(std::size_t from, std::size_t to);
  void cycleNext();
  void cyclePrev();

  // Session
  void restoreFromSession(const core::PaneSessionV6& panel);
  core::PaneSessionV6 captureSession() const;

  // Inspect
  std::size_t tabCount() const noexcept;
  std::size_t activeTabIdx() const noexcept;
  PaneController& activeTab() noexcept;             // *activeCell_
  PaneController& tabAt(std::size_t idx) noexcept;

  HWND stripHandle() const noexcept;
  int stripPreferredHeight() const;

 private:
  void rebuildStrip();                              // setTabs from tabs_
  std::wstring homeFolder() const;

  MainWindow* host_;
  std::size_t paneIdx_;
  PaneController*& activeCell_;
  std::vector<std::unique_ptr<PaneController>> tabs_;
  std::size_t activeTab_{0};
  std::unique_ptr<TabStrip> strip_;
};

}  // namespace fast_explorer::ui
```

### `MainWindow` (src/explorer/main-window.h) changes

```diff
- std::array<std::unique_ptr<PaneController>, 4>  paneControllers_;
- PaneController*                                 pane_;
+ std::array<std::unique_ptr<PaneTabHost>, 4>     paneTabHosts_;
+ std::array<PaneController*, 4>                  activeForPane_{};
+ PaneController*                                 pane_;   // cached *activeForPane_[activePane_]

  std::array<std::unique_ptr<adapters::ShellItemSource>, 4>     itemSources_;
  std::array<std::unique_ptr<adapters::ShellItemDispatcher>, 4> itemDispatchers_;
  std::array<std::unique_ptr<adapters::ShellClipboard>, 4>      clipboards_;
  std::array<std::unique_ptr<adapters::ShellDragDrop>, 4>       dragDrops_;
  std::array<std::unique_ptr<adapters::ShellContextMenuAdapter>, 4> contextMenus_;
  // LocalSettingsStore is window-scoped, unchanged
```

Adapter constructor pattern:

```cpp
ShellItemSource(PaneController* const& activeCell, HWND listView, ...);
// stored internally as:
//   PaneController* const* cell_ = std::addressof(activeCell);
```

Constructor parameter is a reference for caller ergonomics; the adapter stores the address of the cell so it survives moves of the adapter and binds once. Method bodies:

```cpp
void ShellItemSource::activateRow(std::uint32_t row) {
  PaneController* c = *cell_;
  if (!c) return;                     // defensive — pane teardown only
  c->openItem(row);
}
```

The cell itself (`activeForPane_[paneIdx]`) is owned by MainWindow and lives as long as the pane slot does. Adapter destruction happens before pane teardown nulls the cell — see `uninstallPaneAt` ordering below.

New accelerator ids in `src/explorer/messages.h`:

```cpp
inline constexpr WORD kAccelNewTab        = 124;  // Ctrl+T
inline constexpr WORD kAccelCloseTab      = 125;  // Ctrl+W
inline constexpr WORD kAccelTabCycleNext  = 126;  // Ctrl+Tab
inline constexpr WORD kAccelTabCyclePrev  = 127;  // Ctrl+Shift+Tab
```

### `installPaneAt` / `uninstallPaneAt` flow

```cpp
void MainWindow::installPaneAt(std::size_t i) {
  // 1. create the tab host. it constructs its first PaneController at Home
  //    and writes the cell.
  paneTabHosts_[i] =
      std::make_unique<PaneTabHost>(this, i, activeForPane_[i]);
  paneTabHosts_[i]->restoreFromSession(capturedState_->panes[i]);

  // 2. create per-slot UI (listview, toolbar row, address bar, drop target,
  //    iconCoord, selectionSync, labelEdit) — same as today
  // 3. construct the 6 adapters with the cell reference
  itemSources_[i]    = std::make_unique<adapters::ShellItemSource>(
                          activeForPane_[i], listViews_[i], ...);
  itemDispatchers_[i] = std::make_unique<adapters::ShellItemDispatcher>(
                          activeForPane_[i], ...);
  // ... same pattern for clipboards_, dragDrops_, contextMenus_
  // 4. position the tab strip above the toolbar row
}

void MainWindow::uninstallPaneAt(std::size_t i) {
  // adapters reset first (they reference the cell)
  contextMenus_[i].reset();
  dragDrops_[i].reset();
  clipboards_[i].reset();
  itemDispatchers_[i].reset();
  itemSources_[i].reset();
  // then the host (and its tabs)
  paneTabHosts_[i].reset();
  activeForPane_[i] = nullptr;
}
```

### Layout integration

The pane layout engine (`pane-layout.h/cpp`) already treats each slot as a rect. The tab strip's height is added to the slot's chrome height alongside the existing toolbar row. Update path:

- `MainWindow::layoutPane(i)` reserves `stripHeight + toolbarRowHeight` at the top of slot i and positions listview below. Strip height comes from `paneTabHosts_[i]->stripPreferredHeight()`.
- DPI-change handler re-reads `preferredHeight()` from each strip.

## Data flow

### Session schema v6

`docs/superpowers/specs/2026-05-22-file-grouping-design.md`-style flat JSON. The shape:

```json
{
  "schema_version": 6,
  "window_x": 100, "window_y": 100, "window_w": 1600, "window_h": 1000,
  "preset": "dual_v",
  "ratios": [...],
  "pane_count": 2,
  "active_pane": 0,
  "panes": [
    {
      "tabs": [
        { "path": "C:\\Users\\me\\Docs" },
        { "path": "D:\\proj" }
      ],
      "active_tab": 0
    },
    {
      "tabs": [ { "path": "C:\\Users\\me" } ],
      "active_tab": 0
    }
  ],
  "view_show_hidden": 0,
  "view_show_extensions": 1
}
```

- `panePaths[4]` field is removed.
- Per-tab data is **just** `{ "path": ... }` in v6. Scroll position, selection, sort-spec per-tab are out of scope — they live in process memory and reset on restart, same as today.

### Migrator v5 → v6

```cpp
// pseudo
for (size_t i = 0; i < state.paneCount; ++i) {
  PaneSessionV6 p;
  p.tabs.push_back({ state.panePaths[i] });
  p.activeTab = 0;
  state.panes[i] = std::move(p);
}
```

Forward-compat rules:
- `panes` length `<` `pane_count` → missing slots filled with a single-tab Home pane.
- `panes` length `>` `pane_count` → trailing entries ignored.
- `active_tab` ≥ `tabs.size()` → clamped to `tabs.size() - 1`.
- `tabs` empty → replaced with a single-tab Home pane.
- Malformed JSON → fall through to all-defaults same as v5.

### Tab activation flow

```
TabStrip.onActivate(j) → PaneTabHost::activateTab(j)
  ├─ activeCell_ = tabs_[j].get();                     // O(1) — adapter rebind
  ├─ host_->bindListViewToStore(paneIdx_,
  │      tabs_[j]->store(), tabs_[j]->generation());
  ├─ host_->refreshAddressBar(paneIdx_,
  │      tabs_[j]->currentPath());
  ├─ host_->refreshNavButtons(paneIdx_,
  │      tabs_[j]->canGoBack(),
  │      tabs_[j]->canGoForward(),
  │      tabs_[j]->canGoUp());
  ├─ host_->refreshStatusBar(paneIdx_);
  └─ strip_->setActive(j);
```

`bindListViewToStore` is a new MainWindow helper that swaps the per-pane listview's virtual-mode data source without `LVM_DELETEALLITEMS` (it calls `ListView_SetItemCountEx` with `LVSICF_NOSCROLL` to preserve scroll where possible, or with `LVSICF_NOINVALIDATEALL` for clean swap — implementation chooses).

### Ctrl+T (new tab) flow

```
accelRouter_(kAccelNewTab)
  → paneTabHosts_[activePane_]->openNewTab()
      ├─ tabs_.push_back(make_unique<PaneController>(host_->hwnd(), paneIdx_));
      ├─ tabs_.back()->openFolder(homeFolder());
      ├─ rebuildStrip();
      └─ activateTab(tabs_.size() - 1);
```

`homeFolder()` resolves once and caches:

```cpp
std::wstring PaneTabHost::homeFolder() const {
  PWSTR p = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &p))) {
    std::wstring out{p};
    CoTaskMemFree(p);
    return out;
  }
  return L"C:\\";
}
```

### "Open in new tab" flow (middle-click + context menu)

Listview WM_MBUTTONUP handler in MainWindow:

```cpp
LRESULT MainWindow::onListViewMButtonUp(std::size_t paneIdx, LPARAM lParam) {
  LVHITTESTINFO ht{};
  ht.pt.x = GET_X_LPARAM(lParam);
  ht.pt.y = GET_Y_LPARAM(lParam);
  ListView_HitTest(listViews_[paneIdx], &ht);
  if (ht.iItem < 0) return 0;

  PaneController& pane = paneTabHosts_[paneIdx]->activeTab();
  const auto& store    = pane.store();
  const std::uint32_t raw = store.visibleOrder()[ht.iItem];
  const auto& entry    = store.entryAt(raw);
  if (!core::isDirectory(entry)) return 0;

  std::wstring abs = core::joinPath(pane.currentPath(),
                                    core::nameView(entry));
  paneTabHosts_[paneIdx]->openInNewTab(abs);
  return 0;
}
```

`openInNewTab` is identical to `openNewTab` except: (a) initial path is the supplied folder, (b) activeTab does **not** switch — the new tab is opened in background. Win parity.

Right-click context menu adds an "Open in new tab" item at the top when the row is a folder. The shell context menu adapter prepends this app-owned verb before deferring to `IContextMenu::QueryContextMenu`.

### Close tab flow

```
TabStrip.onClose(j)  OR  Ctrl+W  OR  WM_MBUTTONUP on a tab
  → PaneTabHost::closeTab(j)
      ├─ if (tabs_.size() == 1) {
      │     tabs_[0]->openFolder(homeFolder());      // last tab → Home reset
      │     // activeTab_ stays 0
      │  } else {
      │     bool wasActive = (j == activeTab_);
      │     tabs_.erase(tabs_.begin() + j);
      │     if (j < activeTab_) --activeTab_;
      │     else if (wasActive)
      │       activeTab_ = std::min(activeTab_, tabs_.size() - 1);
      │  }
      ├─ rebuildStrip();
      └─ activateTab(activeTab_);
```

### Right-click tab menu

```
TabStrip.onContextMenu(j, screen)
  → MainWindow::showTabContextMenu(paneIdx, j, screen)
      ├─ Popup with:
      │    Close tab            (always)
      │    Close other tabs     (greyed when tabs.size() == 1)
      │    Close tabs to right  (greyed when j == tabs.size() - 1)
      └─ Selection invokes the matching PaneTabHost method
```

### Reorder

```
TabStrip.onReorder(from, to)
  → PaneTabHost::moveTab(from, to)
      ├─ auto p = std::move(tabs_[from]);
      ├─ tabs_.erase(tabs_.begin() + from);
      ├─ tabs_.insert(tabs_.begin() + to, std::move(p));
      ├─ activeTab_ adjustment:
      │     if activeTab_ == from           → activeTab_ = to
      │     elif from < activeTab_ <= to   → --activeTab_
      │     elif to <= activeTab_ < from   → ++activeTab_
      │     else                            → unchanged
      └─ rebuildStrip();
```

`activeForPane_` cell does not change because the active `PaneController*` is unaffected by a reorder.

### Cycle (Ctrl+Tab / Ctrl+Shift+Tab)

```cpp
void PaneTabHost::cycleNext() {
  activateTab((activeTab_ + 1) % tabs_.size());
}
void PaneTabHost::cyclePrev() {
  activateTab((activeTab_ + tabs_.size() - 1) % tabs_.size());
}
```

Accel router only fires on the active pane.

## Error handling and edge cases

| Scenario | Behavior |
|---|---|
| `openFolder` returns false during tab creation | Tab is **not** pushed onto `tabs_`. Strip unchanged. No user-facing error toast — Ctrl+T uses a known-good path; "open in new tab" uses a path resolved from an existing entry. |
| `SHGetKnownFolderPath(FOLDERID_Profile)` fails | Fallback to `C:\`. Tab is still created. |
| Layout preset shrinks (Ctrl+1 from Quad) | Closing panes' `PaneTabHost`s are destroyed. Their tab vectors disposed. Settings capture happens **before** destruction so closed panes' tabs persist to disk. Subsequent expansion restores them. (Same semantics as v5 — closed pane state survives between layout cycles within a session.) |
| Two tabs on the same folder | Two `PaneController`s with independent selection/scroll/history. Win parity. |
| Long-running enumerate in a background tab | jthread runs to completion. Only the foreground tab's listview is bound; background tabs' results land in their own stores and are visible when the user switches. |
| OLE drop in progress when user clicks another tab | `TabStrip::setActive` is **deferred** until OLE drop completes. Implementation: `PaneTabHost::activateTab` checks `MainWindow::isOleDragInProgress(paneIdx_)`; if true, the call is queued (single-pending slot — newer requests overwrite older) and replayed from `IDropTarget::Drop`'s tail. Simple and avoids the mid-drop adapter-rebind hazard. |
| Cross-pane tab drag | Not supported in v1. TabStrip drag tracking is bounded to its own client rect; mouse leaving the strip cancels the drag (no drop). |
| Schema migration: `panes` length mismatches `pane_count` | Lenient — missing slots filled with Home tab, extra slots ignored, `active_tab` clamped. |
| Adapter call with `*activeCell_ == nullptr` (theoretical, during teardown) | Adapter early-returns the appropriate "no-op" value (E_FAIL for OLE, false for boolean, 0 for counters). Defensive only. |
| Middle-click on a file (not folder) | No-op. Win parity — Win Explorer does nothing on file middle-click in listview. |
| Middle-click on a tab | Same as the X button — `onClose(idx)`. |
| Tab strip too narrow for "+" button | "+" still drawn; chevrons scroll the tab area, "+" stays anchored at right. |
| 100+ tabs in a pane | Strip overflows with chevrons. No max-tab cap. `kMaxSettingsBytes = 64 KB` may eventually truncate save; in that case the save fails and next startup falls back to default (v6 with default Home pane). No explicit error UI. |
| OLE drop target lifetime across tab switch | `RegisterDragDrop` is bound to the listview HWND (pane-scoped). The drop target adapter holds the cell reference; tab switch only changes which `PaneController` receives `Drop`. No `RevokeDragDrop` / re-register on tab switch. |

## Testing

### `winui_lite_tests` (chrome-only, no shell)

- **TabStrip — model rendering**
  - `setTabs({A, B, C})` → 3 hit-test regions, widths match preferred metric, active highlight on idx 0 (default).
  - `setActive(2)` → active highlight moves.
- **TabStrip — close affordances**
  - Middle-click over tab idx 1 → `onClose(1)`.
  - Middle-click on strip background → no callback.
  - Synthesized `WM_MOUSEMOVE` to hover tab 0 → X region becomes hit-testable; left-click there → `onClose(0)`.
- **TabStrip — reorder**
  - Mouse-down on tab 1, move horizontally past tab 0's midpoint, mouse-up → `onReorder(1, 0)`.
  - Mouse-down + move under threshold + mouse-up → no callback.
  - Mouse-down + mouse-leave-without-up → drag cancelled, no callback.
- **TabStrip — overflow**
  - Force narrow width with many tabs → chevrons appear; chevron click scrolls; "+" stays anchored.
- **TabStrip — dark mode visual**
  - Toggle dark mode; spot-check colors via theme-watcher harness already in `winui_lite_tests`.

### `core-tests` (PaneTabHost + adapter rewire)

- **PaneTabHost** with a mock `MainWindow*` (callback observer)
  - `openNewTab()` increments tabs, sets active to new idx, writes cell.
  - `closeTab(0)` with tabs.size() == 1 → tabs.size() stays 1, openFolder(home) was called, cell unchanged.
  - `closeTab(j)` with j == activeTab and j > 0 → activeTab becomes j - 1.
  - `closeTab(j)` with j < activeTab → activeTab decrements.
  - `closeOtherTabs(j)` → tabs.size() == 1, the surviving tab is the one previously at j.
  - `closeTabsToRight(j)` → tabs.size() == j + 1.
  - `moveTab(2, 0)` with activeTab == 2 → activeTab tracks the moved controller (becomes 0).
  - `cycleNext` last → 0 wrap; `cyclePrev` 0 → last wrap.
- **Adapter cell semantics**
  - Construct each of the 6 adapters with a `PaneController*` cell pointing at controller A.
  - Update the cell to controller B; call an adapter method that performs a state-querying op → observe B was hit, not A.
  - Set cell to nullptr; call adapter method → no crash, returns the documented sentinel (E_FAIL / false / 0).

### `settings-store-tests`

- v5 → v6 migration round trip — 1, 2, 3, 4-pane v5 configs migrate to expected v6 shape.
- v6 round trip — save then load returns identical state (deep equality).
- Malformed v6:
  - `panes` shorter than `pane_count` → missing slots get Home tab.
  - `panes` longer than `pane_count` → trailing dropped.
  - `tabs` empty → replaced with Home tab.
  - `active_tab` >= `tabs.size()` → clamped.
- 64 KB cap exceeded (synthetic): save returns false, next load uses defaults.

### Manual UI checklist (run before tagging)

- `Ctrl+T` from Single → Home tab spawns, becomes active.
- `Ctrl+W` on the only tab → tab resets to Home (path becomes `%USERPROFILE%`); pane stays open.
- `Ctrl+W` on a non-last tab → tab closes, neighbor becomes active.
- `Ctrl+Tab` / `Ctrl+Shift+Tab` cycle wrap.
- Middle-click on a folder row → background tab opens (active stays put).
- Middle-click on a file row → nothing happens.
- Middle-click on a tab → tab closes.
- Hover a tab → X button appears; click closes.
- Right-click a tab → menu with three entries; greyed-out states correct.
- Drag a tab to a new position → drop indicator follows mouse; drop reorders; no slide animation.
- Drag tab and release outside the strip → no reorder.
- Quad layout + 3 tabs in each pane → memory usage observed; tab switch latency < 50 ms (eager).
- Boot with a v5 `settings.json` → migration runs once; subsequent boots see v6.
- Dark-mode toggle with tabs open → strip recolors correctly.
- DPI change with tabs open → strip preferred height updates; layout repacks.
- Active pane switch (alt-click into another pane) → that pane's accel router scope (Ctrl+T/W/Tab) applies to the newly active pane.
- OLE drop into a listview while tabs are switchable — confirm tab-click during drop is queued, not interleaved.

### `winui_lite_demo`

- Demo gains a "tabs demo" view that constructs a `TabStrip` and an in-memory `MockTabHost` with 3 mock tabs. Demonstrates the strip working without any shell linkage.

### Regression guards

- All existing `core-tests` pass after adapter signature updates.
- Existing 86 `winui_lite_tests` pass; new TabStrip cases added on top.
- `cmake/check-winui-lite-isolation.cmake` still passes — TabStrip uses no shell tokens.

## Out of scope (v1)

- Closed-tab history / `Ctrl+Shift+T`.
- Cross-pane tab drag.
- Tab rename / pinned tabs / tab groups / tab color.
- Per-tab persisted scroll position, selection, sort/group/filter state.
- Right-click context-menu entries beyond Close tab / Close other tabs / Close tabs to the right.
- Detach tab into a new window.
- Background-tab UI hint when an `FsWatcher` change fires in an inactive tab (e.g. dirty dot).

## Open follow-ups (after v1 ships)

- Surface "Home" as a navigation target elsewhere (toolbar button, address-bar shortcut, default for the address bar's empty-input case).
- Pre-existing address-bar bug (typing a syntactically valid but non-existent absolute path clears the listview) — flagged in the v0.6.x handoff. The tab close → Home reset path makes this less visible but does not fix the root cause.

## Backwards compatibility

- v5 `settings.json` files migrate cleanly to v6 on first load. No user action required.
- v4 / pre-v4 files migrate v4 → v5 (existing) → v6 (new).
- Downgrade from v6 to a v5 build: v5 reader sees an unknown `schema_version`, falls back to defaults (lenient v5 behavior). Tabs are lost. Acceptable — v5 has no tabs anyway.

## Build sequence (preview, full plan in writing-plans phase)

1. Schema v6 + migrator + tests.
2. `TabStrip` widget + winui_lite_tests.
3. `PaneTabHost` + unit tests with mock host.
4. Adapter signature change to `PaneController* const&` + adapter unit tests + fixture updates.
5. `MainWindow` wiring — slot arrays, `activeForPane_` cell, layout integration.
6. Accelerators (Ctrl+T / W / Tab / Shift+Tab).
7. Listview middle-click + context-menu "Open in new tab".
8. Tab right-click context menu.
9. Drag-to-reorder.
10. Demo updates.
11. Manual UI sweep + Quad eager-cost measurement.
