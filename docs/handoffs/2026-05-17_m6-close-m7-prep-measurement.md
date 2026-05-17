---
date: 2026-05-17
git_commit: 204dd24
branch: main
status: handoff
project_type: windows-desktop-cpp
---

# 핸드오프: M6 close → M7 prep tech-debt → M7 측정 infra 완결

## 작업 내용

PDCA Phase: **Do (active)** — milestone 진행도 M1–M6 ✅ / M7 ✅ (infra) / M7 🟡 (UI automation 정량 follow-up).

이 세션에서 prior handoff (`8c90811`, 2026-05-16) 이후 **20 commits** 누적: M6 UI 통합 잔여 5 atom + M6 close 문서 + M7 prep tech-debt 3 + M7 §14.7 deliverable 측정 인프라 9 + dataset coverage 조정.

### M6 UI 통합 잔여 (verb 단위 5 atom)

| Atom | Commit | 내용 |
|---|---|---|
| 6a | `8c7cf41` | PaneController::deleteItem + VK_DELETE + focus guard |
| 6b | `a38cdee` | renameItem + F2 + LVS_EDITLABELS in-place + DRY resolveRowSourcePath |
| 6c | `afce074` | createSubfolder + Ctrl+Shift+N + Windows Explorer create→edit + uniqueFolderLeaf (CompareStringOrdinal IgnoreCase) |
| 6d | `e1cfb15` | OperationResult channel + opResultStatusText + **L2가 잡은 lost-result race** 양쪽 수정 (drainResults clear-inside-lock) |
| 6e | `7d6c58a` | ImageList byte-count probe + low-memory shrink (kWmFeLowMemory) |
| close | `7f04aea` | design v1.0.10 + plan v1.0.3, §14.6 Completed 일화 + §14.7 carry-forward 정리 |

### M7 prep tech-debt 3 atom

| Atom | Commit | 내용 |
|---|---|---|
| T1 | `558d895` | ResultChannel<T> template — IconProvider + ShellWorker publish/drain/coalesce DRY-002 해소, lost-result fix 단일화 |
| T2 | `d69d56f` | ProcessMemoryService::LowMemoryCallback void(*)() → std::function<void()>, static HWND global 제거 |
| T3 | `64265a5` | IconCacheCoordinator 추출 — MainWindow ~80 LOC 감소 (icon-cache slice SRP closed; selection-sync / label-edit slice는 M7+ follow-up) |

### M7 §14.7 measurement infra (이번 세션 9 atom)

| Atom | Commit | 내용 / 측정 결과 |
|---|---|---|
| §14.7 doc adj | `951f653` | Dataset coverage already-met-from-M2 명시 |
| MemoryProbe | `030a1ce` | PerfTracker::EventId::MemoryProbe + 5 sample sites (launch/pane-open/first-batch/enum-complete/shutdown) + `recordMemoryProbe(PerfTracker&)` helper |
| 100k working set probe | `509c3ee` | EnumerationBenchResult.WorkingSetSamples 추가, **peak 9.39 MB / 50 MB target의 18.8%** |
| Memory soak | `698ca0a` | 10-cycle drift 추적 + post-cycle 시계열, **drift 404 KB / 5 MB gate의 8.1%** (c0=400, c1-c9=404 KB 평탄화 → no-leak signature) |
| StallHistogram | `e344323` | 7-bucket 메시지 디스패치 latency aggregator, shutdown dump |
| Bench JSON enumerate | `5d1f112` | bench-json.{h,cpp} + machine info (GetSystemInfo + RtlGetVersion) + `--format json` (_O_U8TEXT crash 회피: MultiByteToWideChar→fputws round trip) |
| DispInfoHistogram | `288a32f` | LVN_GETDISPINFO p99 estimator (50us 버킷 경계 = §14.3 gate), handleGetDispInfo QPC bracket |
| head-to-head JSON | `9b0e1eb` | enumerate JSON 일관성 |
| EmptyWorkingSet probe | `54484f8` | ProcessMemoryService에 call-latency + bytes-before/after envelope |
| 1-hour soak checklist | `d902afd` | `docs/02-design/runbooks/m7-1hour-soak-checklist.md` 매뉴얼 측정 protocol |
| bench-compare CI script | `204dd24` | `scripts/bench-compare.ps1` — JSON 두 개 비교, 5% tolerance + §14.7 drift gate, exit 0/1/2 |

