---
date: 2026-05-16
git_commit: 903f00b
branch: main
status: handoff
project_type: windows-desktop-cpp
---

# 핸드오프: M5 → tech-debt sweep → M6 완료, M7 진입 준비

## 작업 내용

PDCA Phase: **Do (active)** — milestone 진행도 M1–M6 ✅ / M7 ⏳.

이 세션에서 M5 sub-step 1부터 M6 close까지 단일 세션에 완수. 누적 commit 34개.

### M5 Sorting + Selection (이번 세션 완결)

7 atoms — sort 도메인부터 stable selection까지.

| Atom | Commit | 내용 |
|---|---|---|
| 1 | `35c86e4` | SortKey + compareEntries (pure tri-state) |
| 2 | `a3a3bef` | visibleOrder permutation + sort(SortSpec) |
| 3a | `8936553` | LVN_GETDISPINFO → visibleAt 라우팅 |
| 3b | `03b9067` | LVN_COLUMNCLICK + requestSort + sort indicator |
| 4a | `28641ab` | GETDISPINFO race 해소 (kMaxEntries reserve + publishedCount atomic) |
| 4b | `5542fd6` | Background sort worker + 2k threshold |
| 5a | `42826be` | Stable selection by raw entries index (domain) |
| 5b | `5985570` | LVN_ITEMCHANGED + sort 후 reapply UI 통합 |
| close | `063cf2c` | Exit-gate 측정 + Design §14.5 Completed |

**Exit gates**:
- medium(10k) Name asc sort **2.75 ms** (50 ms budget의 5.5%)
- 100k UI-stall histogram + sort accept-latency jitter → M7 defer

### Tech-debt sweep (6 atoms)

M5에서 누적된 defer 항목 회수 + 표준 정합성 강화.

| Atom | Commit | 내용 |
|---|---|---|
| 1 | `5652b33` | handleMessage 178→25 lines + finalizeSortApply DRY |
| 2 | `90ca706` | wndProc C++ exception boundary (try/catch + WM_CREATE -1) |
| 3 | `9cc4625` | stopAndJoin helper + setSortThresholdRowsForTest |
| 4 | `f48030d` | PaneSortCoordinator 추출 (SRP) |
| 5 | `c4691da` | PaneSortCoordinator 단독 단위 테스트 (10 cases) |
| 6 | `092e5da` | FileModelStore vector → unique_ptr<T[]> + atomic<uint32_t> workerSize_ |

특히 #6은 vector 동시 접근 표준 UB(이전 reserve로 실제 race는 없었으나 letter of standard에 reserve 미보장) 정공법 해소: 모든 외부 API 보존, m5-bench 2.75 ms → 2.15 ms.

또한 sweep commit (`bd7f8db`): src/ 전체에서 Design 문서 참조 + planning vocabulary (MVP scope, milestone, later 등) 9곳 제거.

### M6 Icons + Basic Operations (이번 세션 완결)

| Atom | Commit | 내용 |
|---|---|---|
| 1 | `466ff17` | IconCache + folder/file placeholder + LVS_SHAREIMAGELISTS |
| 2 | `5fa1646` | ExtensionIconCache LRU 도메인 (case-insensitive normalize) |
| 3a | `712bdca` | IconProvider STA jthread skeleton (CoInitializeEx + cv stop_token) |
| 3b | `35101ab` | 실제 SHGetFileInfoW + result queue + UI handler + PostMessage coalescing |
| 4 | `dc2c5e0` | ShellExecuteExW openItem + LVN_ITEMACTIVATE + core::joinPath |
| 5a | `54ca2d5` | ShellWorker STA skeleton + ShellCommandKind enum |
| 5b | `df13aeb` + `9d8d798` | IFileOperation Delete (recycle bin) + retrospective fixes |
| 5c | `aa9519e` | Rename + CreateFolder + ComScope<T> RAII guard |
| close | `903f00b` | Design §14.6 Completed (icons + file ops) / Partial 마킹 |

**Exit gates** (M6):
- Icon load delay: by construction met (placeholder 동기 반환, SHGetFileInfoW는 worker)
- OneDrive hydration: 0건 (SHGFI_USEFILEATTRIBUTES로 디스크 stat 없음)
- ImageList cap 258 KB ≪ 3 MB
- Deferred to M7: OperationResult 구조화, IFileOperationProgressSink, low-memory shrink, portable crash dump

### 메모리 규칙 갱신

