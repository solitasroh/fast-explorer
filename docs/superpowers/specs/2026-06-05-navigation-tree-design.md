# v0.8.1 Design — Left Navigation Tree

Status: draft for review
Date: 2026-06-05
Source: brainstorming session 2026-06-05
Prior art:
- `docs/superpowers/specs/2026-05-20-multi-pane-splitter-design.md`
- `docs/superpowers/specs/2026-05-28-tabs-design.md`
- `src/explorer/address-bar-popup.{h,cpp}` Shell TreeView implementation

## Goal

Add a collapsible left navigation tree that stays synchronized with the
active pane's active tab. The tree gives the user a persistent Explorer-like
orientation surface for `This PC`, `Network`, Network Shortcuts, common Shell
locations, mapped drives, UNC targets, and normal folders without adding one
tree per pane.

User request, paraphrased: "For 0.8.1, add a left tree that can be hidden or
shown, synchronized with the active tab path."

## Decisions

| Item | Decision |
|---|---|
| Placement | One window-level left tree, outside the pane grid. It applies to the active pane and active tab. |
| Visibility | Toggleable. Default visible on fresh installs. Persisted across restarts. |
| Width | User-resizable with a vertical splitter. Persisted across restarts. |
| Keyboard | `Ctrl+B` toggles the tree. The hamburger menu also exposes "Navigation tree". |
| Click behavior | Selecting a concrete node navigates the active pane's active tab. |
| Sync direction | Active tab navigation updates the tree selection; tree selection navigates the active tab. |
| Multi-pane behavior | The tree follows `activePane_`. Switching active pane or tab reselects that path in the tree. |
| Shell support | Root nodes include Home, Gallery, Desktop, Downloads, Documents, Pictures, Music, Videos, user profile, This PC, Network, and Recycle Bin where Windows resolves them. |
| File operations | The tree is navigation-only in v0.8.1. No delete, rename, copy, paste, drag, or context menu inside the tree. |
| Deferred | Favorites/pins, tree search, drag/drop, per-pane trees, and custom ordering are out of scope for v0.8.1. |

## UX

The tree should feel like a quiet navigation rail, not another file pane.
It uses the same system image list and theme colors as the address-bar popup
TreeView. It does not show explanatory in-app text. Empty or unresolvable Shell
branches simply remain collapsed or show no children.

Layout:

```text
+----------------------+-----------------------------------------------+
| Navigation tree      | Pane grid                                      |
|                      |  tab strip / toolbar / address                 |
| Home                 |  list view                                     |
| Gallery              |                                               |
| Desktop              |                                               |
| This PC              |                                               |
|   C:\                |                                               |
| Network              |                                               |
+----------------------+-----------------------------------------------+
| Status bar spans the full window width                                |
+------------------------------------------------------------------------+
```

When hidden, the pane grid takes the full width. When visible, the tree takes
`navigationTreeWidthPx` plus a small splitter hit area. The status bar remains
full-width because it already reports active pane state and should not be
visually owned by the navigation tree.

## Architecture

### Component responsibilities

| Component | Responsibility | v0.8.1 change |
|---|---|---|
| `ShellTreeController` | Shared TreeView core: PIDL storage, roots, lazy expansion, selection, theme, icon image list, and node-to-location conversion. | New `src/explorer/shell-tree-controller.{h,cpp}` extracted from `AddressBarPopup`. |
| `AddressBarPopup` | Popup window lifetime, anchoring, mouse hook, and forwarding selected locations to `MainWindow`. | Keeps popup-specific code and delegates tree population/selection to `ShellTreeController`. |
| `NavigationTreePane` | Persistent child window that hosts a `WC_TREEVIEWW`, exposes show/hide, resize, sync, and pick callbacks. | New `src/explorer/navigation-tree-pane.{h,cpp}`. |
| `MainWindow` | Owns global tree visibility, layout reservation, active pane/tab sync, command routing, and settings capture/restore. | Adds tree member, splitter handling, `Ctrl+B`, menu item, and sync calls. |
| `SettingsStore` | Persists navigation tree visibility and width. | Schema v7 to v8 migration with defaults. |
| Tests | Pure behavior and session tests for visibility, width clamp, sync routing, and settings. | Adds focused unit tests; GUI smoke verifies actual TreeView rendering. |

### New shared tree core

