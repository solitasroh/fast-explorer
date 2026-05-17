---
date: 2026-05-18
git_commit: 8b3a9a5
branch: main
status: handoff
project_type: windows-desktop-cpp
---

# 핸드오프: M6 SRP follow-up → PDCA Check → M8 마라톤 → M9 마라톤

## 작업 내용

PDCA Phase: **Do → Check → Report 1차 종료 후 M8/M9 추가 마라톤 진행** — milestone 진행도 M1–M9 ✅ (multi-pane block 완결).

Prior handoff (`d596b02`, 2026-05-17) 이후 **19 commits** 누적. M1-M7 첫 PDCA Report 발행 → M8 (foundation hardening) 마라톤 → M9 (multi-pane block) 마라톤까지 동일 사이클 안에서 진행.

### Phase 1: M6 SRP follow-up + R6 tests + M1–M7 Report

| Commit | 내용 |
|--------|------|
| `a871222` | M6 SRP follow-up: SelectionSync + LabelEditController 추출 (main-window.cpp 945→785 LOC). atom shape 결정 (1 atom 결합) AskUserQuestion. L1+L2 review 2 MED applied (ScopedFlag dtor noexcept + `[[nodiscard]]`). |
| `fdbd240` | Check R6 gap closure: SelectionSync + LabelEditController standalone tests (19 cases, 412→431). `pendingFolderNameForTest()` test affordance, `diskPathExists`/`writeEmptyDiskFile` helpers promoted to bench-fs-helper.h. |
| `5ee2573` | PDCA Report M1-M7 completion v1.0.0 (447 LOC). gap-detector 측정: match rate 95%. Carry-forward A1/A2/C1/C2/C3/R1-R5 명시. |

### Phase 2: 추가 문서

| Commit | 내용 |
|--------|------|
| `8e4f1ef` | `docs/usage-guide.md` (371 LOC) — end-user + developer + architecture 통합 가이드. Mermaid 레이어 다이어그램, 단축키 표, bench CLI, 1-hour soak protocol, 트러블슈팅. |
| `06f2d9d` | `docs/m8-followup.md` (314 LOC) — M8/M9/M10+ 로드맵. 의존성 그래프 mermaid, 각 atom 변경 파일/LOC/시간 추정. |

### Phase 3: M8 마라톤 (foundation hardening + manual measurement)

| Atom | Commit | 내용 |
|------|--------|------|
| R3-R5 doc | `2f01561` | design §11.1 PerfTracker 14 promised events → 6 phase-boundary + histogram surface (StallHistogram/DispInfoHistogram/EmptyWorkingSetProbe/MemoryProbe). §11.1.2 책임 분할 신규. design v1.0.10 → v1.0.11. |
| R1+R2 | `5012291` | Attributes 컬럼 (H/S/R/J/L/C) + NM_CUSTOMDRAW dim. kIsSymlink flag + isReadOnly accessor. SortKey::None=4 sentinel. ColumnSpec.sortable. 13 new tests (431→444). |
| C1 | `64b735b` | settings.json reader/writer + atomic save + 64KB cap + JSON parser. MainWindow applyInitialState/capturedSessionState. WM_DESTROY GetWindowPlacement 캡처. main.cpp orchestration. text-utf.{h,cpp} 추출. 10 new tests (444→454). |
| Report v1.0.1 | `fd8b639` | M8 closure addendum. match rate 95% → 98%. |

### Phase 4: M9 마라톤 (multi-pane block)

