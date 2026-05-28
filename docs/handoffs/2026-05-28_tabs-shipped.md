# Handoff — v0.7.0 per-pane tabs implemented, manual sweep + tag pending

Date: 2026-05-28
Session focus: full execution of the 31-task tabs implementation plan (`docs/superpowers/plans/2026-05-28-tabs.md`). All production code, schema migration, unit tests, demo wiring, and accelerators land on `feat/tabs`. The branch is ready for the user's manual UI sweep, eager-cost measurement, and merge/tag.
Next session focus: manual sweep → merge `feat/tabs` into `main` → tag `v0.7.0` → ship via the existing release workflow. Open follow-ups below.

---

## What just shipped on `feat/tabs`

18 commits on top of `v0.6.1` (counting both the docs commit at the foot of the branch and per-task style cleanups). Linear history reads top-down via `git log v0.6.1..feat/tabs`:

| Commit  | Phase / Theme |
|---------|---------------|
| `90631ed` | style(demo): drop unused `<algorithm>`, add explicit `<utility>` |
| `cc5eee2` | feat(demo): winui_lite_demo demonstrates TabStrip without shell |
| `52a163f` | feat(tab-strip): drop indicator hugs target slot edge |
| `bcfe67f` | feat(listview): "Open in new tab" entry on folder right-click |
| `def62ef` | feat(main-window): tab keyboard surface + middle-click open + right-click menu |
| `7ff2f68` | feat(main-window): defer tab activation while OLE drag in progress |
| `7abf52e` | refactor(main-window): own paneTabHosts_ + strip layout + listview rebind |
| `c56277d` | refactor(explorer): extract pane-tab-host-state index helpers + tests |
| `d599e49` | feat(explorer): PaneTabHost (tabs vector, command surface, session) |
| `68c6aed` | refactor(adapters): take PaneController cell + add MainWindow bridge + tests |
| `109e3e8` | feat(winui_lite): TabStrip honours dark-mode palette |
| `d3e7359` | style(tab-strip): drop unused `<algorithm>` include |
| `34fd7a1` | feat(winui_lite): TabStrip widget (paint + mouse interaction) |
| `58cbd6f` | style(tab-strip-geometry-tests): drop unused TabRect using-decl |
| `82c64a1` | feat(winui_lite): TabStripGeometry layout math + tests |
| `30a5156` | style(settings-tests): drop unused using-decls left over from v5 port |
| `b7ebe2a` | test(settings): cover v5->v6 migration and v6 invariants |
| `3dbaf00` | feat(settings): introduce v6 panes[] schema with v5 migration |
| `9b591c4` | docs(tabs): v0.7.0 spec + implementation plan (branch foundation) |

All steps verified at each commit:
- `cmake --build build` — clean.
- `ctest --test-dir build --output-on-failure` — 2/2 (`winui_lite_tests` and `core-tests`).
- `check_winui_lite_isolation` cmake step passes — `lib/winui_lite/` contains no shell tokens.

`winui_lite_tests` grew from 86 → 95 (9 new `TabStripGeometry` cases). `core-tests` grew with: 4 new schema v5→v6 migration / invariant tests, 3 new adapter-cell tests, 8 new `PaneTabHostState` helper tests.

---

## Architecture as built

```
fast-explorer/
├─ lib/winui_lite/                    Win32 UI library, no shell vocabulary
│  ├─ chrome/   (15 .{h,cpp} pairs)   window-base, status-bar, command-router,
│  │                                  pane-splitter, pane-manager (template),
│  │                                  pane-layout, pane-toolbar-row, theme-watcher,
│  │                                  dark-scrollbar-hook, dispinfo-histogram,
│  │                                  com-raii, dpi-scale, layout-preset, cmd-packing
│  ├─ ports/    (8 .h interfaces)     unchanged from v0.6.x
│  └─ widgets/  (2 .{h,cpp} pairs)    AddressInput, TabStrip (+ TabStripGeometry pure)
│
├─ src/
│  ├─ core/
│  │  └─ settings-store.{h,cpp}       schema v6: PaneSessionV6 / TabRecordV6;
│  │                                  legacyPanePaths[] used for v5 migration
│  ├─ explorer/                       shell-bound app
│  │  ├─ adapters/  (7 shell adapters)  all five PaneController-coupled
│  │  │                                 adapters take `PaneController* const&`
│  │  │                                 cell, stored as `PaneController* const*`
│  │  ├─ main-window.cpp                ~3,100 lines after wiring
│  │  ├─ main-window-commands.cpp       registers four new accels:
│  │  │                                 Ctrl+T/W/Tab/Shift+Tab
│  │  ├─ main-window.h                  paneTabHosts_[4] + activeForPane_[4] +
│  │  │                                 activePane_ + paneCount_ +
│  │  │                                 oleDragInProgress_[4] + pendingActivation_[4]
│  │  ├─ pane-tab-host.{h,cpp}          NEW — per-pane tabs vector coordinator,
│  │  │                                 owns TabStrip, writes activeForPane_ cell
│  │  ├─ pane-tab-host-state.h          NEW — pure index-math helpers + 8 tests
│  │  ├─ pane-controller.{h,cpp}        unchanged — now the "tab body"
│  │  ├─ shell-context-menu.{h,cpp}     gained PrependItem; returns app verb id
│  │  ├─ shell-context-menu-adapter.cpp gained onOpenInNewTab callback
│  │  ├─ drop-target.cpp                hooks setOleDragInProgress + replay queue
│  │  └─ messages.h                     +kAccelNewTab/CloseTab/TabCycleNext/Prev
│  │                                   +kVerbOpenInNewTab (0xCA00)
│  ├─ app/main.cpp                    +4 ACCEL rows (Ctrl+T/W/Tab/Shift+Tab)
│  └─ bench/                          unchanged
│
├─ examples/minimal-window/
│  └─ main.cpp                        winui_lite_demo gained a 3-tab TabStrip
│                                     showcase with activate/close/new/reorder
│
└─ tests/                             core-tests + winui_lite_tests
                                      + 3 new test files:
                                      - tab-strip-geometry-tests.cpp
                                      - adapter-cell-tests.cpp
                                      - pane-tab-host-state-tests.cpp
```

