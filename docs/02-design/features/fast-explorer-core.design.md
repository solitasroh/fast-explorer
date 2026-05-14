# fast-explorer-core - Design Document

> **Summary**: Windows native file explorer MVP architecture for instant local-folder responsiveness, cancellable background work, virtualized rendering, and repeatable performance measurement.
>
> **Author**: Codex
> **Created**: 2026-05-14
> **Status**: Review
> **Version**: 1.0.0
> **Level**: Starter

---

## Version History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 1.0.0 | 2026-05-14 | Initial technical design document | Codex |

## Related Documents

- Plan: [fast-explorer-core.plan.md](../../01-plan/features/fast-explorer-core.plan.md)
- Analysis: `docs/03-analysis/fast-explorer-core.analysis.md` 예정
- Report: `docs/04-report/features/fast-explorer-core.report.md` 예정

---

## 1. Overview

`fast-explorer-core`는 로컬 디스크 폴더를 빠르게 여는 Windows native file explorer MVP다. 제품의 우선순위는 기능 수가 아니라 "폴더 진입 후 첫 화면이 즉시 보이고, 대용량 폴더에서도 UI가 멈추지 않는 것"이다.

이 설계는 Plan 문서의 목표를 구현 가능한 구조로 고정한다.

- UI: C++20, Win32, common controls 기반 native desktop app
- File list: Win32 List-View `LVS_OWNERDATA` virtual list 우선
- Core: C++ native file system engine
- Work model: UI thread와 background work를 명확히 분리
- Performance: first visible rows, UI stall, memory, sort timing을 MVP gate로 측정
- Safety: 파일 작업은 Shell API 중심으로 안전성을 우선

MVP에서 가장 중요한 기술 명제는 다음과 같다.

> UI thread must never wait for directory enumeration, shell metadata, icon extraction, sorting, or file operations.

---

## 2. Design Decisions

### 2.1 Platform

| Item | Decision | Reason |
|------|----------|--------|
| Language | C++20 | Windows API, COM, native UI 제어를 단순하게 유지 |
| Target OS | Windows 11 x64 first, Windows 10 best-effort | MVP 성능 검증을 Windows 11 기준으로 고정 |
| Compiler | MSVC | Windows desktop API, Visual Studio debugger, native profiling |
| Build | CMake + MSVC generator | app, benchmark, tests가 core library를 공유하기 쉬움 |
| UI framework | Win32 + common controls | 낮은 런타임 비용과 message loop 직접 제어 |
| Rendering | `LVS_OWNERDATA` List-View first | 100k+ row 처리 검증을 가장 빠르게 시작 |
| Custom render | Deferred | List-View 한계가 측정될 때 Direct2D/DirectWrite로 이동 |
| External dependencies | Avoid by default | 성능/빌드 복잡도 리스크를 낮춤 |

### 2.2 MVP Scope Decisions

| Question | Decision |
|----------|----------|
| Single, dual, or quad layout | MVP는 single + dual을 구현한다. Quad는 `PaneManager` 구조상 확장 가능하게 두되 첫 MVP gate에는 포함하지 않는다. |
| Shell context menu | MVP 제외. UI thread block과 third-party shell extension 리스크가 크다. |
| Drag-and-drop | MVP 제외. file operation 안정화 이후 별도 설계한다. |
| Icons | 포함하되 background batch loading만 허용한다. 파일명 표시를 지연시키면 실패다. |
| Thumbnails | MVP 제외. |
| Folder size calculation | 자동 계산 제외. |
| File operations | open, rename, create folder, recycle-bin delete만 포함한다. |
| Benchmark harness | 별도 CLI + app instrumentation 둘 다 둔다. |
| Settings storage | `%LOCALAPPDATA%\FastExplorer\settings.json` 파일을 사용한다. |
| Test harness | 초기에는 dependency-free `core-tests.exe`를 둔다. 필요 시 Catch2/doctest를 별도 결정한다. |

### 2.3 Performance Budget

| Budget | Target |
|--------|--------|
| Warm launch to interactive | <= 500 ms |
| Cold launch to interactive | <= 1,500 ms |
| Small folder first visible rows | <= 50 ms |
| Medium folder first visible rows | <= 100 ms |
| Large folder first visible rows | <= 200 ms |
| UI thread single stall | <= 50 ms |
| Folder switch cancellation | <= 50 ms |
| 100k base entries incremental memory | <= 100 MB excluding icons/thumbnails |