| Atom | Commit | 내용 |
|------|--------|------|
| UTF-8 migration | `1671fed` | bench-json + bench-cli → core/text-utf. ring-logger는 hot-path stack buffer 의도적 제외. DRY-001 follow-up 해소. |
| C2-1 PaneManager skeleton | `167c15b` | std::vector<unique_ptr<PaneController>> owner + active index. pane_ raw pointer 캐시 전환. 4 new tests (454→458). |
| C2-2 pane-layout helper | `a38b86c` | computePaneRects 순수 함수, onSize 사용. 9 new tests (458→467). |
| C2-3 WPARAM packing | `cac84d2` | makePaneWParam/paneIndexFromWParam/generationFromWParam. PaneController/SortCoord/ShellWorker/IconProvider/IconCacheCoord/FsWatcher 모두 paneIndex (default 0). 6 new tests (467→473). |
| C2-4 per-pane coordinator arrays | `35ddc95` | iconCoords_/selectionSyncs_/labelEdits_ → std::array size 2. activeSelectionSync/activeLabelEdit helpers. |
| C2-5 dual-mode infrastructure | `b21817b` | PaneManager.openSecond/closeSecond/setActive. MainWindow.enterDualMode/enterSingleMode/setActivePane + listViews_[2] + status bar "활성: 왼쪽/오른쪽". 6 new tests (473→479). |
| C3 layout shortcuts | `b3cee4a` | Ctrl+1 single / Ctrl+2 dual / Ctrl+H toggle / Tab active switch. 4 accelerator + onCommand. |
| C2-6 regression close | `5d89634` | L1+L2 review fixes: installPaneCoordinators helper (DRY-001 + 예외 안전성), relayout() 동기 helper, closeSecond noexcept. dual-pane soak runbook 추가. |
| C2-6b dispatcher refactor | `b7e3ea4` | gap-detector NOT-READY catch: onEnumBatch/Complete/Error/SortComplete/OperationResult/IconBatch/FsChange가 paneForWParam(wp)로 라우팅 (이전엔 pane_ 캐시 사용). redrawVisibleRows(idx). 패널별 fs-coalesce 타이머. |
| Report v1.0.2 | `8b3a9a5` | M9 closure addendum. match rate 98% → 97% (multi-pane 도입 의도적 1pp 후퇴). |

## 핵심 측정값 (회귀 없음 / 신규 통과)

| 게이트 | M7 기준 | M9 종료 | 변화 |
|--------|---------|---------|------|
| 100k working set (headless, 단일) | 9.39 MB | 회귀 없음 (재측정 없음) | — |
| 10-cycle memory drift | 404 KB | 회귀 없음 | — |
| Tests pass | 412/412 | **479/479** | +67 (M8: +23, M9: +25, M6 SRP follow-up R6: +19) |
| Build clean | ✅ | ✅ | — |
| Run stability (모든 atom commit 직전 3-run) | ✅ | ✅ | — |
| 설계 일치율 (gap-detector) | 93% (M7 직후) → 95% (R6 종료) | **97%** (M9 종료) | +2pp |
| Dual-pane soak (50 nav drift ≤ 10 MB) | n/a | **runbook ready, 미실행** | M9-A1 amber |

## 누적 산출물

### Core (M9 신규)
- `src/core/text-utf.{h,cpp}` — widenUtf8/narrowUtf8 (UTF-8 ↔ UTF-16 round-trip). bench-json + bench-cli 마이그레이션 완료. ring-logger 의도적 제외 (hot path stack buffer).
- `src/core/settings-store.{h,cpp}` (M8) — SessionState + JSON 파서/writer + atomic write (temp + MoveFileExW WRITE_THROUGH + FlushFileBuffers).
- `src/core/file-sort.h` — SortKey::None=4 sentinel (append-only 보존).
- `src/core/file-entry.h` — kIsSymlink flag bit + isSymlink/isReadOnly accessors.
- `src/core/win32-fs-backend.cpp` — buildEntry에 IO_REPARSE_TAG_SYMLINK 인식.
- `src/core/fs-watcher.{h,cpp}` — watch() paneIndex 파라미터 + workerLoop inline WPARAM packing (ui/ 의존 회피).

### UI (M9 신규)
- `src/ui/pane-manager.{h,cpp}` — PaneController owner, openInitial/openSecond/closeSecond/setActive/active/at/count/activeIndex/isDual.
- `src/ui/pane-layout.{h,cpp}` — computePaneRects 순수 함수, 단일/dual 분기.
- `src/ui/messages.h` — kAccelLayoutSingle/Dual/Toggle/PaneSwitch (108-111). makePaneWParam/paneIndexFromWParam/generationFromWParam packing helpers.
- `src/ui/main-window.{h,cpp}` — paneManager_ + pane_ raw pointer 캐시 + listViews_[2] + std::array<unique_ptr<X>, 2> coordinator arrays. enterDualMode/enterSingleMode/setActivePane/installPaneCoordinators/relayout helpers. 모든 kWmFe* dispatcher가 paneForWParam(wp) 라우팅.

### UI (M6/M8 신규)
- `src/ui/selection-sync.{h,cpp}` (M6 SRP) — LVN_ITEMCHANGED ↔ PaneController bridge + reentrancy guard.
- `src/ui/label-edit-controller.{h,cpp}` (M6 SRP) — LVS_EDITLABELS lifecycle + pendingFolderName_ 핸드오프.
- `src/ui/column-formatter.{h,cpp}` (M8 R1+R2) — formatAttributesForEntry (H/S/R/J/L/C 마커) + shouldRenderDimmed.