세션 중 사용자 지적 → 메모리 강화 두 번:
1. `feedback_use_question_tool.md` — atom 쪼개기/합치기 결정도 분기 결정이라 AskUserQuestion 강제 (2026-05-15).
2. 본 세션 5b commit에서 L1/L2 review 누락 → retrospective 보정 + 다음 atom에서는 정상 순서 복구.

## 핵심 참조 문서

- `docs/02-design/features/fast-explorer-core.design.md` v1.0.9 — §14.1–§14.6 모두 Completed (M6는 partial). §14.7 Pending.
- `docs/01-plan/features/fast-explorer-core.plan.md` — §7 schedule M1–M6 Completed/Partial, M7 Pending.

## 누적 산출물 (이번 세션 신규 파일)

### Core
- `src/core/file-sort.{h,cpp}` — Tri-state comparator + lessEntries adapter

### UI 신규 모듈
- `src/ui/pane-sort-coordinator.{h,cpp}` — Sort 도메인 분리
- `src/ui/jthread-utils.h` — stopAndJoin shared helper
- `src/ui/icon-cache.{h,cpp}` — HIMAGELIST + placeholder
- `src/ui/extension-icon-cache.{h,cpp}` — Extension → slot LRU
- `src/ui/icon-provider.{h,cpp}` — STA icon worker
- `src/ui/shell-worker.{h,cpp}` — STA IFileOperation worker

### 신규 테스트 파일
`file-sort-tests.cpp`, `pane-sort-coordinator-tests.cpp`, `icon-cache-tests.cpp`, `extension-icon-cache-tests.cpp`, `icon-provider-tests.cpp`, `shell-worker-tests.cpp`

### 수정된 핵심 파일
- `src/core/file-model-store.{h,cpp}` — raw array + workerSize_ atomic + publishedCount + applySortedOrder + kMaxEntries
- `src/core/file-entry.h` — Design 참조 주석 제거
- `src/core/path-utils.{h,cpp}` — joinPath 추가, 주석 정리
- `src/ui/pane-controller.{h,cpp}` — visibleAt 라우팅, requestSort/applyPendingSort 위임, selectRaw 도메인, openItem, ShellExecuteExW
- `src/ui/main-window.{h,cpp}` — handleMessage decomposition (11 on*), finalizeSortApply, LVN_ITEMACTIVATE/COLUMNCLICK/ITEMCHANGED, onIconBatch, resolveIconIndex, ScopedFlag RAII, wndProc try/catch

## 학습한 내용

### 빌드 환경 (변경 없음)
- Visual Studio 2026, MSVC v143, C++20, `/MD /W4 /permissive- /utf-8`
- SDK 강제: `Enter-VsDevShell ... -winsdk=10.0.22621.0`
- Build: `cmake --build build --config Release`
- Tests: `& build\core-tests.exe` (345/345 pass)

### Win32 / COM 패턴 (이번 세션 학습)

**LVS_OWNERDATA + LVS_SHAREIMAGELISTS**:
- LVS_NOSORTHEADER 제거하면 LVN_COLUMNCLICK 수신.
- LVS_SHAREIMAGELISTS는 ImageList 소유권을 우리에게 유지 — IconCache RAII가 안전.

**IFileOperation idiom**:
- FOF_ALLOWUNDO + FOFX_RECYCLEONDELETE 함께 사용해야 quota 초과 시도 silent 영구삭제 회피.
- FOF_NOCONFIRMATION + FOF_NOERRORUI + FOF_SILENT로 dialog 모두 억제.
- `SHGetFileInfoW(any_path, FILE_ATTRIBUTE_*, SHGFI_USEFILEATTRIBUTES)`는 디스크 안 건드림 (cloud hydrate 회피).
- `SHCreateItemFromParsingName` + `IFileOperation::{Delete|Rename|New}Item` + `PerformOperations` — STA 필수.

**STA worker 패턴**:
- `std::jthread` + `CoInitializeEx(COINIT_APARTMENTTHREADED)` + stop_token-aware `condition_variable_any::wait` + `CoUninitialize` on exit.
- 결과 channel: 작은 result queue + PostMessage (coalescing은 atomic<bool> postPending_).

**COM RAII**:
- `ComScope<T>` move-only template: dtor Release, `put()` → T**, IID_PPV_ARGS 호환.
- 모든 helper noexcept (COM은 C ABI라 C++ exception 안 던짐).

### 동시성 모델

**FileModelStore reader/writer**:
- entries_ + visibleOrder_는 std::unique_ptr<T[]> (reserve == kMaxEntries).
- workerSize_ atomic<uint32_t>: worker가 release-store, UI가 acquire-load.
- publishedCount_ atomic<uint32_t>: batch boundary visibility.
- UI는 publishedCount() 미만 인덱스만 read.