Budget을 만족하지 못하는 기능은 MVP에 들어가지 않는다.

---

## 3. Architecture

### 3.1 Process View

```text
FastExplorer.exe
  AppHost
    MainWindow
      CommandBar
      AddressBar
      PaneManager
        FilePane[1..2]
          VirtualFileList
          PaneStatus
      StatusBar

  Core
    NavigationController
    FileModelStore
    DirectoryEnumerator
    SortService
    TaskScheduler
    CancellationRegistry
    PerfTracker

  Shell
    IconProvider
    OperationService
    ShellWorker

FastExplorerBench.exe
  DatasetGenerator
  EnumerationBench
  SortBench
  ResultWriter
```

### 3.2 Layer Responsibilities

| Layer | Responsibility | Must Not Do |
|-------|----------------|-------------|
| `app` | process startup, COM init, message loop, crash boundary | enumerate folders directly |
| `ui` | windows, controls, pane layout, user commands | block on shell/core work |
| `core` | path model, enumeration, sorting, file entry storage | call UI APIs |
| `shell` | ShellExecute, icons, recycle-bin delete, COM operations | run on UI thread |
| `bench` | dataset generation and repeatable timings | depend on UI controls |
| `tests` | deterministic core checks | require user profile data |

### 3.3 Repository Structure

```text
CMakeLists.txt
src/
  app/
    main.cpp
    app-host.cpp
    app-host.h
  ui/
    main-window.cpp
    main-window.h
    pane-manager.cpp
    pane-manager.h
    file-pane.cpp
    file-pane.h
    virtual-file-list.cpp
    virtual-file-list.h
    address-bar.cpp
    address-bar.h
  core/
    path-utils.cpp
    path-utils.h
    file-entry.h
    file-model-store.cpp
    file-model-store.h
    directory-enumerator.cpp
    directory-enumerator.h
    navigation-controller.cpp
    navigation-controller.h
    sort-service.cpp
    sort-service.h
    task-scheduler.cpp
    task-scheduler.h
    cancellation-registry.cpp
    cancellation-registry.h
    perf-tracker.cpp
    perf-tracker.h
    result.h
  shell/
    icon-provider.cpp
    icon-provider.h
    operation-service.cpp
    operation-service.h
    shell-worker.cpp
    shell-worker.h
  bench/
    bench-main.cpp
    dataset-generator.cpp
    dataset-generator.h
    enumeration-bench.cpp
    enumeration-bench.h
    sort-bench.cpp
    sort-bench.h
tests/
  core-tests.cpp
docs/
  01-plan/
  02-design/
  03-analysis/
  04-report/
```

File names use kebab-case. C++ types use PascalCase. Functions use camelCase. Constants use UPPER_SNAKE_CASE.

---

## 4. UI Design

### 4.1 Main Window

첫 화면은 랜딩 페이지가 아니라 실제 파일 탐색 화면이다.

```text
+--------------------------------------------------------------------------------+
| CommandBar: Back Forward Up Refresh Layout NewFolder Rename Delete              |
| AddressBar: C:\Users\...\Downloads                                             |
+--------------------------------------------------------------------------------+
| Pane 1                                      | Pane 2                            |
| Path header                                 | Path header                       |
| Virtual file list                           | Virtual file list                 |
| Pane status                                 | Pane status                       |
+--------------------------------------------------------------------------------+
| Global status: ready / loading / selected / benchmark markers                   |
+--------------------------------------------------------------------------------+
```

### 4.2 Pane Layout

MVP layout modes:

- `single`: one focused file pane
- `dual-horizontal`: two panes left/right

Architecture-ready but not MVP gate:

- `quad`: 2x2 panes
- `dual-vertical`: two panes top/bottom

Each pane owns independent navigation history, cancellation generation, loading state, selection, and sort state.

### 4.3 File Pane States

| State | Meaning | UI Behavior |
|-------|---------|-------------|
| `empty` | no path loaded | show path prompt |
| `loading` | enumeration in progress and no batch rendered yet | show quiet loading status |
| `partial` | at least one batch visible, enumeration still running | list remains interactive |
| `ready` | enumeration complete | show item count and timing |
| `sorting` | sort worker active | keep current rows, show sort pending state |
| `error` | path cannot be opened | show explicit error and retry option |

