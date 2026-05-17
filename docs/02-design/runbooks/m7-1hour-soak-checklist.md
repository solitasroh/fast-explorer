# M7 1-Hour Soak Checklist

> **Purpose**: Verify the design §14.7 "1-hour soak: crash 0, memory leak 0" gate.
> **Related**: §14.7 (Milestone 7 exit criteria), §11 (Performance / Resource Strategy).
> **Author**: Claude
> **Last updated**: 2026-05-17

---

## 1. Scope

A single 60-minute interactive session with the Release build of `FastExplorer.exe`, exercising the realistic 100k-scale workload, that ends with:

- **Crash 0** — no unhandled exception, no SEH-translated fault, no minidump written outside the `--crash-test` paths.
- **Memory leak 0** — working-set drift ≤ 5 MB across the session (matches the cycle-soak gate already verified at the bench layer).
- **Stall ceiling** — no single dispatch latency ≥ 500 ms (the `StallHistogram` "≥500ms" bucket stays at 0).
- **LVN_GETDISPINFO p99** — bucket-readable p99 estimate ≤ 50 µs.

Out of scope for this checklist: multi-pane navigation soak (separate run), 1-hour throughput numbers (the bench harness covers throughput).

---

## 2. Prerequisites

1. Release build at `build\FastExplorer.exe` and `build\FastExplorerBench.exe`.
2. Two scratch datasets generated in advance (e.g. `%TEMP%\fe-soak-large` and `%TEMP%\fe-soak-mixed`):
   ```powershell
   & build\FastExplorerBench.exe generate --preset large-flat --out $env:TEMP\fe-soak-large
   & build\FastExplorerBench.exe generate --preset mixed-names --out $env:TEMP\fe-soak-mixed
   ```
3. Defender / indexer / cloud sync state recorded in the run notes (Defender on/off, OneDrive paused/active).
4. Idle baseline working set captured before launch via `FastExplorerBench.exe enumerate --path <small-dataset> --runs 1 --format json` and saved.
5. Clean process tree (no leftover FastExplorer.exe instances).

---

## 3. Run procedure (60 minutes)

| Minute | Action | Why |
|--------|--------|-----|
| 0      | Launch `FastExplorer.exe --open %TEMP%\fe-soak-large` (the 100k flat dataset). | Establish working-set baseline for the 100k case. |
| 0–2    | Let the enumeration complete, scroll top→bottom, observe first row in ≤ 200 ms. | §14.7 "large folder first row ≤ 200 ms" sanity check at session start. |
| 2–10   | Repeat open→close cycle (Ctrl+L, type a different folder path under `fe-soak-mixed`, Enter) every ~30 seconds. | Drives the FsWatcher refresh + memory-soak path. |
| 10–15  | Minimize the window for 60 seconds, restore, repeat 3×. | Exercises `notifyMinimized` / `EmptyWorkingSet` / restore-recovery. |
| 15–25  | Sort by Name asc → desc → Size asc → desc → Modified asc → desc on the 100k dataset. | Sort worker (M5) + GETDISPINFO histogram under reorder. |
| 25–35  | Create 5 new folders (Ctrl+Shift+N), rename each (F2), delete (Del). | Exercise the IFileOperation verbs + watcher coalesce. |
| 35–45  | Switch into `fe-soak-mixed` (Unicode + extension diversity) and scroll top→bottom 3×. | Stress ExtensionIconCache LRU + IconProvider STA worker. |
| 45–55  | Open Process Explorer / `Get-Process FastExplorer | Select WorkingSet64` and record the working-set every 60 s. | External corroboration of the in-process MemoryProbe trace. |
| 55–60  | Close the window (X button). | Triggers WM_NCDESTROY → PerfTracker + StallHistogram + DispInfoHistogram dump. |

---

## 4. Pass / fail criteria

| Item | Pass | Source |
|------|------|--------|
| Crash | App exits cleanly; no minidump in `%LOCALAPPDATA%\FastExplorer\dumps` (other than any `--crash-test` artifacts from prior runs). | CrashHandler write path (`src/core/crash-handler.cpp`). |
| Working-set drift | Final working set (post-close, before process exit) minus baseline ≤ 5 MB. | MemoryProbe events in the perf log; cross-checked against Process Explorer readings. |
| Stall ceiling | `[stall-histogram]` line in the log shows `>=500ms: 0`. | StallHistogram dump (main-loop epilogue). |
| GETDISPINFO p99 | `[dispinfo-histogram]` line in the log shows `p99 <= 50 us` (or any value in the `<=50` bucket family). | DispInfoHistogram dump (window destruction). |
| EmptyWorkingSet probe | `EmptyWorkingSet: call=N us  before=N KB  after=N KB` lines appear after each minimize; `call` is < 200 ms on every entry. | RingLogger output from `ProcessMemoryService::notifyMinimized`. |
| OneDrive hydration | 0 cloud-hydrate events triggered during enumeration. | External: OneDrive activity center / `Get-WinEvent Microsoft-Windows-Cloud-Files-Service/Operational`. |

Any single failure stops the run; capture the log + process-explorer snapshot before relaunching.

---

## 5. Result recording

After the run completes:

1. Copy `%LOCALAPPDATA%\FastExplorer\logs\fast-explorer.log` (or the configured log path) to a dated archive folder under `docs/02-design/runbooks/results/YYYY-MM-DD-1hour-soak/`.
2. Capture a `FastExplorerBench.exe enumerate --path %TEMP%\fe-soak-large --runs 5 --format json > result.json` snapshot AFTER the soak, save next to the log.
3. Update §14.7's "1-hour soak" exit-criteria bullet in `docs/02-design/features/fast-explorer-core.design.md` with the date, dataset(s), final drift, and pass/fail verdict.
4. If any criterion failed, file a follow-up under §17 deferred decisions or the PDCA `do` phase backlog with the captured artifacts attached.

---

## 6. Notes

- The full-UI run can't be automated through the existing bench-cli — that path drives the core enumerator headless. UI automation (UI Automation framework + a small driver harness) is a separate deliverable; the manual checklist above is the interim measurement protocol that the §14.7 gate is checked against.
- The 1 s `kEmptyWorkingSetThrottleMs` in `ProcessMemoryService` caps the probe-update rate; minimize-restore cycles shorter than that produce only the first probe entry. Hold the minimized state for at least 2 s to guarantee a fresh probe per cycle.