Key invariants preserved or established:

- **Adapter lifetime ≠ controller lifetime.** Adapters bind once at pane creation to an `activeForPane_[i]` cell. Tab switch is a single pointer write into that cell. Adapters dereference per call with defensive null-check. OLE registrations (RegisterDragDrop) survive tab switches untouched.
- **Eager background tabs.** Every `PaneController` in a pane's tabs vector stays alive (FsWatcher + worker + store). Win Explorer parity; performance cost paid up-front for instant tab switch.
- **`lib/winui_lite/` shell isolation.** Build-time grep gate (`cmake/check-winui-lite-isolation.cmake`) blocks shell tokens; passes on every commit.
- **Last-tab close → Home reset.** A pane never has zero tabs; closing the only tab navigates it to `%USERPROFILE%` instead.
- **OLE drop defers tab switch.** During `IDropTarget::DragEnter` → `Drop`, a click on another tab is queued and replayed from the `Drop` tail.

---

## Manual UI sweep — to run before merge

User: run the steps below on `build\FastExplorer.exe` and tick each line. Items that fail are bugs to fix on the branch before tag.

### Keyboard
- [ ] `Ctrl+T` from Single layout → new Home tab spawns and becomes active.
- [ ] `Ctrl+W` on the only tab → tab resets to Home (`%USERPROFILE%`), pane stays open.
- [ ] `Ctrl+W` on a non-last tab → tab closes, neighbor activates.
- [ ] `Ctrl+Tab` cycles forward; wraps last → 0.
- [ ] `Ctrl+Shift+Tab` cycles backward; wraps 0 → last.
- [ ] `Ctrl+1`..`Ctrl+4` still cycle layout presets (not stolen by tab accels).

### Mouse
- [ ] Middle-click on a folder row → background tab opens with that folder; active stays.
- [ ] Middle-click on a file row → nothing happens.
- [ ] Middle-click on a tab in the strip → that tab closes.
- [ ] Hover over a tab → X button appears; click → tab closes.
- [ ] Click the `+` button on the right of the strip → new Home tab.
- [ ] Right-click a tab → 3-item menu (Close tab / Close other tabs / Close tabs to the right). Correct greyed-out states (others greyed when n=1, to-right greyed on last tab).
- [ ] Right-click a folder row → "Open in new tab" appears at the top of the popup; clicking it opens a background tab.
- [ ] Right-click a file row → no "Open in new tab" entry.
- [ ] Drag a tab → drop indicator follows mouse and snaps to slot edges; release reorders; no slide animation.
- [ ] Drag a tab and release outside the strip → no reorder.

### Layout / state / theming
- [ ] Quad layout + 3 tabs in each pane → each pane has its own independent tab strip.
- [ ] Tab switch latency in 12-tab Quad scenario — should feel instant.
- [ ] Boot the app with the existing `%LOCALAPPDATA%\FastExplorer\settings.json` (v5) → migration runs once; subsequent runs save as v6.
- [ ] Boot, open several tabs, restart → tabs restore at the same paths, same active tab per pane.
- [ ] Toggle dark mode while tabs are open → tab strip recolours immediately.
- [ ] DPI change with tabs open → strip height adapts; layout repacks.

### Eager-cost measurement (record numbers in this file)
- Working set with Quad layout + 3 tabs each (12 PaneControllers): __ MB (baseline single-tab Single layout: __ MB)
- Background-tab FsWatcher CPU during idle: __ %
- Active-tab switch perceived latency: __ ms / "instant" / "noticeable"

---

## Lessons worth keeping

