# winui_lite Refactor — Completion Report

Branch: `refactor/ui` (rebased onto `main` at v0.6.1)
Date: 2026-05-28
Plan: `docs/refactor-plan.html` (rev3)

## Outcome

All 15 plan steps completed. FastExplorer builds, runs, and passes
manual UI verification at every step. A standalone `winui_lite_demo`
executable proves the chrome + ports surface is now buildable without
any shell or domain code.

## Steps shipped (rebased onto main first; commits listed bottom-up)

| Step | Commit  | Summary |
|------|---------|---------|
| 5a   | eae0c48 | `PaneSplitter` class string → `winui_lite.*` namespace |
| 5b   | 05d0aa1 | Extract `StatusBar` wrapper (HWND + dark subclass) to chrome |
| 5c   | a972f3b | `CommandRouter` scaffold + Alt+M sample migration |
| 6    | 461023d | Extract `AddressInput` widget to `widgets/` |
| 7    | 97ce426 | Define `ItemSource` / `ItemDispatcher` / `ItemActivator` ports |
| 8    | e81ff0b | `ShellItemSource` + `ShellItemDispatcher` adapters; address-bar Enter routed |
| 9    | 87d9a7f | `ClipboardBackend` + `DragDropBackend` ports + shell adapters; Ctrl+C/V/X + drag wired |
| 10   | d5ef209 | `ChangeNotifier` + `SettingsStore` ports + adapters; main.cpp load/save wired |
| 11   | d29471a | `ContextMenu` port + `ShellContextMenuAdapter`; right-click wired |
| 12   | 38f9d3e | Migrate 12 accelerator cases to `CommandRouter` |
| 13   | 6247a34 | `winui_lite_demo` standalone executable (zero shell) |
| 14   | 42be93c | `winui_lite_tests` standalone test target (82 cases pass) |

## Rules check (all green)

| Rule | Check | Result |
|------|-------|--------|
| 1 | `lib/winui_lite/` cannot include `src/` | CMake gates every build via `check-winui-lite-isolation.cmake` |
| 2 | No `explorer` / `shell` / `folder` / `file:` tokens in chrome/ports/widgets | grep clean |
| 3 | No `<shell.h>` / `<shlobj.h>` / `<shlwapi.h>` / `<shellapi.h>` / `<shtypes.h>` / `<propsys.h>` includes inside `lib/` | grep clean |
| 4 | Ports stay narrow (1–4 methods) | ItemSource 4 / ItemDispatcher 2 / ItemActivator 1 / ClipboardBackend 2 / DragDropBackend 1 / ChangeNotifier 2 / SettingsStore 2 / ContextMenu 1 |
| 5 | Library does not spawn threads; results come back via callbacks | adapters own all async (ShellWorker, FsWatcher worker, DoDragDrop blocking call) |

## Line-count snapshot

```
lib/winui_lite/
  chrome/    2,780 lines (15 .h + .cpp pairs)
  ports/       330 lines (8 .h interfaces)
  widgets/      82 lines (address-input only)
  TOTAL      3,192 lines

src/explorer/adapters/   638 lines (7 .h + .cpp pairs)

examples/minimal-window/   ~460 lines (demo + in-memory port impls)
```

`src/explorer/main-window.cpp` is still ~3,155 lines (~3% smaller than
pre-refactor). The plan's aspirational ~300-line target would
require migrating the packed-cmd toolbar / menu switch (≈600 lines)
into `menu-actions.{h,cpp}` plus extracting `onCreate`'s ~280-line
setup block. Both are clean follow-up work — the mechanism is now
in place (`accelRouter_` + 12 sample registrations in `registerAccelHandlers`).

## Targets shipped

- `winui_lite`            — static lib, 27 files (chrome 24 + ports 7 + widgets 2). [unchanged interface to consumers]
- `FastExplorer`          — shell app, now consumes `winui_lite` through 7 adapters under `src/explorer/adapters/`.
- `winui_lite_demo`       — **new.** WIN32 executable that links ONLY `winui_lite`. Renders 10 fake items via `LVS_OWNERDATA` driven by `ItemDispatcher::textFor`, with chrome `StatusBar` at the bottom and `CommandRouter` routing F2 to a sample dialog.
- `winui_lite_tests`      — **new.** Standalone test executable (no `src/` deps). 82 cases pass in 0.02 s. Covers CommandRouter, port contracts, splitter ratios, splitter resize, dpi-scale, dispinfo histogram, layout-preset, pane-layout.
- `core-tests`            — existing combined suite, still green (covers everything plus the pieces that need `PaneController`).

## What's possible now (unblocked by this refactor)

- New small tools (log viewer, memory viewer, registry viewer) by writing a new `ItemSource` + `ItemDispatcher` adapter against `winui_lite`. Chrome stays as-is.
- UI automation tests via the demo-style in-memory adapters — no real filesystem needed to scroll, select, route commands.
- WinUI / XAML migration path: replace `chrome/` while keeping the port surface intact; adapters under `src/explorer/adapters/` continue to work.
- `winui_lite` could be hoisted to a separate repo / package once one more consumer outside FastExplorer exists.

## What this refactor intentionally did NOT do

- No user-visible behaviour change. Same keyboard shortcuts, menus, layouts, performance characteristics.
- No new features.
- No bug fixes (pre-existing issues like "navigation to non-existent absolute path clears the list-view" stay as they were; the rebase brought v0.6.1's Ctrl+A / UAC fixes along but added nothing new).
- No source-tree rename in the original step plan (the `src/ui/` → `src/explorer/` rename followed up after the plan completed; see the follow-up commit log).
- No WinUI / cross-platform work.