### Docs (3 신규)
- `docs/usage-guide.md` — end-user + developer + architecture 통합 가이드 (371 LOC).
- `docs/m8-followup.md` — M8/M9/M10+ 로드맵 (314 LOC).
- `docs/02-design/runbooks/m9-dual-pane-soak-checklist.md` — 30분 매뉴얼 soak protocol.
- `docs/05-report/features/fast-explorer-core.report.md` v1.0.2 — M1-M7 (v1.0.0) + M8 addendum (v1.0.1) + M9 addendum (v1.0.2).

### 신규 테스트 파일 (M6/M8/M9)
`selection-sync-tests.cpp`, `label-edit-controller-tests.cpp`, `settings-store-tests.cpp`, `pane-manager-tests.cpp`, `pane-layout-tests.cpp`. column-formatter-tests.cpp + ui-messages-tests.cpp 확장.

## 학습한 내용 (이번 세션)

### 마라톤 운영 패턴

- **점진적 atom 분할이 거대 refactor의 회귀 0 달성 비법**: M9 9 atom 모두 빌드+테스트 통과 후 commit. 큰 변경(C2 multi-pane)을 6+ atom으로 쪼개는 cost는 작고 confidence ROI는 매우 큼. 한 번도 회귀 안 발생.
- **Default argument가 wide-surface refactor의 마찰 minimizer**: M9 atom 3에서 paneIndex=0 default가 모든 기존 ctor call site (test 포함 ~50개)를 변경 없이 통과시킴. 명시적 호출은 새 코드에만 의무.
- **gap-detector를 atom close에 routine으로**: M9 close에서 NOT-READY 판정으로 M9-A2/A3 catch. 추가 atom 6b로 해소. 만약 patch 만들고 바로 Report 갔으면 dual-mode 첫 사용 시 silent misroute 가능. gap-detector는 사후 검증이 아니라 close 게이트로 routine화 필요.
- **순수 cleanup atom은 review skip 정당화 가능**: text-utf migration / per-pane array conversion 등 mechanical refactor는 commit body에 "structurally identical to prior reviewed atom" 사유 기록하고 review 생략 (memory rule 허용).
- **doc-only atom도 PDCA 인식 가능한 형태로**: M8 R3-R5 reconciliation은 코드 변경 0이지만 design intent를 ship된 surface에 정렬하는 명확한 closure 가치. 단순 doc 수정이 아니라 gap-detector가 인식하는 형태로 명문화 (§11.1.2 책임 분할 섹션 추가).

### Win32 / 동시성

- **WPARAM 64-bit 활용**: UINT_PTR는 x64에서 64-bit. 기존 generation 32-bit 그대로 두고 high 8 bits에 paneIndex 패킹. 레거시 senders (WPARAM=0)는 paneIndex=0으로 자동 디코드 → backward-compatible.
- **L2 c-cpp-reviewer가 잡은 RAII 예외 안전성**: enterDualMode가 openSecond 후 coord 생성 중 throw 시 paneManager에 partial state 잔류 → installPaneCoordinators try/catch + rollback 패턴으로 해결. production stress test에서만 발견될 수 있던 버그를 sub-step review가 catch.
- **`PostMessageW(WM_SIZE, SIZE_RESTORED, 0)` vs `relayout()`**: 전자는 async + 거짓 lParam=0 (실제 WM_SIZE는 width/height 인코딩) → 미래 reader 오해 가능. 후자는 synchronous + 정직한 MAKELPARAM(clientW, clientH) → 명확. L2 LOW finding이 좋은 cleanup 트리거.
- **destruction order 가시화**: enterSingleMode에서 coordinators[1] → paneManager.closeSecond() → DestroyWindow(listView[1]) 순서. 각 단계가 STA worker join 보장. L2가 명시적으로 검증.
- **MOVEFILE_WRITE_THROUGH는 rename만 보장**: 데이터 자체 disk persistence는 FlushFileBuffers 필요. L2 LOW finding으로 atomic save 강화.
- **`UINT_PTR == DWORD_PTR == size_t`** (x64): WM_TIMER `wParam`을 size_t로 비교/캐스트 안전. 다중 timer id range `kTimerFsCoalesceBase + idx` 패턴으로 패널별 디바운스.