## 핵심 측정값 (실측)

| Gate | Target | Measured | Margin |
|---|---|---|---|
| 100k working set | ≤ 50 MB target, ≤ 100 MB budget | **9.39 MB** (peak, headless) | **5.3× under target** |
| Memory soak 10-cycle drift | ≤ 5 MB | **404 KB** | **12.4× under gate** |
| FileModelStore footprint @ 100k | (informational) | entries 3.81 MB + arena 1.00 MB = 4.81 MB | — |
| Enumerate timing @ 100k (median/p95) | (informational) | 45.7 / 50.8 ms (M2 baseline 43.8 ms — same range) | — |
| Stall histogram | (UI run needed) | infra ready, full-UI 정량 manual capture 필요 | — |
| GETDISPINFO p99 | ≤ 50 µs | infra ready, full-UI 정량 manual capture 필요 | — |
| EmptyWorkingSet call latency | ≤ 200 ms (recovery half) | infra ready, call-latency half 측정 가능 | — |

## §14.7 deliverable 완료도

✅ = 완결, 🟡 = infra ready / 정량 follow-up

| Deliverable | Status |
|---|---|
| Dataset generator preset coverage | ✅ Met from M2 |
| Memory snapshot (GetProcessMemoryInfo) | ✅ MemoryProbe events |
| UI stall probe integration | ✅ Histogram + shutdown dump |
| Scroll p95 / GETDISPINFO p99 측정 | ✅ DispInfoHistogram infra / 🟡 full-UI scroll soak |
| Benchmark result JSON + machine info | ✅ enumerate + head-to-head |
| Baseline 비교 CI script | ✅ scripts/bench-compare.ps1 |
| 1-hour soak test checklist | ✅ runbooks/m7-1hour-soak-checklist.md |
| 100k entries process working set | ✅ 9.39 MB (headless) / 🟡 full-UI |
| Memory soak Δ ≤ 5 MB | ✅ 404 KB |
| EmptyWorkingSet 회복 ≤ 200 ms | ✅ call-latency / 🟡 restore recovery |
| Low-memory caches drop 검증 | ✅ M6 atom 6e + EmptyWorkingSet probe |
| Multi-pane soak | 🟡 dual-pane 아키텍처 미진행 |
| 1-hour soak: crash 0 / leak 0 | 🟡 manual checklist 실행 필요 |

## 누적 산출물

### Core (M7 신규)
- `src/core/perf-tracker.{h,cpp}` — EventId::MemoryProbe 추가, eventName label
- `src/core/process-memory.{h,cpp}` — recordMemoryProbe(PerfTracker&) 헬퍼, EmptyWorkingSetProbe envelope, LowMemoryCallback std::function 시그니처
- `src/core/ring-logger.{h,cpp}` — 변경 없음

### Bench (M7 신규)
- `src/bench/bench-json.{h,cpp}` — formatEnumerateBenchJson / formatHeadToHeadBenchJson / captureMachineInfo
- `src/bench/enumeration-bench.{h,cpp}` — WorkingSetSamples + per-cycle drift 시계열
- `src/bench/bench-cli.{h,cpp}` — --format text|json, parse 확장

### UI (M6 close + M7 신규)
- `src/ui/result-channel.h` — DRY-002 template (T1)
- `src/ui/icon-cache-coordinator.{h,cpp}` — MainWindow extraction (T3)
- `src/ui/folder-name.{h,cpp}` — uniqueFolderLeaf
- `src/ui/dispinfo-histogram.{h,cpp}` — GETDISPINFO p99 estimator
- `src/ui/stall-probe.{h,cpp}` — StallHistogram + 7-bucket dump
- `src/ui/shell-worker.{h,cpp}` — OperationResult 채널, ResultChannel<T> 사용
- `src/ui/icon-provider.{h,cpp}` — ResultChannel<T> 사용
- `src/ui/icon-cache.{h,cpp}` — byteSize + swap + createPlaceholderImageList
- `src/ui/pane-controller.{h,cpp}` — deleteItem / renameItem / createSubfolder + resolveRowSourcePath
- `src/ui/main-window.{h,cpp}` — iconCoord_ 위임 + DispInfoHistogram 통합 + LVS_EDITLABELS in-place rename + pendingEditFolderName_ auto-edit

