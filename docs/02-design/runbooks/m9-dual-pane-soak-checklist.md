# Multi-Pane Soak Checklist

> **Purpose**: Verify the design §14.7 "Multi-pane soak: dual nav 50회 누적 working set Δ ≤ 10 MB" gate after the B-series follow-up (B1 horizontal layout / B2 Alt+V/H/F6 / B3 settings v3 orientation).
> **Related**: §14.7 (Milestone 7 exit criteria carry-forward), §4.2 (dual-horizontal layout), §4.5 (keyboard table).
> **Author**: Claude
> **Last updated**: 2026-05-19 (B-series reflected)

---

## 1. Scope

A single 30-minute interactive session with the Release build of `FastExplorer.exe`, exercising the realistic dual-pane workflow in **both seam orientations**, that ends with:

- **Crash 0** — no unhandled exception, no SEH-translated fault, no minidump written outside the `--crash-test` paths.
- **Memory drift ≤ 10 MB** — process working-set delta across 50 dual-pane navigation cycles. Wider than the single-pane 5 MB gate because two PaneControllers + two IconCacheCoordinators + two listView caches are alive concurrently.
- **Layout switch latency** — `Ctrl+1` / `Alt+V` / `Alt+H` visibly applies within one frame (16 ms). Subjective UX check; the StallHistogram should also show no `≥500ms` bucket entries triggered by these keys.
- **Seam-rotation latency** — `Alt+V` ↔ `Alt+H` while already dual flips the seam in place (no destroy/recreate). Visibly < 1 frame.
- **Active pane switch correctness** — `F6` moves focus + status-bar label flips between "활성: 왼쪽" / "활성: 오른쪽" with each press. F2 / Delete / Ctrl+Shift+N then act on the focused pane only.
- **Session restore correctness** — closing in dual + horizontal must restore dual + horizontal next launch, with both pane folders intact.

Out of scope: pane seam drag-resize (still 50/50, no splitter widget), per-pane filter (not yet implemented), thumbnails / dark mode (design §17.2 deferred).

---

## 2. Prerequisites

1. Release build at `build\FastExplorer.exe` (build head ≥ `1f817b7` for orientation persistence).
2. Two scratch datasets (or two existing folders) for the two panes:
   ```powershell
   & build\FastExplorerBench.exe generate --preset large-flat   --out $env:TEMP\fe-soak-A
   & build\FastExplorerBench.exe generate --preset mixed-names  --out $env:TEMP\fe-soak-B
   ```
3. Defender / indexer / cloud sync state recorded.
4. Idle baseline working set captured before launch via Process Explorer or Task Manager (`Private Bytes` column).
5. Clean process tree (no leftover FastExplorer.exe).
6. **Settings reset (optional)**: delete `%LOCALAPPDATA%\FastExplorer\settings.json` to start from defaults — only needed if you want to verify v2→v3 fresh-create path. Keep it if you want to verify the cross-session restore scenario.

---

## 3. Run procedure (30 minutes)

| Minute | Action | Why |
|--------|--------|-----|
| 0 | Launch FastExplorer.exe. Note PID + initial working set | baseline |
| 0-1 | Open `%TEMP%\fe-soak-A` in left pane (only pane active) | warm caches |
| 1 | Press `Alt+V` to enter vertical (side-by-side) dual mode | verify split < 1 frame; second pane should auto-open on left pane's folder (B1+A6 fallback) |
| 1-2 | In right pane, press Ctrl+L → paste `%TEMP%\fe-soak-B` → Enter | both panes populated, distinct folders |
| 2-3 | Press `F6` 5 times; verify status bar flips "왼쪽 ↔ 오른쪽" each press | active-pane focus |
| 3-12 | Alternate F6 + navigation: in each pane, open a subdirectory, then Alt+← back. Aim for ~50 navigation actions split across both panes | the soak workload |
| 12 | Sample working set | mid-point drift check |
| 12-15 | Press `Alt+H` (currently vertical → rotates to horizontal stacked). Press `Alt+V` (rotates back to vertical). Press `Alt+H` again (rotates to horizontal). Press `Alt+H` a 4th time (exits to single). Press `Alt+H` (re-enters horizontal dual). | seam-rotation + same-key-exit semantics |
| 15-20 | F2 rename in left pane, Delete in right pane, Ctrl+Shift+N in left pane. Verify each action lands on the focused pane only | per-pane action routing |
| 20 | Press `Ctrl+1` to collapse to single pane. Press `Alt+V` to re-enter dual. Press `Ctrl+1` again. Press `Alt+H` (enters horizontal dual). | layout switch leak check across both seams |
| 20-25 | Repeat the 5-press F6 cycle + 10 navigations to confirm post-toggle stability | re-warm + drift |
| 25 | Sample working set | final drift check |
| 25-27 | While in horizontal dual, close the app via Alt+F4 | clean shutdown — capture state |
| 27-28 | Relaunch FastExplorer.exe. Verify: app opens in horizontal dual, left pane = `fe-soak-A`, right pane = `fe-soak-B`, active pane matches pre-close. | session restore correctness (v3 schema) |
| 28-30 | Close again | done |