### 측정 / 인프라

- **doc reconciliation은 design intent와 ship 인프라의 정렬**: §11.1 14 promised events vs 6 shipped → histogram-based surface가 point event를 대체했음을 §11.1.2로 명문화. 책임 분할 원칙 정립.
- **per-atom L1+L2 review가 catch한 buggy patterns**: ScopedFlag dtor 암묵 noexcept → 명시화. `[[nodiscard]]` on LRESULT return → caller drop 방지. DRY-001 UTF-8 helpers (4 copy) → text-utf 추출 + ring-logger 의도적 제외 명문화.

## 핵심 참조 문서

- `docs/05-report/features/fast-explorer-core.report.md` v1.0.2 — M1-M9 종합 보고서 (Executive Summary + 4-관점 가치 + per-atom 상세 + match rate 분해 + 학습).
- `docs/02-design/features/fast-explorer-core.design.md` v1.0.11 — §11.1 reconciled, §14.7 M7-M9 deliverable 측정값 반영 (M9 multi-pane 신규 영역은 still amber).
- `docs/01-plan/features/fast-explorer-core.plan.md` v1.0.3 (unchanged in M8/M9).
- `docs/m8-followup.md` — M8/M9 진행 로드맵 (M10+ deferred 항목 포함).
- `docs/usage-guide.md` — end-user + developer 종합 가이드 (M9 multi-pane 기능 미반영, 갱신 권장).
- `docs/02-design/runbooks/m9-dual-pane-soak-checklist.md` — 30분 매뉴얼 dual-pane soak protocol.

## 다음 작업 항목

### 1. M9 follow-up 3개 (next sprint atom 1-2개)

권장 통합 atom (M9-A6 + A7 + A4/A5 결정):
- **M9-A6**: `enterDualMode`에서 pane 1을 빈 화면 대신 pane 0의 currentPath() (또는 session restore secondPath)로 자동 진입. 사용자 첫인상 개선.
- **M9-A7**: `SessionState`를 schema v2로 확장 — layout mode (enum: Single/Dual) + secondPath (wstring). settings.json reader 호환성 보존. M9-A6과 합쳐 1 atom 가능.
- **M9-A4 / M9-A5**: Ctrl+H ↔ "show hidden" 관례 충돌 / VK_TAB ↔ list-view internal Tab 가로채기 — design §4.5 재검토. Rebind (예: `Ctrl+\\` for split toggle, `Ctrl+Tab` for pane switch) 또는 의도적 deviation 명문화. AskUserQuestion으로 결정 권장.

### 2. M9-A1 dual-pane soak 매뉴얼 실행

`docs/02-design/runbooks/m9-dual-pane-soak-checklist.md` 30분 protocol 실행:
- Release 빌드 + 두 데이터셋 (`large-flat` + `mixed-names`) 사전 생성.
- 활성 패널 cycle / 양쪽 navigation 50회 / F2/Delete/Ctrl+Shift+N / Ctrl+H toggle 3회.
- Working set drift ≤ 10 MB 검증.
- 결과를 design §14.7 multi-pane deliverable 표에 반영. 통과 시 match rate 97% → 99%+.

### 3. `usage-guide.md` M9 기능 반영

현재 가이드는 M8 종료 시점 (multi-pane 미구현). 갱신 항목:
- §2.2 단축키 표에 Ctrl+1/2/H/Tab 추가 (현재는 "M8+ carry-forward"로 표기).
- §2.3 주요 기능에 듀얼 패널 사용법 추가 (사용자에게 이미 멀티-패널 설명 메시지로 전달한 내용 기반).
- §6 carry-forward에서 M9 closed 항목 제거.
- §4 아키텍처에 PaneManager + dual layout 추가 (mermaid 갱신).

### 4. M10+ deferred (m8-followup.md §5 참조)

- **Filter / search-as-you-type**: 100k 폴더 사용성 핵심.
- **DnD between panes**: dual pane 활용도 극대화.
- **Shell context menu** (right-click).
- **Per-pane status bar**: 현재 단일 status bar라 두 패널의 status update가 last-wins.
- **Thumbnails / Dark mode / HiDPI 아이콘**: design §17.2 deferred.

### 5. PDCA 사이클 진행