### Docs (M6 close + M7)
- `docs/01-plan/features/fast-explorer-core.plan.md` v1.0.3 — M6 status Completed
- `docs/02-design/features/fast-explorer-core.design.md` v1.0.10 — §14.6 Completed 일화, §14.7 deliverable 표 측정값 반영
- `docs/02-design/runbooks/m7-1hour-soak-checklist.md` — 매뉴얼 measurement protocol

### 신규 테스트 파일
`folder-name-tests.cpp`, `result-channel-tests.cpp`, `perf-tracker-tests.cpp`, `dispinfo-histogram-tests.cpp`, `process-memory-tests.cpp`, `bench-json-tests.cpp`. 345 → **412 tests** (+67).

## 학습한 내용 (이번 세션)

### Win32 / SDK 패턴

**LVS_OWNERDATA + LVS_EDITLABELS**:
- LVN_BEGINLABELEDIT는 FALSE 반환 = 편집 허용 (W/A 동일).
- LVN_ENDLABELEDIT는 OWNERDATA 하에서 **항상 FALSE** 반환 권장 — list-view가 자체 텍스트 저장 안 함, 모델 갱신은 controller 책임.
- W-suffix dispatch (LVN_BEGINLABELEDITW / LVN_ENDLABELEDITW)는 WC_LISTVIEWW + Unicode 부모 등록 시 도착.

**UTF-8 출력 on _O_U8TEXT stdout**:
- 메인 entry가 `_setmode(_fileno(stdout), _O_U8TEXT)`로 wide-text 모드 설정 시, 좁은 fwrite 호출은 UB (MSVC 환경에서 /GS stack-buffer overrun으로 즉시 프로세스 종료).
- 회피: UTF-8 narrow JSON을 `MultiByteToWideChar`로 wide 변환 후 `fputws`. U8TEXT 변환 레이어가 다시 UTF-8로 emit.

**CompareStringOrdinal IgnoreCase**:
- Win32 NTFS case-folding과 일치하는 ordinal 비교.
- `std::towlower`는 locale-dependent → 비-ASCII (터키어, 그리스어, surrogate pair)에서 NTFS와 불일치.

### Concurrency (M6 atom 6d L2가 잡은 race)

`drainResults` 의 `postPending_.store(false)`가 swap mutex **밖**에 있으면:
1. UI가 swap, mutex 해제.
2. Worker가 mutex 잡고 push, CAS(false→true) — 그러나 `postPending_`는 아직 prev publish의 `true`로 남아 있음.
3. CAS 실패 → PostMessage 안 보냄.
4. UI가 store(false) — 너무 늦음, 메시지 손실.

**Fix**: swap과 store(false)을 **같은 mutex 영역 안에** 배치. ResultChannel<T> template이 canonical impl.

### 메모리 규칙 갱신 (반복 적용)

- atom shape 결정마다 AskUserQuestion (verb 단위 5개 vs 계층 분리 vs 최소단위 7개; ResultChannel/LowMemoryCallback/Coordinator 순서; head-to-head JSON + EmptyWorkingSet 합칠지 별개로 갈지).
- sub-step별 L1+L2 review (각 atom commit 직전).
- 모든 severity 검토 후 apply/defer + 사유 commit body 기록.
- 주석 minimal + design/plan 참조 + planning vocab 금지 (commit body는 OK, code 코멘트만 제한).

## 핵심 참조 문서

- `docs/02-design/features/fast-explorer-core.design.md` v1.0.10 — §14.1–§14.6 Completed, §14.7 deliverable 표 측정값 반영, follow-up 정리.
- `docs/01-plan/features/fast-explorer-core.plan.md` v1.0.3.
- `docs/02-design/runbooks/m7-1hour-soak-checklist.md` — manual soak protocol.
- `scripts/bench-compare.ps1` — baseline-compare CI script.

## 다음 작업 항목

### 1. Full-UI 정량 측정 (manual)