---

## 4. Gates and acceptance

| Gate | Target | How to measure |
|------|--------|----------------|
| Crash count | 0 | no minidump in `%LOCALAPPDATA%\FastExplorer\crashdumps\` |
| Working-set drift | ≤ 10 MB | (working-set at minute 25) − (working-set at minute 0) |
| Mid-point drift | ≤ 7 MB | (working-set at minute 12) − (working-set at minute 0) — sanity check |
| Layout switch UX | < 1 frame visible | subjective; no `[stall-histogram]` `≥500ms` entries in shutdown dump |
| Seam-rotation UX | in-place rotation < 1 frame | subjective; the two pane windows must not flicker (B2 setLayoutOrientation does relayout only, no destroy/recreate) |
| Active-pane label | switches every F6 | manual observation; Tab now belongs to list-view internal nav and must NOT trigger pane switch |
| F2 / Delete / Ctrl+Shift+N | act on focused pane only | manual: confirm the file change in correct pane's folder |
| Session restore (post-close) | restores last seam orientation + both pane paths | relaunch, verify horizontal dual + A/B + active matches the pre-close state |

---

## 5. Failure handling

- **Crash**: file minidump, capture `%LOCALAPPDATA%\FastExplorer\logs\fast-explorer-YYYYMMDD.log` + the StallHistogram / DispInfoHistogram shutdown dump. Re-attempt after triage.
- **Drift exceeds 10 MB**: investigate `IconCacheCoordinator` per-pane shrink behavior (low-memory broadcast was already wired in M9 atom 4); check that `enterSingleMode` actually frees slot-1 coordinators (look for icon-batch messages still arriving for pane 1 after collapse).
- **Alt+V or Alt+H not honored**: verify the accelerator table in `src/app/main.cpp` (FALT|FVIRTKEY|'V' / 'H') and the dispatch in `onCommand` (`kAccelLayoutVerticalToggle` / `kAccelLayoutHorizontalToggle` → `resolveLayoutToggle`).
- **Seam rotation destroys panes instead of in-place flip**: confirm `setLayoutOrientation` only assigns `orientation_` and calls `relayout()` (no `closeSecond` / `openSecond`). If panes flicker, the rotation path is incorrectly going through enterSingleMode → enterDualMode.
- **F2 / Delete acts on wrong pane**: verify `activeLabelEdit()` / active pane routing in onCommand. The `pane_` cached pointer should be refreshed by `setActivePane`.
- **Session restore lands in vertical when horizontal was saved**: verify `restoreLayoutFromSession` reads `state.orientation` BEFORE the `if (Dual)` branch and passes it to `enterDualMode(secondPath, state.orientation)`. Check `%LOCALAPPDATA%\FastExplorer\settings.json` after close — it should contain `"orientation":"horizontal"`.

---

## 6. Carry-forward

If this 30-minute soak passes the listed gates, the multi-pane block (M9 + B-series) is functionally complete and the §14.7 multi-pane gate closes.

Items still amber after this soak (UI-automation candidates for the next milestone):
- 1-hour duration soak (this is a 30-minute focused check; full hour requires more user time)
- Quantitative layout-switch latency histogram (current is subjective < 1 frame)
- Per-pane working-set isolation (currently we measure process-wide; per-pane attribution would need ETW or per-coordinator memory probes)
- Drag-resize seam (no splitter widget; 50/50 hard split)