`AddressBarPopup` already contains the hard part: Shell PIDLs, lazy expansion,
system icons, theme application, and `addressLocationForPidl`. v0.8.1 should
move the reusable pieces into `ShellTreeController` so the popup and the left
tree do not drift.

```cpp
namespace fast_explorer::ui {

struct ShellTreeSelection {
  std::wstring location;  // Serialized location, e.g. C:\, shell:ThisPC, \\server
};

class ShellTreeController {
 public:
  ShellTreeController() = default;
  ~ShellTreeController();

  ShellTreeController(const ShellTreeController&) = delete;
  ShellTreeController& operator=(const ShellTreeController&) = delete;

  bool attach(HWND tree);
  void detach() noexcept;

  void applyTheme() noexcept;
  void ensureRoots();
  void resetRoots();

  void reflectLocation(const std::wstring& location);
  std::optional<ShellTreeSelection> selectionForItem(HTREEITEM item) const;

  LRESULT handleNotify(NMHDR* hdr);

 private:
  void populateRoots();
  void expandNode(HTREEITEM item);
  void onDeleteItem(NMHDR* hdr);
  void selectPath(const std::wstring& path);
  void selectShellLocation(const std::wstring& location);
  HTREEITEM findChildByPath(HTREEITEM parent, const std::wstring& fsPath);

  HWND tree_ = nullptr;
  bool rootsLoaded_ = false;
};

}  // namespace fast_explorer::ui
```

Rules:
- `ShellTreeController` owns no parent window and no navigation action.
- Each TreeView item `lParam` remains a CoTaskMemFree-able absolute PIDL.
- `TVN_DELETEITEM` frees PIDLs. `resetRoots()` calls `TreeView_DeleteAllItems`.
- It returns serialized locations only; `MainWindow` decides which pane/tab
  navigates.

## MainWindow layout

`computePaneLayout` remains unchanged. `MainWindow::relayout()` reserves a left
strip before calling it:

```cpp
const int navW = navigationTreeVisible_
    ? clampNavigationTreeWidth(navigationTreeWidthPx_, clientW)
    : 0;
const int navSplitterW = navigationTreeVisible_ ? scaleForDpi(5, dpi) : 0;
const int contentLeft = navW + navSplitterW;
const int contentW = clientW - contentLeft;

auto result = computePaneLayout(preset_, ratios, contentW, clientH, 0, statusH);

// Tree and its splitter use [0, clientH - statusH).
// Pane slots are offset by contentLeft when positioned.
```

Minimum and maximum width:

```cpp
constexpr int kNavigationTreeMinWidthDip = 180;
constexpr int kNavigationTreeDefaultWidthDip = 260;
constexpr int kNavigationTreeMaxWidthDip = 420;
```

For narrow windows, clamp the tree to at most 45% of the client width so the
active list view keeps usable space.

## Navigation and sync data flow

### Tree click to active tab

```text
User selects tree node
  -> NavigationTreePane gets TVN_SELCHANGED
  -> ShellTreeController::selectionForItem returns serialized location
  -> NavigationTreePane callback passes location to MainWindow
  -> MainWindow opens location in activeForPane_[activePane_]
  -> existing PaneController/OpenFolder path handles Shell and filesystem cases
```

The tree must suppress reentrant navigation while `reflectLocation()` is
programmatically selecting a node. That guard prevents "active tab changed,
tree selects, selection notification navigates again" loops.

```cpp
bool suppressSelectionCommit_ = false;
```

### Active tab to tree

The tree reflects the active tab when any of these happen:
- `MainWindow::setActivePane`
- `PaneTabHost::activateTab` callback into `MainWindow`
- successful folder navigation completion
- address popup pick
- back, forward, up, refresh, middle-click opened tab activation
- session restore after pane/tab creation

The reflected value should prefer `PaneController::currentLocation()` because
it preserves Shell namespace identities. If `currentLocation()` is empty, fall
back to `currentPath()`.

```cpp
std::wstring MainWindow::activeNavigationLocation() const {
  if (!pane_) return {};
  std::wstring loc = pane_->currentLocation();
  return loc.empty() ? pane_->currentPath() : loc;
}
```

## Shell location behavior

`ShellTreeController::reflectLocation` handles three classes:

| Location | Selection behavior |
|---|---|
| Drive or normal path | Expand This PC, drive root, then folder segments. |
| UNC share path | Select Network when exact shell child is unavailable; future expansion may find the server/share if shell enumeration exposes it. |
| Shell namespace | Select matching known-folder PIDL or parsed absolute PIDL. |