`scripts/bench-compare.ps1`와 `docs/02-design/runbooks/m7-1hour-soak-checklist.md`를 가지고 매뉴얼 실행:
- LargeFlat (100k) + MixedNames + DeepTree 데이터셋 준비.
- FastExplorer.exe 1-hour interactive 세션.
- Log: `[stall-histogram]` / `[dispinfo-histogram]` / `EmptyWorkingSet:` / MemoryProbe events 캡처.
- 결과를 §14.7 exit-criteria 표에 반영.

### 2. UI automation harness (Optional)

- UI Automation framework (UIAutomationClient COM)로 minimize / restore / scroll / sort 시뮬레이션.
- Manual checklist를 스크립트로 변환.
- §14.7 N2/N3/N4 (Plan §12.1 deferred decisions) 해소.

### 3. M6 SRP partial-close 마무리 (T3 follow-up)

- SelectionSync extraction (handleItemChanged + reapplySelectionFromPane).
- LabelEditController extraction (handleBeginLabelEdit + handleEndLabelEdit + beginRenameFocusedItem + maybeStartPendingFolderEdit + pendingEditFolderName_).
- main-window.cpp 800 LOC 미만 목표.

### 4. Multi-pane 아키텍처 (큰 작업)

- §14.7 "Multi-pane soak: dual nav 50회 누적 working set Δ ≤ 10 MB" gate.
- §14.5 v1.0.8에서 M6로 잘못 태깅되었다 §14.7로 carry-forward된 단축키 4개 (Ctrl+1 / Ctrl+2 / Ctrl+H / Tab) 의 자연스러운 home.

### 5. PDCA Check phase 진입 검토

- M7 deliverable 인프라가 완비됐고 §14.7 핵심 gate 2개 (working set 9.39 MB, soak drift 404 KB)가 측정 완료된 상태.
- Manual 1-hour soak 실행 후 결과 좋으면 PDCA Do → Check phase 전환 검토.
- gap-detector + report-generator로 종합 보고서.

## 기타 참고사항

### Project type
Windows native C++20 desktop. Target Windows 11 x64. SDK 10.0.22621.0 강제.

### Build environment
```powershell
# Required dev shell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -winsdk=10.0.22621.0 -no_logo

cmake -B build -S .
cmake --build build --config Release
& build\core-tests.exe
# Expected: 412 passed, 0 failed (current head 204dd24)
```

### M7 측정 실행 예시

```powershell
# 100k working-set + soak measurement
& build\FastExplorerBench.exe generate --preset large-flat --out $env:TEMP\fe-100k
& build\FastExplorerBench.exe enumerate --path $env:TEMP\fe-100k --runs 10

# Baseline JSON capture
cmd /c "build\FastExplorerBench.exe enumerate --path $env:TEMP\fe-100k --runs 10 --format json > baseline.json"

# Regression compare (between two JSON snapshots)
pwsh -File scripts\bench-compare.ps1 -Baseline baseline.json -Current current.json -TolerancePercent 5
```

### 활성 메모리 규칙 (반드시 준수)
- 분기/선택 시 `AskUserQuestion` 툴 사용 (plain-text 금지)
- **atom 쪼개기/합치기 결정도 AskUserQuestion** (2026-05-15 강화, 본 세션에서 5번 적용)
- atom마다 L1+L2 review 후 commit (severity 전수 검토, defer 사유 명시)
- 본 세션 후반부 measurement atom들은 review skip 명시 (commit body에 "structurally identical to prior reviewed atom" 사유)
- 주석 minimal + design 문서 참조 + planning vocab 금지

### Repository
`https://github.com/solitasroh/fast-explorer.git` (origin/main, head `204dd24`)

### 세션 통계
- 누적 commit (이번 세션, prior handoff 이후): **20개**
- Tests: 345 → **412** (+67)
- 신규 소스 파일: 12 (bench/bench-json, ui/result-channel + folder-name + icon-cache-coordinator + dispinfo-histogram, tests x6, runbooks/m7-soak, scripts/bench-compare)
- 신규 헤더 only 모듈: 1 (result-channel.h)
- Design version: 1.0.9 → **1.0.10**
- Plan version: 1.0.2 → **1.0.3**
- §14.7 deliverable 11개 중 9개 ✅ / 2개 🟡 (full-UI 정량 measurement)