### 4.4 Virtual File List

The first implementation uses Win32 List-View with `LVS_OWNERDATA`.

Required columns:

| Column | Source | Notes |
|--------|--------|-------|
| Name | `FileEntry.name` | always first priority |
| Type | extension or directory marker | shell type name excluded initially |
| Size | `FileEntry.size` | blank for folders |
| Modified | `FileEntry.modifiedTime` | locale formatting on UI thread only for visible rows |
| Attributes | cached flags | hidden/system/read-only markers |

Rules:

- Row count and row data are separated.
- The list asks for only visible row text.
- Formatting for visible rows must be cheap and cached when useful.
- Icon cells use placeholder icons until background results arrive.
- Selection is tracked by stable model ids, not raw visible indices alone.

### 4.5 Keyboard and Mouse Commands

| Command | Shortcut | Behavior |
|---------|----------|----------|
| Focus address bar | `Ctrl+L` | select current path |
| Navigate up | `Alt+Up` or toolbar | parent folder |
| Back / forward | `Alt+Left` / `Alt+Right` | per-pane history |
| Refresh | `F5` | cancels current load and reloads generation |
| Open selected | `Enter` | folder in pane or file default app |
| Rename | `F2` | single selected item |
| Delete to Recycle Bin | `Delete` | shell-backed recycle operation |
| New folder | `Ctrl+Shift+N` | create folder in current pane |
| Toggle layout | toolbar control | single/dual |

---

## 5. Core Data Model

### 5.1 FileEntry

`FileEntry` stores only the data needed for display, sorting, and safe operations. It does not duplicate full paths for every row.

```text
FileEntry
  EntryId id
  uint64_t generation
  std::wstring name
  std::wstring extension
  uint64_t size
  FileTime modifiedTime
  FileTime createdTime
  uint32_t attributes
  bool isDirectory
  bool isHidden
  bool isSystem
  IconState iconState
  MetadataState metadataState
  ErrorCode error
```

Full path construction:

```text
full_path = pane_root_path + "\\" + entry.name
```

Full paths are created lazily for operations, visible row requests that require them, or shell calls.

### 5.2 FileModelStore

`FileModelStore` is the pane-local storage for entries.

Responsibilities:

- append enumeration batches
- expose item count to virtual list
- return visible row data by index
- maintain selected stable ids
- maintain `visibleOrder` for sorting
- clear stale generations
- report memory estimate for performance diagnostics

Internal shape:

```text
FileModelStore
  rootPath
  generation
  entries: vector<FileEntry>
  visibleOrder: vector<uint32_t>
  sortState
  selectionState
```

For unsorted initial load, `visibleOrder` may be identity or omitted. For sorted view, it stores indices into `entries`.

### 5.3 Result Model

Core and shell operations return explicit results.

```text
Result<T>
  ok: bool
  value: T
  error: ErrorInfo

ErrorInfo
  code: ErrorCode
  win32Error: DWORD
  message: std::wstring
  path: optional sanitized path
```

Errors are not silently swallowed. UI decides how to present them.

---

## 6. Threading And Cancellation

### 6.1 Threads

| Thread | Responsibility |
|--------|----------------|
| UI thread | message loop, controls, painting, command dispatch |
| Core workers | enumeration, sort, model preparation |
| Shell worker | COM/Shell calls that may block or require STA behavior |
| Benchmark process | repeatable CLI measurement outside UI |

### 6.2 Task Priorities

| Priority | Work |
|----------|------|
| P0 | open folder, first enumeration batch, cancellation |
| P1 | follow-up enumeration batches, sorting requested by user |
| P2 | icon extraction for visible rows |
| P3 | icon extraction for offscreen rows, optional prefetch |

### 6.3 Generation Tokens

Every pane has a monotonic generation id.

Flow:

1. User opens path in pane.
2. Pane increments generation.
3. Existing work for old generation is canceled.
4. New enumeration starts with `(paneId, generation, path)`.
5. Worker posts batches to UI.
6. UI applies a batch only when pane generation still matches.
7. Old results are discarded.

This prevents stale background results from polluting the current folder after rapid navigation.

### 6.4 UI Message Boundary

Background workers never mutate UI controls directly. They post compact messages to the UI thread:

```text
WM_FE_ENUM_BATCH
WM_FE_ENUM_COMPLETE
WM_FE_ENUM_ERROR
WM_FE_SORT_COMPLETE
WM_FE_ICON_BATCH
WM_FE_OPERATION_RESULT
```

Message payloads are owned through safe heap objects or shared ownership handles with clear lifetime rules. UI releases payloads after processing.

---

## 7. Directory Enumeration

### 7.1 API

Initial API choice:

- `FindFirstFileExW`
- `FindNextFileW`
- `FindClose`
- `FindExInfoBasic`
- `FIND_FIRST_EX_LARGE_FETCH` when available and useful

All path handling is wide-character only.

### 7.2 Enumeration Strategy

Rules:

- Enumeration runs off the UI thread.
- Results are sent in batches.
- First batch is optimized for visible rows, not full enumeration completion.
- Batch size starts at 256 entries and can be tuned by benchmark.
- Worker checks cancellation between API calls and before posting each batch.
- Basic attributes from `WIN32_FIND_DATAW` are enough for the first display.
- Shell metadata is not part of enumeration.

Pseudo-flow:

```text
openDirectory(path, paneId, generation):
  normalized = normalizeLocalPath(path)
  handle = FindFirstFileExW(normalized + "\\*")
  while handle valid:
    if canceled: stop
    convert WIN32_FIND_DATAW to FileEntry
    append to current batch
    if batch full or first-visible deadline reached:
      post batch to UI
  post complete or error
```

### 7.3 Path Rules

- Use `std::wstring` for all internal paths.
- Normalize separators at command boundaries.
- Preserve original casing for display.
- Support Unicode names.
- Use long path strategy for paths beyond `MAX_PATH`.
- Do not trim trailing spaces/dots inside valid file names.
- Do not follow reparse points recursively in MVP.

### 7.4 Error Cases

| Case | Expected Behavior |
|------|-------------------|
| missing path | show path not found |
| access denied | show permission error |
| path is file | open default app or show cannot browse |
| path deleted during enumeration | keep already loaded entries and show stale warning or error |
| invalid path syntax | reject before worker dispatch |
| long path failure | show long-path-specific error |

---

## 8. Sorting

### 8.1 Sort Modes

| Sort | Direction | Notes |
|------|-----------|-------|
| Name | asc/desc | default, directories first |
| Type | asc/desc | extension first, directories grouped |
| Size | asc/desc | directories grouped, files by size |
| Modified | asc/desc | directories grouped, newest/oldest |

### 8.2 Strategy

Small datasets may sort immediately if the estimated work stays below the UI stall budget. Large datasets sort on a worker.

Initial thresholds:

- `<= 2,000` rows: allow direct sort only if measured below 20 ms
- `> 2,000` rows: background sort
- `> 50,000` rows: always background sort with visible feedback

The sort worker returns a new `visibleOrder` vector tagged with pane generation and sort request id.

### 8.3 Comparison Rules

- Directories first by default.
- Case-insensitive ordinal comparison for MVP.
- Natural sorting is deferred until benchmarked because shell-like natural compare can add cost.
- Empty extension sorts before non-empty extension.
- Sort must be deterministic for equal primary keys by falling back to name then original index.

---

## 9. Icons And Metadata

### 9.1 Icon Loading

`IconProvider` runs in background and updates visible rows first.

Rules:

- Use placeholder icons immediately.
- Prefer extension-level icon cache before per-file extraction.
- Visible range icon requests are P2.
- Offscreen icon requests are P3.
- Failed icon extraction records an icon error state and keeps placeholder.
- Icon loading must be disableable for benchmark comparison.

### 9.2 Cache

Initial cache keys:

```text
IconCacheKey
  extension
  isDirectory
  attributes mask
```

Per-file icon keys are allowed only for files that require specific icons, and must be bounded by LRU capacity.

### 9.3 Metadata

Included in MVP:

- basic attributes from enumeration
- size for files
- modified time
- hidden/system/read-only flags

Deferred:

- folder recursive size
- thumbnails
- shell type name
- owner/security metadata
- media dimensions

---

## 10. File Operations

### 10.1 Operation Model

Supported MVP operations:

| Operation | API Direction | Notes |
|-----------|---------------|-------|
| Open file | `ShellExecuteExW` | default app |
| Open folder | internal navigation | same pane |
| Rename | Shell operation preferred; Win32 fallback after validation | single item only |
| Create folder | `CreateDirectoryW` | generate conflict-safe default name |
| Delete | `IFileOperation` recycle-bin delete | no permanent delete in MVP |

### 10.2 Shell Worker

Shell operations run through `ShellWorker`, not the UI thread.

Design rules:

- Initialize COM on the shell worker.
- Serialize operations that touch Shell COM APIs.
- Return structured success, partial success, canceled, or failed result.
- UI remains responsive while operations run.
- File list refresh happens after operation result if current generation still matches.

### 10.3 Safety Rules

- No permanent delete in MVP.
- No admin elevation automation.
- No recursive custom delete implementation.
- Confirm destructive-looking actions when recycle-bin behavior cannot be guaranteed.
- Never issue an operation if source/target path validation fails.
- Report partial failures explicitly.

---

## 11. Diagnostics And Performance Tracking

### 11.1 PerfTracker Events

```text
app.launch.start
app.interactive
pane.open.start
pane.first_batch.visible
pane.enumeration.complete
pane.cancel.requested
pane.cancel.observed
sort.requested
sort.complete
icon.batch.requested
icon.batch.applied
ui.stall.detected
```

Events include timestamp, pane id, generation id, path hash or sanitized path policy, item count, and duration when applicable.

### 11.2 Logging

MVP logging is local-only.

Default location:

```text
%LOCALAPPDATA%\FastExplorer\logs\
```

Rules:

- No external telemetry.
- Debug builds may include full local paths.
- Release builds should avoid writing full sensitive paths unless diagnostics mode is enabled.
- Benchmark output can include explicit dataset paths because the user invokes it intentionally.

### 11.3 UI Stall Probe

The app records potential UI stalls by measuring message-loop gaps.

Initial rule:

- warn in debug log when UI thread does not process messages for more than 50 ms
- include active command, focused pane, and current loading state if available

---

## 12. Benchmark Design

### 12.1 CLI

`FastExplorerBench.exe` supports:

```text
FastExplorerBench generate --preset small --out D:\tmp\fe-bench\small
FastExplorerBench generate --preset large-flat --out D:\tmp\fe-bench\large-flat
FastExplorerBench enumerate --path D:\tmp\fe-bench\large-flat --json result.json
FastExplorerBench sort --path D:\tmp\fe-bench\large-flat --by name --json result.json
```

### 12.2 Dataset Presets

| Preset | Size | Purpose |
|--------|------|---------|
| `small` | 200 files | normal folder |
| `medium` | 10,000 files | common heavy folder |
| `large-flat` | 100,000 files | worst-case flat directory |
| `mixed-names` | 50,000 files | Unicode, spaces, long names |
| `mixed-types` | 30,000 files | extension/icon load pressure |
| `many-dirs` | 20,000 folders | directory-first sort |
| `deep-tree` | depth 20+ | path handling |

### 12.3 Metrics

| Metric | Source |
|--------|--------|
| first entry discovered | CLI timing |
| first batch posted | core timing |
| first visible rows | app instrumentation |
| full enumeration | CLI and app |
| sort duration | core timing |
| memory snapshot | process memory query |
| UI stall count | app instrumentation |
| cancellation latency | pane generation event pair |

### 12.4 Result Format

Benchmark results are JSON files under:

```text
bench-results/
  2026-05-14/
    machine-info.json
    enumerate-large-flat.json
    sort-large-flat-name.json
```

The result includes machine info, OS version, build type, power mode note, dataset summary, and measured values.

---

## 13. Testing Plan

### 13.1 Automated Tests

| Area | Test |
|------|------|
| path utils | normalization, long path, invalid path rejection |
| file model | append batches, clear generation, row lookup |
| selection | stable selection after sort |
| sorting | name/type/size/date deterministic order |
| cancellation | stale generation discarded |
| error model | Win32 error conversion |
| benchmark generator | expected file counts and names |

### 13.2 Integration Tests

Use generated folders under `D:\tmp\fast-explorer-test` by default.

Scenarios:

- enumerate empty folder
- enumerate 200 files
- enumerate 10,000 files
- enumerate Unicode names
- enumerate long names
- open missing path
- open access-denied path when available
- rename in sandbox folder
- create folder in sandbox folder
- recycle-bin delete in sandbox folder