**sort 동시성**:
- workerActive_ (PaneController) atomic으로 enumeration 진행 중 sort 거부.
- Background sort 결과는 pendingSortedOrder_ + pendingSortGen_ → applyPendingSort로 commit (generation gate).
- requestSort sync 분기에서 leftover pendingSortedOrder_ 정리 — stale dispatch가 sync 결과 덮어쓰지 못함.

**wndProc 안전성**:
- try/catch(...) 외곽 (C++ exception이 wndProc 경계 넘으면 UB).
- WM_CREATE catch 시 -1 반환 (부분 생성 윈도우 방지).
- SEH는 의도적 통과 (/EHsc).

### 메모리 규칙 (memory/MEMORY.md)

1. **AskUserQuestion 강제** — 분기/선택 시 plain-text 금지. atom 쪼개기/합치기도 분기.
2. **Per-sub-step review** — atom마다 commit 전 L1+L2 spawn. 본 세션에서 5b retrospective로 강화.
3. **Severity 전수 검토** — Critical/High/Medium/Low 모두 검토 후 apply/defer + commit body 사유.
4. **주석 최소화** — Design/Plan 참조 + planning vocab (milestone, MVP, scope) 금지. src/ 전체 sweep 1회 수행.

## 다음 작업 항목

### M7 Benchmark + Stabilization (Design §14.7)

핵심 deliverable:
- Full dataset generator preset coverage
- UI stall probe + scroll p95 + LVN_GETDISPINFO p99 측정
- 100k entries process working set ≤ 100 MB
- Memory soak (100k→0→100k cycle)
- 1-hour soak test
- CI regression gate

### M6 잔여 (UI 통합 + 측정)

다음 세션에서 가능한 atom:
- `PaneController::deleteItem(row)` / `renameItem(row, name)` / `createSubfolder(name)` API
- UI 단축키: Delete / F2 / Ctrl+Shift+N
- `OperationResult` 구조화 + status-bar 채널
- ImageList byte-count probe + low-memory shrink

### Defer된 tech-debt 항목

- DRY-002: `IconProvider` + `ShellWorker` 공통 base (3-call site 등장 후 → 4th verb 시 자연)
- `runFileOp(path, verbFn)` higher-order helper (Move/Copy verb 추가 시점)
- `IconResult`/`IFileOperation` raw HICON ownership을 RAII wrapper로 (현재 move-only struct + dtor drain으로 누수 차단)
- L1 MEDIUM (FileModelStore primitive-obsession on two atomic uint32_t — strong type alias)
- PaneController shellOpenPath 분리 (단일 호출 site일 때까지 단일 클래스 응집 유지)

## 기타 참고사항

### Project type
Windows native C++20 데스크톱. Target Windows 11 x64. SDK 10.0.22621.0 강제.

### Test 실행
```powershell
& build\core-tests.exe
# Expected: 345 passed, 0 failed (current head 903f00b)
```

### Manual smoke
```powershell
$tmp = "$env:TEMP\fe-test"
& build\FastExplorerBench.exe generate --preset small --out $tmp
& build\FastExplorer.exe --open $tmp
# - 컬럼 헤더 클릭 → 정렬 + indicator
# - 100 row 이상이면 background sort, 아니면 sync sort
# - Enter / 더블클릭 → 폴더 진입 또는 ShellExecuteExW 실행
# - 외부에서 파일 추가 → 100 ms coalesce 후 list view 갱신
# - 확장자별 시스템 아이콘 비동기 로드 (.txt, .png 등)
Remove-Item -Recurse -Force $tmp
```

### 활성 메모리 규칙 (반드시 준수)
- 분기/선택 시 `AskUserQuestion` 툴 사용 (plain-text 금지)
- **atom 쪼개기/합치기 결정도 AskUserQuestion** (2026-05-15 강화)
- atom마다 L1+L2 review 후 commit (severity 전수 검토, defer 사유 명시)
- 주석 minimal + design 문서 참조 + planning vocab 금지
- 본 세션 5b retrospective 케이스 — review는 commit 전에 실행해야 함

### Repository
`https://github.com/solitasroh/fast-explorer.git` (origin/main, head `903f00b`)

### 세션 통계
- 누적 commit (이번 세션): 34개
- Tests: 244 → 345 (+101)
- 신규 소스 파일: 13 (core 1, ui 7, tests 5)
- 신규 테스트 케이스: 100+
- Design version: 1.0.7 → 1.0.9
