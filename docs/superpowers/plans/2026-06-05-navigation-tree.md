# Left Navigation Tree Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one window-level, collapsible left navigation tree for Fast Explorer v0.8.1.

**Architecture:** Extract the Shell TreeView/PIDL/lazy expansion logic from `AddressBarPopup` into `ShellTreeController`, then host it in a persistent `NavigationTreePane`. `MainWindow` owns visibility, width, splitter layout, active-pane synchronization, commands, and session capture/restore.

**Tech Stack:** Win32 C++20, common-controls `WC_TREEVIEWW`, Shell PIDLs, existing `SessionState` JSON store, existing `CommandRouter`/accelerator model, `core-tests`.

---

### Task 1: RED Tests And Contracts

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/settings-store-tests.cpp`
- Modify: `tests/ui-messages-tests.cpp`
- Create: `tests/navigation-tree-pane-tests.cpp`
- Create: `tests/shell-tree-controller-tests.cpp`

- [ ] **Step 1: Write failing settings tests**

Add tests proving `SessionState` round-trips `navigationTreeVisible` and `navigationTreeWidthPx`, and that a v7 file without the fields defaults to visible width 260.

- [ ] **Step 2: Write failing command/menu tests**

Add tests proving `kAccelToggleNavigationTree` and `kMenuToggleNavigationTree` exist and do not collide with current command ranges.

- [ ] **Step 3: Write failing navigation tree helper tests**

Add tests for `clampNavigationTreeWidthPx` covering min, max, and the narrow-window 45% cap.

- [ ] **Step 4: Write failing Shell tree selection tests**

Create a hidden TreeView, attach `ShellTreeController`, reflect `shell:ThisPC` and `shell:RecycleBinFolder`, and assert the selected item serializes to the same location when Windows resolves the PIDLs.

- [ ] **Step 5: Run RED verification**

Run:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build'
```

Expected: FAIL because the new production headers/types/constants are not implemented yet.

### Task 2: Shared ShellTreeController

**Files:**
- Create: `src/explorer/shell-tree-controller.h`
- Create: `src/explorer/shell-tree-controller.cpp`
- Modify: `src/explorer/address-bar-popup.{h,cpp}`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Move shared TreeView logic**

Move PIDL ownership, root population, lazy `TVN_ITEMEXPANDING`, `TVN_DELETEITEM`, icon image list, theme application, path/shell reflection, and item-to-location serialization into `ShellTreeController`.

- [ ] **Step 2: Keep popup-specific behavior in AddressBarPopup**

Keep popup window creation, anchoring, mouse hook, ESC/Enter handling, and click-to-commit inside `AddressBarPopup`, delegating Shell tree operations to the controller.

- [ ] **Step 3: Run targeted tests**

Run:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build --target core-tests && build\core-tests.exe'
```

Expected: new shell-tree tests pass.

### Task 3: NavigationTreePane And MainWindow Integration

**Files:**
- Create: `src/explorer/navigation-tree-pane.h`
- Create: `src/explorer/navigation-tree-pane.cpp`
- Modify: `src/explorer/main-window.h`
- Modify: `src/explorer/main-window.cpp`
- Modify: `src/explorer/main-window-commands.cpp`
- Modify: `src/app/main.cpp`
- Modify: `src/explorer/messages.h`

- [ ] **Step 1: Add NavigationTreePane host**

Create a child host window containing `WC_TREEVIEWW`; route `WM_NOTIFY` to `ShellTreeController`; expose `reflectLocation`, `setVisible`, `setBounds`, and a navigation callback.

- [ ] **Step 2: Add fixed-width splitter**

Implement a small vertical splitter child next to the tree that commits pixel width to `MainWindow` and calls `relayout()`.

- [ ] **Step 3: Reserve left layout space**

Update `MainWindow::relayout()` to reserve the tree width and splitter width before calling `computePaneLayout`, then offset pane slots and splitters by the reserved left width.

- [ ] **Step 4: Wire active-tab synchronization**

Call tree reflection from `setActivePane`, `syncAddressBar`, `refreshPaneChrome`, restore paths, and tree navigation callback. Prefer `PaneController::currentLocation()` and fall back to `currentPath()`.

- [ ] **Step 5: Wire Ctrl+B and menu**

Add the accelerator, router handler, and checkable hamburger menu item.

- [ ] **Step 6: Run targeted tests**

Run:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build --target core-tests && build\core-tests.exe'
```

Expected: settings, message, navigation-tree helper, and shell-tree tests pass.

### Task 4: Session Persistence

**Files:**
- Modify: `src/core/settings-store.h`
- Modify: `src/core/settings-store.cpp`
- Modify: `src/explorer/main-window.cpp`

- [ ] **Step 1: Add v8 fields**

Add `navigationTreeVisible = true` and `navigationTreeWidthPx = 260` to `SessionState`.

- [ ] **Step 2: Read/write v8 JSON**

Increment schema to 8, read `navigation_tree_visible` and `navigation_tree_width_px` as ints, write both fields, and preserve v7 missing-field defaults.

- [ ] **Step 3: Capture/restore from MainWindow**

Apply the loaded visibility/width during initial state and capture live values during `WM_DESTROY`.

- [ ] **Step 4: Run settings tests**

Run:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build --target core-tests && build\core-tests.exe'
```

Expected: v8 settings tests pass.

### Task 5: Documentation And Full Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/usage-guide.md`

- [ ] **Step 1: Update user docs**

Document left Navigation Tree, `Ctrl+B`, hamburger toggle, splitter width, and session restore. Update usage-guide version line from 0.7.1 to 0.8.1 candidate/current CMake version as appropriate.

- [ ] **Step 2: Run required automated verification**

Run:

```powershell
git diff --check
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build'
ctest --test-dir build --output-on-failure
build\core-tests.exe
build\winui_lite_tests.exe
```

Expected: all commands exit 0.

- [ ] **Step 3: Run benchmark smoke if binaries are available**

Run:

```powershell
build\FastExplorerBench.exe generate --preset medium --out "$env:TEMP\fe-medium"
build\FastExplorerBench.exe enumerate --path "$env:TEMP\fe-medium" --runs 5
build\FastExplorerBench.exe head-to-head --path "$env:TEMP\fe-medium" --runs 5
```

Expected: commands exit 0 and no obvious large-folder enumeration regression is introduced by the UI-only tree work.

- [ ] **Step 4: Run manual GUI smoke**

Check `Ctrl+B`, hamburger menu checked state, splitter resize and restart restore, active tab/path reflection, tree node navigation, pane/tab switching reflection, known Shell roots, Network Shortcuts path preservation, and filter interaction.