- **The `activeForPane_[4]` cell pattern is the right model for "pane chrome × tab data" decoupling.** Adapter constructors take `PaneController* const&` (an alias to the cell). Internally store `PaneController* const* cell_;` and dereference per call with a one-line null guard. Cheap, lifetime-safe, and OLE registrations don't have to churn.

- **PaneManager template stayed but is unused by production.** `winui_lite_demo` is its only consumer. The shape of `TabPane(MainWindow*, idx, cell&)` would have required either a factory-callback PaneManager or a two-phase init — both more invasive than just managing `paneTabHosts_[4]` directly in MainWindow.

- **Schema migration v5 → v6 used a `legacyPanePaths[]` shim on `SessionState`.** The reader populates the legacy field when it sees `pane_paths` in old files; the tail of `loadSessionState` promotes it into `panes[i].tabs[0]` only when `schemaVersionLoaded < 6`. Writer never touches the legacy field. Clean shim, no public-facing API change.

- **Subagent dispatches that touch many files (e.g. P5 T23–T25 dropping `paneManager_`) consistently report compressed summaries that under-state the diff size.** Verify with `git show --stat <sha>` before trusting that the work matches the prompt. In this branch the actual changes always matched the plan even when summaries were terse.

- **Clangd's index lags the real build state.** Several times during the session, clangd reported `src/ui/...` errors (path renamed in v0.6.x) and "private member" errors against newly-public methods. The actual MSVC build was always clean. Don't chase clangd-only diagnostics — verify with the real compiler.

- **clang-tidy `unused-includes` / `misc-unused-using-decls` fire often on auto-generated test code and refactored ports.** Cheaper to drop them inline at commit time than to dispatch a follow-up. ~5 such cleanup commits in this branch.

---

## Open follow-ups (deferred from this session)

From the spec's "Out of scope (v1)" list, none addressed:

1. **Closed-tab history / `Ctrl+Shift+T`** — would need a per-pane bounded LIFO of `(path, scrollPosition)` records + an accel binding. No schema slot reserved.
2. **Cross-pane tab drag** — TabStrip drag is bounded by client rect today; mouse-leave during drag cancels. Cross-pane needs a different OLE-drag flow.
3. **Tab rename / pinned tabs / tab groups / per-tab color** — not designed.
4. **Per-tab persisted scroll / selection / sort-spec** — schema v6 stores only `path`. A future v7 could nest these. Performance cost: settings file size grows.
5. **Detach tab into a new window** — Win Explorer 11 doesn't do this; no parity pressure.
6. **Background-tab dirty hint** — when an inactive tab's FsWatcher sees changes, no visual cue today. A subtle dot or strikethrough on the tab title is the obvious affordance.

Carried over from the earlier handoff (`docs/handoffs/2026-05-28_winui-lite-refactor-shipped.md`) and not addressed this session:

1. **Address-bar nonexistent-but-valid path bug.** Typing `C:\not\a\real\path` into the address bar clears the listview because `PaneController::openFolder` runs `navigateInternal` on path-syntax-valid input without probing existence. Tabs make the symptom slightly less visible (close-to-Home reset) but the root cause is unchanged.
2. **`main-window.cpp` is ~3,100 lines** post-Phase-5. A second TU split (`main-window-setup.cpp` for slot-0 chrome) is the obvious follow-up but not blocking.
3. **`winui_lite_demo` multi-pane.** Demo still shows a single TabStrip + listview. Adding PaneManager-driven multi-pane to the demo would round out the showcase.

---

## Branch / merge instructions

```powershell
# Pull origin to make sure main hasn't moved
git fetch origin

# Rebase feat/tabs on origin/main (should be fast-forward if no other work merged)
git switch feat/tabs
git rebase origin/main

# Run the full sweep one more time
cmake --build build
ctest --test-dir build --output-on-failure

# Manual sweep checklist above

# Merge to main (linear, no merge commit)
git switch main
git merge --ff-only feat/tabs
git push origin main

# Tag v0.7.0 — GitHub Actions release workflow will pick it up
git tag v0.7.0
git push origin v0.7.0

# After CI publishes the release, delete the feature branch
git branch -d feat/tabs
git push origin :feat/tabs
```

---

## Environment + workflow (unchanged from prior handoffs)

- VS 2026 Pro at `C:\Program Files\Microsoft Visual Studio\18\Professional`. `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation` before cmake in a fresh shell.
- Build: `cmake --build build`. Ninja generator already configured under `build/`.
- Test: `ctest --test-dir build --output-on-failure` → 2 targets, `winui_lite_tests` + `core-tests`.
- Run: `Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue; Start-Process build\FastExplorer.exe`.
- Demo: `Start-Process build\winui_lite_demo.exe`.
- Settings: `%LOCALAPPDATA%\FastExplorer\settings.json` (schema v6 — v5 files auto-migrate on first load).
- Release pipeline: tag triggers GitHub Actions → installer + portable zip + appcast.xml.
