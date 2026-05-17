# M9 Dual-Pane Soak Checklist

> **Purpose**: Verify the design §14.7 "Multi-pane soak: dual nav 50회 누적 working set Δ ≤ 10 MB" gate after M9 closes.
> **Related**: §14.7 (Milestone 7 exit criteria carry-forward), §4.2 (dual-horizontal layout), §4.5 (keyboard table).
> **Author**: Claude
> **Last updated**: 2026-05-18

---

## 1. Scope

A single 30-minute interactive session with the Release build of `FastExplorer.exe`, exercising the realistic dual-pane workflow, that ends with:

- **Crash 0** — no unhandled exception, no SEH-translated fault, no minidump written outside the `--crash-test` paths.
- **Memory drift ≤ 10 MB** — process working-set delta across 50 dual-pane navigation cycles. Wider than the single-pane 5 MB gate because two PaneControllers + two IconCacheCoordinators + two listView caches are alive concurrently.
- **Layout switch latency** — `Ctrl+1` / `Ctrl+2` / `Ctrl+H` visibly applies within one frame (16 ms). Subjective UX check; the StallHistogram should also show no `≥500ms` bucket entries triggered by these keys.
- **Active pane switch correctness** — `Tab` moves focus + status-bar label flips between "활성: 왼쪽" / "활성: 오른쪽" with each press. F2 / Delete / Ctrl+Shift+N then act on the focused pane only.

Out of scope: drag-and-drop between panes (deferred, design §17.1), pane width drag-resize (no splitter widget in M9), per-pane independent sort/filter (currently sort is per-pane via PaneController; filter not yet implemented).

---

## 2. Prerequisites

1. Release build at `build\FastExplorer.exe`.
2. Two scratch datasets (or two existing folders) for the two panes:
   ```powershell
   & build\FastExplorerBench.exe generate --preset large-flat --out $env:TEMP\fe-soak-A
   & build\FastExplorerBench.exe generate --preset mixed-names --out $env:TEMP\fe-soak-B
   ```
3. Defender / indexer / cloud sync state recorded.
4. Idle baseline working set captured before launch via Process Explorer or Task Manager (`Private Bytes` column).
5. Clean process tree (no leftover FastExplorer.exe).

---

## 3. Run procedure (30 minutes)

| Minute | Action | Why |
|--------|--------|-----|
| 0 | Launch FastExplorer.exe. Note PID + initial working set | baseline |
| 0-1 | Open `%TEMP%\fe-soak-A` in left pane (only pane active) | warm caches |
| 1 | Press `Ctrl+2` to enter dual mode | verify split animation < 1 frame |
| 1-2 | In right pane, open `%TEMP%\fe-soak-B` (via Ctrl+L → paste path) | both panes populated |
| 2-3 | Press `Tab` 5 times; verify status bar flips "왼쪽 ↔ 오른쪽" each press | active-pane focus |
| 3-15 | Alternate Tab + navigation: in each pane, open a subdirectory, then Alt+← back. Aim for ~50 navigation actions split across both panes | the soak workload |
| 15 | Sample working set | mid-point drift check |
| 15-20 | F2 rename in left pane, Delete in right pane, Ctrl+Shift+N in left pane. Verify each action lands on the focused pane only | per-pane action routing |
| 20 | Press `Ctrl+H` to toggle single mode → dual mode → single mode 3× | layout switch leak check |
| 20-25 | Repeat the 5-press Tab cycle + 10 navigations to confirm post-toggle stability | re-warm + drift |
| 25 | Sample working set | final drift check |
| 25-30 | Press `Ctrl+1` to collapse to single pane. Close app. | clean shutdown |

---

## 4. Gates and acceptance

| Gate | Target | How to measure |
|------|--------|----------------|
| Crash count | 0 | no minidump in `%LOCALAPPDATA%\FastExplorer\crashdumps\` |
| Working-set drift | ≤ 10 MB | (working-set at minute 25) − (working-set at minute 0) |
| Mid-point drift | ≤ 7 MB | (working-set at minute 15) − (working-set at minute 0) — sanity check |
| Layout switch UX | < 1 frame visible | subjective; no `[stall-histogram]` `≥500ms` entries in shutdown dump |
| Active-pane label | switches every Tab | manual observation |
| F2 / Delete / Ctrl+Shift+N | act on focused pane only | manual: confirm the file change in correct pane's folder |
| Session restore (post-close) | restores last layout + both pane paths | relaunch, verify both panes open to A / B and active matches the pre-close state (M9 extends C1 SessionState with right pane + layout mode — verify if shipped) |

---

## 5. Failure handling

- **Crash**: file minidump, capture `%LOCALAPPDATA%\FastExplorer\logs\fast-explorer-YYYYMMDD.log` + the StallHistogram / DispInfoHistogram shutdown dump. Re-attempt after triage.
- **Drift exceeds 10 MB**: investigate `IconCacheCoordinator` per-pane shrink behavior (low-memory broadcast was already wired in atom 4); check that `enterSingleMode` actually frees slot-1 coordinators (look for icon-batch messages still arriving for pane 1 after collapse).
- **Tab does not switch active**: verify `kAccelPaneSwitch` reaches `onCommand` (Tab is a global accelerator; list-view normally consumes Tab — possible TranslateAccelerator priority issue). The L2 review flagged this; if hit, replace `VK_TAB` with `FCONTROL|VK_TAB` in `src/app/main.cpp` accelerator table.
- **F2 / Delete acts on wrong pane**: verify `activeLabelEdit()` / active pane routing in onCommand. The `pane_` cached pointer should be refreshed by `setActivePane`.

---

## 6. Carry-forward

If this 30-minute soak passes the listed gates, M9 dual-pane block is functionally complete and the §14.7 multi-pane gate closes.

Items still amber after this soak (UI-automation candidates for M10+):
- 1-hour duration soak (this is a 30-minute focused check; full hour requires more user time)
- Quantitative layout-switch latency histogram (current is subjective < 1 frame)
- Per-pane working-set isolation (currently we measure process-wide; per-pane attribution would need ETW or per-coordinator memory probes)
