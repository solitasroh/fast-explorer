# Handoff — winui_lite refactor shipped, tabs next

Date: 2026-05-28
Session focus: full 15-step `docs/refactor-plan.html` (rev3) execution + 2 follow-up cleanup commits — packed-cmd router migration and `src/ui → src/explorer` directory rename.
Next session focus: **Tabs feature** (per-pane tabs, browser-style). v0.4.0 handoff flagged this; the refactor was partly groundwork for it.

---

## What just shipped on `main`

17 commits on top of v0.6.1, all rebased linearly so `git log` reads top-down:

| Commit  | Step / Theme |
|---------|--------------|
| `26cf9f4` | rename src/ui/ to src/explorer/ |
| `1ca490e` | extract WM_COMMAND wiring to main-window-commands.cpp + shell-actions.{h,cpp} |
| `e557e5c` | packed-cmd path moves through CommandRouter |
| `ac7225e` | step 15 — completion report |
| `42be93c` | step 14 — winui_lite_tests standalone target |
| `6247a34` | step 13 — winui_lite_demo executable |
| `38f9d3e` | step 12 — migrate 12 accel cases to CommandRouter |
| `d29471a` | step 11 — ContextMenu port + adapter |
| `d5ef209` | step 10 — ChangeNotifier + SettingsStore ports + adapters |
| `87d9a7f` | step 9 — Clipboard + DragDrop ports + adapters |
| `e81ff0b` | step 8 — ShellItemSource + ShellItemDispatcher adapters |
| `97ce426` | step 7 — define ItemSource / Dispatcher / Activator ports |
| `461023d` | step 6 — extract AddressInput widget |
| `a972f3b` | step 5c — CommandRouter scaffold |
| `05d0aa1` | step 5b — extract StatusBar wrapper to chrome |
| `eae0c48` | step 5a — rename PaneSplitter class string |
| `a0e4302` | step 4 (rebased) — WindowBase WndProc extraction |

No user-visible change beyond v0.6.1. Every step verified by:
- `core-tests` (full integration suite)
- `winui_lite_tests` (chrome-only standalone, 86 cases, 0.03 s)
- Manual UI spot-checks at the risky steps (5c, 8, 9, 11, 12)

No bug fixes shipped — all changes were structural.

Full per-step report: `docs/refactor-completion.md`.
Plan source: `docs/refactor-plan.html`.

---

## Current architecture

```
fast-explorer/
├─ lib/winui_lite/                    Win32 UI library, knows no shell
│  ├─ chrome/   (15 .{h,cpp} pairs)   window-base, status-bar, command-router,
│  │                                  pane-splitter, pane-manager (template),
│  │                                  pane-layout, pane-toolbar-row, theme-watcher,
│  │                                  dark-scrollbar-hook, dispinfo-histogram,
│  │                                  com-raii, dpi-scale, layout-preset, cmd-packing
│  ├─ ports/    (8 .h interfaces)     ItemSource, ItemDispatcher, ItemActivator,
│  │                                  ClipboardBackend, DragDropBackend,
│  │                                  ChangeNotifier, SettingsStore, ContextMenu
│  └─ widgets/  (1 .{h,cpp} pair)     AddressInput
│
├─ src/
│  ├─ core/                           Storage, enumeration, sorting, FS backends
│  ├─ explorer/                       Shell-bound app (was src/ui/)
│  │  ├─ adapters/  (7 shell adapters wrapping ClipboardOps, FsWatcher,
│  │  │              IContextMenu, FileDropSource, PaneController, etc.)
│  │  ├─ main-window.cpp              ~2,836 lines — WM_ dispatch + class lifecycle
│  │  ├─ main-window-commands.cpp     ~292 lines — CommandRouter registrations
│  │  ├─ main-window.h
│  │  ├─ shell-actions.{h,cpp}        single-shot shell verbs
│  │  ├─ pane-controller.{h,cpp}      per-pane navigation history + worker
│  │  ├─ address-bar-popup, search-popup, shell-context-menu, ...
│  │  └─ (other shell-coupled UI)
│  ├─ app/main.cpp                    WinMain + bootstrap + SettingsStore adapter
│  └─ bench/                          CLI measurement tool
│
├─ examples/
│  └─ minimal-window/                 winui_lite_demo — zero shell, in-memory ports
│
└─ tests/                             core-tests + winui_lite_tests
```

Layer summary:

- `lib/winui_lite/` — chrome (Win32 widgets) + ports (abstract interfaces) + widgets. Linked statically; consumed by the app and the demo.
- `src/explorer/` — shell-bound UI. Each adapter under `adapters/` implements one port using shell / Win32 / domain APIs.
- `src/core/` — pure storage, enumeration, filesystem backends. No UI.
- `src/app/` — WinMain glue. Builds services, constructs MainWindow, registers the SettingsStore adapter.

Rule check (all enforced via `cmake/check-winui-lite-isolation.cmake` + grep at build time):
- lib → src reference: blocked
- "shell" / "explorer" / "folder" / "file:" tokens inside lib: clean
- shell headers (`<shlobj.h>` etc.) inside lib: clean

---

## Next session: tabs

The seam was called out in the v0.4.0 handoff:

> Tabs per pane. PaneController is already structured to be the future "Tab" body; SessionState would migrate to `panePaths[i]` → `panes[i].tabs[j].path` via a schema v6 migrator. Each slot grows a tab-strip above the listview. No layout-engine changes needed.

After the v0.6.x refactor the seam is in even better shape:
- `PaneController` owns one folder's worth of state (currentPath, history, FsWatcher, store, sort coordinator). It's exactly the unit a "tab" wants to be.
- `paneManager_` holds up to 4 `unique_ptr<PaneController>` today. The tab feature wants each slot to hold a `vector<unique_ptr<PaneController>> + activeTabIdx`.
- The adapters under `src/explorer/adapters/` already take a `PaneController&` — switching the active tab is "swap which controller the adapters point at".