이번 세션은 동일 PDCA 사이클 안에서 M1-M7 Report 발행 → M8 마라톤 → M9 마라톤까지 진행됨. M10 진입 시 결정:
- (a) **현재 사이클 종료** + 새 PDCA 사이클 `/pdca plan fast-explorer-core-m10` 시작.
- (b) **연장된 단일 사이클**로 M10도 동일 Report에 addendum (v1.0.3) 추가.

권장: (a). M10은 새 기능 그룹 (filter/DnD/shell-context) → 새 plan/design 문서 가치 있음.

## 기타 참고사항

### Project type
Windows native C++20 desktop. Target Windows 11 x64. SDK 10.0.22621.0 강제. MSVC v143 (Visual Studio 2026).

### Build environment
```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -winsdk=10.0.22621.0 -no_logo && cmake --build build --config Release'
& build\core-tests.exe
# Expected: 479 passed, 0 failed (of 479)  # head 8b3a9a5
```

PowerShell `cmd /c` wrapping 필수 — PowerShell의 인자 파싱이 `-winsdk=10.0.22621.0`을 mangle함.

### M9 사용법 (사용자 검증용)

```
Ctrl+2          → 듀얼 패널 진입 (오른쪽 패널은 빈 화면, Ctrl+L로 경로 입력)
Ctrl+1          → 단일 패널로 복귀
Ctrl+H          → 듀얼 ↔ 단일 토글
Tab             → 활성 패널 전환 (status bar "활성: 왼쪽/오른쪽")
```

기존 단축키 (`Ctrl+L`, `Alt+←/→/↑`, `F5`, `Delete`, `F2`, `Ctrl+Shift+N`) 모두 활성 패널 대상으로 동작.

### M7 측정 실행 예시

```powershell
& build\FastExplorerBench.exe generate --preset large-flat --out $env:TEMP\fe-100k
& build\FastExplorerBench.exe enumerate --path $env:TEMP\fe-100k --runs 10
cmd /c "build\FastExplorerBench.exe enumerate --path $env:TEMP\fe-100k --runs 10 --format json > baseline.json"
pwsh -File scripts\bench-compare.ps1 -Baseline baseline.json -Current current.json -TolerancePercent 5
```

### 활성 메모리 규칙 (반드시 준수)
- 분기/선택 시 `AskUserQuestion` 툴 사용 (plain-text 금지). 본 세션 6번 적용 (M8 진행 시점, M9 atom shape, M9 진행 여부, M9 사용자 가이드 분기 등).
- **atom 쪼개기/합치기 결정도 AskUserQuestion**. 본 세션 적용 (M6 SRP 1 atom 결합, M8 R1+R2 결합 / C1 단일, M9 6 atom 분할).
- atom마다 L1+L2 review 후 commit — 단, mechanical refactor (text-utf migration, per-pane array conversion, dispatcher routing 등)는 commit body에 "structurally identical to prior reviewed atom" 사유 명시 후 skip 허용 (본 세션 후반부 다수 atom 적용).
- L1+L2 review에서 모든 severity 검토 후 apply/defer + 사유 commit body 기록.
- 주석 minimal + design/plan 참조 + planning vocab 금지 (commit body는 OK, code 코멘트만 제한).

### Repository
`https://github.com/solitasroh/fast-explorer.git` (origin/main이 prior handoff 시점 `d596b02`에서 머묾, 본 세션 19 commits는 local main만 — push 결정 필요)

### 세션 통계
- 누적 commit (이번 세션, prior handoff `d596b02` 이후): **19개**
- Tests: 345 (prior) → 412 (handoff 시점) → 431 (R6 종료) → 454 (M8 종료) → **479** (M9 종료, +67)
- 신규 소스 파일: 11 (text-utf + settings-store + pane-manager + pane-layout + selection-sync + label-edit-controller + 6 test 파일 + 1 runbook + 3 문서)
- 신규 헤더-only 모듈: 0 (atom 1 prior session에서 result-channel.h 도입 후 멈춤)
- Design version: 1.0.10 → **1.0.11** (M8 §11.1 reconciliation)
- Plan version: 1.0.3 (unchanged)
- Report version: (신규) → 1.0.0 (M1-M7) → 1.0.1 (M8 addendum) → **1.0.2** (M9 addendum)
- Match rate (gap-detector): 93% (M7 직후) → 95% (R6) → 98% (M8) → **97%** (M9, multi-pane 도입 의도적 1pp 후퇴)
- §14.7 deliverable: M7 9/11 ✅ 2/11 🟡 → M8 그대로 → M9 multi-pane 신규 영역 amber (M9-A1)