### 13.3 Manual UI QA

| Scenario | Pass Criteria |
|----------|---------------|
| launch app | file explorer UI appears immediately |
| open Downloads | rows appear and UI stays interactive |
| open 100k folder | first rows appear before full enumeration |
| switch folder during load | old results never appear in new pane |
| resize during load | no freeze or broken layout |
| sort large folder | command accepted quickly; UI remains responsive |
| dual pane load | panes load independently |
| icon enabled/disabled | file names always render first |
| delete to recycle bin | operation result is explicit |
| missing path | clear error, no crash |

---

## 14. Implementation Order

### 14.1 Milestone 1: Native Scaffold

- CMake project
- Win32 app entry point
- main window
- command bar/address bar placeholder
- empty file pane
- local logging directory

Exit criteria:

- app launches and closes cleanly
- warm launch timing event exists

### 14.2 Milestone 2: Core Enumeration

- path utilities
- `DirectoryEnumerator`
- `FileEntry`
- `FileModelStore`
- first benchmark CLI command

Exit criteria:

- CLI enumerates generated small/medium datasets
- core tests cover path and model basics

### 14.3 Milestone 3: Virtual List UI

- `LVS_OWNERDATA` list control
- batch append from worker to UI
- first visible rows timing
- loading/partial/ready states

Exit criteria:

- UI opens local folder
- 10k folder remains interactive during loading

### 14.4 Milestone 4: Navigation And Cancellation

- address bar navigation
- enter folder
- up/back/forward/refresh
- generation token cancellation

Exit criteria:

- rapid folder switching does not apply stale results
- cancellation latency is measured

### 14.5 Milestone 5: Sorting And Selection

- name/type/size/modified sort
- visible order model
- stable selection
- keyboard and mouse basics

Exit criteria:

- large sort does not block UI
- selected rows remain coherent after sort

### 14.6 Milestone 6: Icons And Basic Operations

- placeholder icons
- background icon cache
- open file
- rename
- create folder
- delete to recycle bin

Exit criteria:

- icon loading never delays file names
- file operations return structured results

### 14.7 Milestone 7: Benchmark And Stabilization

- full dataset generator presets
- memory snapshot
- UI stall probe
- benchmark result JSON
- 1-hour soak test checklist

Exit criteria:

- design performance gates can be measured
- Check phase gap analysis can compare implementation to this document

---

## 15. Security And Privacy

- The app does not send telemetry.
- Logs are local-only.
- Release logging avoids full personal paths unless diagnostics mode is enabled.
- Delete uses recycle bin by default.
- Permanent delete is out of scope.
- Shell extensions are treated as untrusted latency and stability risks.
- UI must not freeze while waiting for Shell operations.
- File operations must distinguish success, partial success, canceled, and failed.
- The app must validate command paths before executing file operations.

---

## 16. Design Traceability

| Plan Goal | Design Coverage |
|-----------|-----------------|
| Native Windows C++ MVP | Sections 2, 3, 14 |
| First visible rows priority | Sections 2.3, 7, 11, 12 |
| UI thread never blocked | Sections 3, 6, 7, 8, 10 |
| Virtualized rows | Section 4.4 |
| Async icons/metadata | Section 9 |
| Benchmark harness | Section 12 |
| Basic navigation | Sections 4.5, 14.4 |
| Multi-pane UX | Sections 4.2, 14.4 |
| Error model | Sections 5.3, 7.4, 10 |
| Safety-first operations | Sections 10, 15 |

---

## 17. Deferred Decisions

These are intentionally not part of MVP implementation:

- Direct2D/DirectWrite custom file list
- quad layout as a release gate
- Shell context menu
- drag-and-drop
- thumbnails
- folder recursive size
- plugin system
- archive browsing
- network drive optimization
- Windows Explorer replacement registration
- app updater, signing, installer

These items should only be reconsidered after benchmark and responsiveness gates are passing.

---

## 18. Design Completion Checklist

- [x] Architecture defined
- [x] UI structure defined
- [x] Threading model defined
- [x] Cancellation model defined
- [x] Directory enumeration design defined
- [x] Virtual list design defined
- [x] Data model defined
- [x] File operation design defined
- [x] Error model defined
- [x] Benchmark design defined
- [x] Test plan defined
- [x] Implementation order defined