### Design questions for the next session (DO NOT pre-decide here)

1. **Tab strip placement.** Above the existing pane toolbar row, or replacing it? Win Explorer uses a window-top strip; some apps use per-pane. Recommend per-pane to keep multi-pane semantics clean — but ask.

2. **SessionState schema v6.** `panePaths[4]` becomes `panes[4].tabs[]` and `panes[4].activeTab`. Migrator: v5 → v6 promotes each `panePaths[i]` to a single-element tabs vector.

3. **Keyboard conflict.** Ctrl+1..4 currently switch layout presets. Browser-style Ctrl+1..9 = "go to Nth tab" collides. Options:
   - Move layout to Alt+1..4, free Ctrl+1..9 for tabs
   - Use Ctrl+PageUp/Down for tab navigation, leave Ctrl+digits alone
   - User decides

4. **Close affordances.** Middle-click? X-button on hover? Ctrl+W only?

5. **New tab default.** Active tab's current folder? Home? Last-closed?

6. **Drag-to-reorder.** Browser convention. Defer to v0.8 if it adds scope creep.

7. **Tab → adapter rewiring.** Each `PaneController` already has its matching `ShellItemSource/Dispatcher/Clipboard/DragDrop/ContextMenu` adapter in MainWindow. The tab switch needs to either (a) recreate the per-pane adapter array against the new active controller, or (b) make the adapters look up their PaneController lazily through a `PaneController* (*)(std::size_t pane)` accessor.

   (a) is simpler at the cost of re-allocating adapters on every tab switch.
   (b) avoids the allocation but means adapters never bind permanently; the layer between source and adapter becomes a function pointer per pane.

   Recommend (b) — adapters are thin enough that "borrow a fresh PaneController* on each call" is cheap, and it sidesteps lifetime concerns on tab close.

### Recommended first session structure

1. Brainstorm via the `superpowers:brainstorming` skill (decisions 1–4 above).
2. Write a plan doc under `docs/superpowers/plans/` matching the v0.4.0 multi-pane plan format.
3. Implement in subagent batches as v0.4.0 did. Verify against a manual UI checklist before tag.

---

## Open follow-ups (deferred from the refactor)

From `docs/refactor-completion.md`'s "Open follow-ups" list, only the directory rename landed this session. Still pending, in priority order:

1. **Pre-existing bug**: typing a syntactically-valid-but-nonexistent absolute path into the address bar clears the listview. PaneController's `openFolder` calls `navigateInternal` on path-syntax-valid input regardless of existence; the enum worker finds nothing and the store empties. Fix would be: probe `FILE_ATTRIBUTE_DIRECTORY` on the resolved path before resetting the store. **Likely tabs blocker — easier to do before** since tab close on an empty store has the same visible symptom.

2. **WinUI / XAML evaluation.** Refactor opened the door; not in scope until a user pull.

3. **Demo app multi-pane.** Splitter + status-bar wired; PaneManager template not yet driven from the demo's main. Nice-to-have for showing tabs in the demo too.

4. **onCreate setup extraction.** `main-window.cpp` is still ~2,836 lines and ~280 of those are slot-0 chrome setup inside onCreate. Could move to `main-window-setup.cpp` using the same TU-split pattern as `main-window-commands.cpp`. Defer until it actively gets in the way.

---

## Environment + workflow (unchanged from v0.4.0 handoff)

- VS 2026 Pro at `C:\Program Files\Microsoft Visual Studio\18\Professional`. `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation` before cmake in a fresh shell.
- Build: `cmake --build build`. Ninja generator already configured under `build/`.
- Test: `ctest --test-dir build --output-on-failure` → 2 targets, `winui_lite_tests` (~0.03 s) + `core-tests` (~3.4 s).
- Run: `Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue; Start-Process build\FastExplorer.exe`.
- Demo run: `Start-Process build\winui_lite_demo.exe`.
- Settings file: `%LOCALAPPDATA%\FastExplorer\settings.json` (schema v5; v6 will land with tabs).
- Release: rebase local on `origin/main` to pick up the auto-appcast commit from the previous tag, then `git tag vX.Y.Z` + `git push origin main vX.Y.Z`. GitHub Actions runs the release workflow → installer + portable zip + appcast.xml update.

---

## Lessons worth keeping

- **`registerPackedCommand` is the canonical pattern for new commands.** Don't grow onCommand's switch. Even mid-feature, register against `accelRouter_` in `registerAccelHandlers` (or main-window-commands.cpp). The two-storage dispatch (`by_id_` + `packed_`) handles both bare ids and `packCmd(buttonId, paneIdx)` packed ids.

- **Adapters take a borrowed `PaneController&`. They do not own it.** Lifetime is the host's — adapters get reset at the head of `uninstallPaneAt` before the controller goes away. This pattern is critical for the tab feature where controllers come and go on each tab close.

- **Member-function-in-second-TU is fine.** `MainWindow::registerAccelHandlers` lives in `main-window-commands.cpp` and accesses private state through `this`. No friend declarations needed. Use the same trick if other private methods grow large — e.g. an `onCreateSetup()` could live in a `main-window-setup.cpp`.

- **CRLF warnings on `main-window.cpp` are harmless.** Git's autocrlf normalises to LF on commit. The file shows as LF in the repo and CRLF in working copy because PowerShell wrote it that way during the rename. No action needed.

- **The rebase from refactor/ui → main was fast-forward.** The branch had been rebased onto v0.6.1 mid-session, so the final merge added 30 commits linearly. Standard procedure if you branch again: `git rebase origin/main` before pushing for review keeps history flat.