Known Shell aliases from v0.8.0 remain valid: `shell:ThisPC`,
`shell:Network`, `shell:NetHood`, `shell:RecycleBinFolder`, `shell:AppsFolder`,
`shell:UsersFilesFolder`, Gallery/Home aliases, and `::{CLSID}`.

Network Shortcuts behavior:
- `shell:NetHood` appears as a root or child where Windows exposes it.
- Individual shortcuts navigate through the existing `addressLocationForPidl`
  and `PaneController::openFolder` paths.
- If a shortcut is PIDL-only and no filesystem/UNC target is available, the
  tree still shows the Shell node but navigation may remain in Shell namespace
  mode with file operations disabled.

## Persistence

Current settings schema is v7. v0.8.1 increments it to v8 and adds:

```json
{
  "navigation_tree_visible": true,
  "navigation_tree_width_px": 260
}
```

`SessionState` additions:

```cpp
bool navigationTreeVisible = true;
int navigationTreeWidthPx = 260;
```

Read rules:
- Missing fields use defaults.
- Wrong-type fields fail the load, matching existing strict handling for schema
  corruption.
- Width is clamped after load to the current window DPI and client width during
  first `relayout()`.

Write rules:
- Always write both keys in v8.
- Capture the live splitter width on `WM_CLOSE`.

## Commands and menu

New command id in `src/explorer/messages.h`:

```cpp
inline constexpr WORD kAccelToggleNavigationTree = 132;  // Ctrl+B
inline constexpr WORD kMenuToggleNavigationTree = 392;
```

Command behavior:
- If visible: hide, keep last width in memory and settings.
- If hidden: show at last width.
- Hamburger menu item is checkable when visible.

## Error handling

- If TreeView creation fails, the app continues without the navigation tree and
  disables the menu item for that session.
- If Shell root population fails, show an empty tree surface rather than
  blocking pane navigation.
- If selecting a tree node resolves to an empty location, ignore the selection.
- If navigation fails because the path does not exist, keep the current pane
  state, using the existing v0.8.0 missing-path preservation behavior.

## Performance

- Roots are loaded lazily on first show, not during process startup.
- Child enumeration happens only on `TVN_ITEMEXPANDING`.
- Active tab sync does not enumerate arbitrary folder trees. It expands only
  the known path segments needed for the selected location.
- Repeated sync to the same location is skipped by caching
  `lastReflectedLocation_`.
- Background tabs do not get their own trees; the single tree avoids extra
  watcher/icon/cache cost.

## Tests and verification

Automated tests:
- `SettingsStore_V8_RoundTripNavigationTree`
- `SettingsStore_V7_MissingNavigationTreeFields_DefaultsVisible`
- `NavigationTree_ClampWidth_WithinClientBudget`
- `NavigationTree_SuppressSelectionDuringReflect`
- `ShellTreeController_ThisPcSelection_MapsToShellAlias`
- `ShellTreeController_RecycleBinSelection_MapsToShellAlias`

Manual GUI smoke:
- Toggle tree visible/hidden with `Ctrl+B` and hamburger menu.
- Resize tree width, restart, confirm width and visibility restore.
- Navigate active tab to `C:\`, `Downloads`, `shell:ThisPC`,
  `shell:Network`, `shell:RecycleBinFolder`, Gallery, Home, and confirm tree
  selection follows.
- Select tree nodes and confirm the active pane's active tab navigates.
- Switch panes and tabs, confirm the tree selection follows the newly active
  tab.
- In a filtered list, toggle/select tree nodes and confirm filter behavior is
  unchanged except when navigation intentionally changes folders.
- Verify network shortcut `C:\Users\SOOJANG\AppData\Roaming\Microsoft\Windows\Network Shortcuts\Home`
  remains accessible through the same Shell/location path as v0.8.0.

Release verification for v0.8.1:
- `git diff --check`
- Visual Studio developer environment `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `build\core-tests.exe`
- `build\winui_lite_tests.exe`
- Relevant `FastExplorerBench.exe` enumerate/head-to-head smoke to ensure the
  tree did not regress large-folder enumeration.

## Non-goals

- No per-pane tree in v0.8.1.
- No favorites or pinned folders.
- No drag/drop into or out of the tree.
- No Shell context menu in the tree.
- No inline rename/delete in the tree.
- No custom virtual folders beyond what Windows Shell exposes.
