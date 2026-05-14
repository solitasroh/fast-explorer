# fast-explorer-core - Design Document

> **Summary**: Windows native file explorer MVP architecture for instant local-folder responsiveness, cancellable background work, virtualized rendering, and repeatable performance measurement.
>
> **Author**: Codex
> **Created**: 2026-05-14
> **Status**: Review
> **Version**: 1.0.3
> **Level**: Starter

---

## Version History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 1.0.0 | 2026-05-14 | Initial technical design document | Codex |
| 1.0.1 | 2026-05-14 | Teammate review 결과 반영: COM apartment 명시, cancellation 3계층, IFileOperation 운영 디테일, long path/reparse/UNC/cloud placeholder 정책, manifest/CRT/MSVC toolset, FileEntry 메모리 제약, ReadDirectoryChangesW MVP 포함, LVN_GETDISPINFO 예산, crash dump + 로깅 backend, ETW/QPC 측정 결정, milestone 성능 게이트 분산, deferred decisions 확장 | Claude |
| 1.0.2 | 2026-05-14 | 메모리 최적화 전략 전면 반영: FileEntry 40 B 압축, name arena `VirtualAlloc` chunk, ImageList process-global 공유, Format LRU bounded, CRT/컴파일 옵션 (`/GR-` 검토, `/GL+LTCG`, `/Gw/Gy`), Working Set 핸들러 (`SetProcessWorkingSetSizeEx`, `EmptyWorkingSet`, low-memory notification → caches drop), generation 교체 시 즉시 회수, 메모리 enforcement (static_assert + bench gate), 예상 process 총 메모리 ~50 MB target (100 MB budget 대비 2× 마진) | Claude |
| 1.0.3 | 2026-05-14 | M1 review fix 반영: §5.3.1 PerfTracker ring size 표기 정정 (640 KB → 320 KB, 24 B/slot 명시), §11.2 backend SPSC → MPSC + per-slot publication seq + overflow drop counter 명시. 구현 측 변경: RingLogger shutdown drain 순서 (stopEvent 먼저 → drain → join → flags), atomic ordering release on inProgress store, overflow guard + drop counter, WriteFile short-write 처리, crash handler RingLogger 의존 제거 (signal-safe path), MiniDump user-stream에 PerfTracker ring 첨부, --crash-test 토큰 정확 매칭 + real unhandled exception 경로 (=throw), low-memory state-based notification handling (busy loop 회피), EmptyWorkingSet 1 Hz throttle + SetPriorityClass BACKGROUND 짝, path-utils 추출 (DRY) | Claude |

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
| Target OS | Windows 11 x64 first, Windows 10 1809+ best-effort | MVP 성능 검증을 Windows 11 기준으로 고정 |
| Compiler | MSVC v143 (Visual Studio 2022 17.6+) | C++20 modules/coroutines 지원, 안정성 |
| Windows SDK | 10.0.22621.0 이상 (Windows 11 SDK) | DPI v2 API, common controls v6, 최신 Shell API |
| CRT linkage | `/MD` (shared CRT) + VC++ Redistributable 동봉 | 바이너리 크기/패치 가능성. portable zip 배포 시 `/MT` 빌드 별도 production. |
| Build | CMake 3.24+ + Ninja or MSVC generator | app, benchmark, tests가 core library를 공유 |
| UI framework | Win32 + common controls v6 | 낮은 런타임 비용과 message loop 직접 제어 |
| Rendering | `LVS_OWNERDATA` List-View first | 100k+ row 처리 검증을 가장 빠르게 시작 |
| Custom render | Deferred | List-View 한계가 측정될 때 Direct2D/DirectWrite로 이동 |
| External dependencies | Avoid by default | 성능/빌드 복잡도 리스크를 낮춤 |
| Code signing | Unsigned MVP (SmartScreen 경고 허용) | 상용 배포 단계가 아님. signing은 Phase 9 deployment에서 결정. |

### 2.1.0 Compiler / Linker Options (Memory + Size Optimization)

Release build:

| Option | Decision | Reason |
|--------|----------|--------|
| `/O2` | enabled | 속도 우선. PGO는 M7 이후 검토. |
| `/Gw` | enabled | global data COMDAT → linker dead-strip |
| `/Gy` | enabled | function-level linking → dead-strip |
| `/GL` (whole-program opt) | enabled | inlining, cross-TU dead-code elim |
| `/LTCG` (link-time codegen) | enabled | `/GL` 짝. final binary 축소 |
| `/GR-` (RTTI off) | **검토** (M2 결정) | COM/Win32는 IID 기반, `dynamic_cast` 미사용 시 가능. exe ~1–2 KB + per-vtable RTTI 제거 |
| `/EHsc` (C++ exceptions) | enabled | std lib 일부 사용. SEH는 thread 경계만. |
| `/permissive-` | enabled | strict ISO 준수 |
| `/Zc:__cplusplus` | enabled | `__cplusplus` 매크로 정확성 |
| `/utf-8` | enabled | source + execution charset UTF-8 |
| `/W4 /WX` | enabled | warnings as errors (현재 M1은 `/W4`만, `/WX`는 M2에서 추가) |
| `/sdl` | enabled | additional security checks |
| `/guard:cf` | enabled | Control Flow Guard |
| `/Qspectre` | enabled | Spectre mitigation (size 부담 작음) |
| `/DEBUG:FULL` | Release에도 enabled (별 PDB) | crash dump 분석 위한 PDB 보관 (배포본에는 미포함) |
| `/OPT:REF /OPT:ICF` | enabled | linker dead code + identical-COMDAT folding |
| iostream | **excluded** | 60+ KB CRT bloat 회피 |
| `std::regex` | **excluded** | 큰 정적 코드 |
| `std::filesystem` | **excluded** | heap intensive, Win32 직접 호출이 더 빠르고 가벼움 |

### 2.1.1 Application Manifest (필수)

`FastExplorer.exe.manifest` 항목 (모두 MVP 포함):

| Manifest Item | Value | Reason |
|---------------|-------|--------|
| `requestedExecutionLevel` | `asInvoker` (uiAccess=false) | 권한 상승 자동화 금지 |
| `Microsoft.Windows.Common-Controls` dependency | version `6.0.0.0` | themed List-View. 누락 시 Win95 UI 폴백 |
| `longPathAware` | `true` | `\\?\` 없이도 long path 수용. Win10 1607+ |
| `dpiAwareness` | `PerMonitorV2` | per-monitor DPI v2 활성화. WM_DPICHANGED 처리 |
| `gdiScaling` | `true` | per-monitor scaling 시 GDI 자동 보정 |
| `activeCodePage` | `UTF-8` | (Win10 1903+) console/CRT path 호환 |
| `supportedOS` | Win10 + Win11 GUID | OS 호환성 advertisement |

설치 없이 실행되는 portable zip은 manifest를 exe에 임베드한다.

### 2.1.2 Settings Storage Lock

- 기본 경로: `%LOCALAPPDATA%\FastExplorer\settings.json`
- Portable override: 환경변수 `FAST_EXPLORER_PORTABLE_ROOT` 가 설정되면 그 디렉터리 하위 `settings.json` 사용. exe와 동일 디렉터리에 `portable.marker` 파일이 있으면 자동 portable 모드. (Plan §16.1 → portable 모드를 향후 막지 않기 위한 설계)
- 로그 경로: `%LOCALAPPDATA%\FastExplorer\logs\` 또는 portable mode 시 `<portable_root>\logs\`

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

| Budget | Target | Measurement Point |
|--------|--------|-------------------|
| Warm launch to interactive | <= 500 ms | `app.launch.start` → `app.interactive` |
| Cold launch to interactive | <= 1,500 ms | 동일, OS 캐시 무효화 후 |
| Small folder first visible rows | <= 50 ms | `pane.open.start` → `pane.first_batch.visible` |
| Medium folder first visible rows | <= 100 ms | 동일 |
| Large folder first visible rows | <= 200 ms | 동일 |
| UI thread single stall | <= 50 ms | `ui.stall.detected` (message loop gap) |
| **Scroll frame p95** | **<= 16.7 ms (60 Hz)** | `ui.scroll.frame` 샘플의 p95 |
| **LVN_GETDISPINFO callback budget** | **<= 50 µs / row** | per-callback QPC 샘플 |
| Folder switch cancellation | <= 50 ms | `pane.cancel.requested` → `pane.cancel.observed` |
| 100k base entries incremental memory (budget) | <= 100 MB excluding icons/thumbnails | working set delta |
| **100k base entries incremental memory (target)** | **<= 50 MB total process working set** | aspirational, 2× margin (§5.4 분석 기반) |
| **FileEntry sizeof bound** | **== 40 B / entry** (was: <= 128, then <= 64) | static_assert로 강제 (v1.0.2 추가 압축) |
| **Per-pane FileModelStore total** | **<= 10 MB @ 100k entries** | entries 4 MB + name arena ~4.8 MB + visibleOrder 0.4 MB |
| **ImageList process-global cap** | **<= 3 MB** | 500 ext + 200 per-file × 32×32 BGRA |
| **Working set after generation drop** | **drop within 200 ms** | `VirtualFree` + `EmptyWorkingSet` |

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

| Column | Source | Display Rule |
|--------|--------|--------------|
| Name | `FileEntry.name` | always first priority |
| Type | extension (캐시) or `<DIR>` marker | shell type name excluded in MVP |
| Size | `FileEntry.size` | folder는 빈 칸. `StrFormatByteSizeW` 또는 자체 포매터, 결과는 row cache에 저장 |
| Modified | `FileEntry.modifiedTime` | `GetDateFormatEx` + `GetTimeFormatEx` (Locale=user). 결과는 LRU cache. UI thread에서 visible rows만 포맷 |
| Attributes | cached flags | `H` (hidden), `S` (system), `R` (read-only), `J` (junction/reparse), `L` (symlink), `C` (cloud placeholder) 문자 마커 |

### 4.4.1 LVS_OWNERDATA Callback Budget

`LVN_GETDISPINFO`는 UI thread 동기 호출이다. 100k row scroll 시 초당 수천 회 호출될 수 있다.

| Callback | Budget | Allowed Work |
|----------|--------|--------------|
| `LVN_GETDISPINFO` | **<= 50 µs / row** | `FileModelStore::getVisibleRow(index)` lookup + 사전 포맷된 문자열 포인터 반환 만 허용 |
| `LVN_ODCACHEHINT` | **prefetch trigger** | visible window prefetch. icon/format 작업은 IconProvider/FormatService에 enqueue. 동기 작업 금지 |
| `LVN_ODFINDITEM` | <= 100 µs | linear scan 금지. visibleOrder index lookup만 |
| `LVN_ODSTATECHANGED` | <= 100 µs | 범위 selection 변경 단일 알림. selectionState bitmap 업데이트만 |
| `NM_CUSTOMDRAW` | <= 200 µs / item | hidden/system dimming, junction overlay 마커 표시 |

### 4.4.2 Rules

- Row count과 row data는 분리한다. `ListView_SetItemCountEx(..., LVSICF_NOINVALIDATEALL)` 사용.
- List는 visible row text만 요청한다.
- Visible row 포매팅은 cheap이며 가능 시 사전 캐시.
- Icon cell은 background 결과 도착 전까지 placeholder 아이콘.
- Selection은 raw visible index가 아니라 stable model id로 추적한다.
- Hidden/system 파일은 `NM_CUSTOMDRAW`에서 dim 색상으로 표시 (`COLOR_GRAYTEXT`).
- Junction/symlink는 `NM_CUSTOMDRAW`에서 화살표 오버레이 또는 attribute 컬럼 문자만 (오버레이 이미지 deferred).
- `LVS_EX_DOUBLEBUFFER + LVS_EX_FULLROWSELECT + LVS_EX_HEADERDRAGDROP` 스타일 설정.

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
| Single layout | `Ctrl+1` | layout 모드 전환 |
| Dual layout | `Ctrl+2` | layout 모드 전환 |
| Toggle hidden files | `Ctrl+H` | show/hide hidden+system |
| Cycle pane focus | `Tab` / `Shift+Tab` | pane 간 포커스 이동 (dual layout) |

---

## 5. Core Data Model

### 5.1 FileEntry

`FileEntry` stores only the data needed for display, sorting, and safe operations. It does not duplicate full paths for every row.

```cpp
struct FileEntry {
  const wchar_t* namePtr;    // 8 B — points into FileModelStore::nameArena
  uint64_t size;             // 8 B — 0 for directories
  FILETIME modifiedTime;     // 8 B
  uint32_t attributes;       // 4 B — raw FILE_ATTRIBUTE_* mask
  uint16_t nameLength;       // 2 B — wide-char count
  uint16_t extensionOffset;  // 2 B — offset into name (UINT16_MAX if none)
  uint8_t  flags;            // 1 B — bit0=isDir, bit1=isHidden, bit2=isSystem,
                             //        bit3=isReparse, bit4=isCloudPlaceholder
  uint8_t  states;           // 1 B — icon nibble (low 4) + metadata nibble (high 4)
  uint8_t  errorCode;        // 1 B — ErrorCode enum (0 = no error)
  uint8_t  reserved;         // 1 B — padding / future
};
static_assert(sizeof(FileEntry) == 40, "FileEntry must be exactly 40 B for memory budget");
static_assert(alignof(FileEntry) == 8);
// 100k entries × 40 B = 4 MB structural + name arena (~4.8 MB) + visibleOrder (400 KB) = ~9.5 MB per pane.
```

Removed vs v1.0.1 (-24 B):

| Field | Reason for removal |
|-------|--------------------|
| `id` (uint32) | entries vector index 가 id 역할. 별도 저장 불필요. |
| `generation` (uint32) | FileModelStore가 단일 generation 보유, entry당 중복 불필요. 결과 폐기는 store-level 체크. |
| `createdTime` (FILETIME 8 B) | UI 컬럼에서 표시 안 함. 필요 시 cold side-arena에서 `id` 기반 lookup. |
| `iconState` + `metadataState` separate bytes | 4 bit + 4 bit 결합 → 1 B 절약. enum max 16 상태 충분. |

Key design rules:

- **No `std::wstring` per entry**. Names are interned into a per-pane arena (§5.2.1). `namePtr` + `nameLength` define an implicit `wstring_view`.
- **Extension is offset+length within name**, not separate string. `extensionView()` returns `wstring_view(namePtr + extensionOffset, nameLength - extensionOffset)`.
- **Bit-packed flags** instead of `bool` fields.
- **Icon image index 미저장**. ImageList lookup은 extension hash 기반 (§9.2 참고). entry당 0 B 추가 비용.
- **No `EntryId` typedef** in MVP (단순화, allocator 부담 회피).
- **POD-like**: no virtual fns, no smart pointers, trivially copyable for memmove batch ops.

Full path construction:

```cpp
std::wstring buildFullPath(const FileEntry& e, const std::wstring& root);
// = root + L"\\" + wstring_view(e.namePtr, e.nameLength)
// 내부 경로는 항상 \\?\ prefix 정규화, 표시 경로는 prefix 제거 (§7.3 참고)
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

### 5.2.1 Name Arena

Per-pane contiguous wide-char buffer used to intern entry names.

| Item | Decision |
|------|----------|
| Backing | `VirtualAlloc(MEM_RESERVE)` reserve 16 MB, `MEM_COMMIT` per 64 KB chunk as needed |
| Growth | append-only. names stored back-to-back, no separator (length tracked) |
| Decommit | generation reset 시 모든 committed chunks `VirtualFree(MEM_DECOMMIT)` 즉시 |
| Cap | hard cap 64 MB reserve (overflow 시 enumeration error, pane은 partial result로 표시) |
| Stability | committed memory의 `namePtr`는 store lifetime 동안 invalid 안 됨 (`VirtualAlloc` 영역 이동 안 함) |
| SSO 미적용 | MVP 단순화. 평균 wide name 24 char → 48 B/name. 100k * 48 B = ~4.8 MB |

```cpp
class NameArena {
public:
  NameArena();
  ~NameArena();  // VirtualFree(MEM_RELEASE) entire reservation
  std::wstring_view intern(std::wstring_view name);  // commits page if needed
  void reset();  // decommit all, keep reservation
  size_t committedBytes() const;
private:
  wchar_t* base_;       // VirtualAlloc(reserved, 16 MB)
  size_t committed_;    // bytes committed
  size_t used_;         // bytes used
};
```

### 5.2.2 Memory Layout Summary (per pane, 100k entries)

| Component | Size |
|-----------|------|
| `entries: vector<FileEntry>` (capacity == count) | 4.0 MB |
| `nameArena` (committed) | ~4.8 MB |
| `visibleOrder: vector<uint32_t>` | 0.4 MB |
| `selectionState` (bitset 100k bits) | 12 KB |
| Format LRU (size/date strings, 1k cap) | ~128 KB |
| **Per-pane total** | **~9.5 MB** |

### 5.3 Memory Optimization Strategy

이 절은 100 MB budget 대비 **2× 마진 (~50 MB target)** 을 위한 통합 전략이다.

#### 5.3.1 Process-Global Shared State

| State | Scope | Why shared |
|-------|:-----:|------------|
| `IconImageList` (`HIMAGELIST`) | process | 모든 pane이 동일 extension에서 동일 icon 재사용 |
| `FormatService` LRU (size string, date string) | process | locale 변경 시 invalidate. 모든 pane 공유 |
| `RingLogger` | process | 1개 ring + async writer thread |
| `PerfTracker` ring (~10k events × 32 B/slot ≈ 320 KB) | process | 1개. slot = 8 B seq + 24 B Event. M2부터 paneId/generation 추가 시 재계산. |
| `IconExtensionCache` (ext → image idx) | process | 500 ext + 200 per-file LRU |

#### 5.3.2 Per-Pane Lifetime

| State | Lifetime |
|-------|----------|
| `FileModelStore` (entries + nameArena + visibleOrder + selectionState) | pane open ~ pane close 또는 generation reset |
| Active enumeration `stop_source`, `std::vector<FileEntry> batchBuilder` | enumeration 1회 (소멸 시 release) |
| In-flight Shell op payloads | op 완료 또는 cancel |

**Generation 전환 시 즉시 회수**: `FileModelStore::resetForNewPath()` 가 호출되면:
1. `stop_source.request_stop()`
2. `nameArena.reset()` → `VirtualFree(MEM_DECOMMIT)` 모든 committed page
3. `entries.clear()` + `shrink_to_fit()` (대용량 vector capacity 회수)
4. `visibleOrder.clear()` + `shrink_to_fit()`
5. `selectionState.reset()`
6. `EmptyWorkingSet(GetCurrentProcess())` 호출 (선택, throttled to 1/sec)

100k → 0 회수는 보통 100 ms 이내 (commit 해제는 lazy하지만 working set 즉시 감소).

#### 5.3.3 Heap / Allocator Rules

- **STL 사용 제한**:
  - `std::filesystem` ❌ (heap intensive, Win32 직접 호출이 가볍고 빠름)
  - `std::regex` ❌ (대형 정적 코드)
  - `std::iostream` ❌ (CRT bloat 60+ KB)
  - `std::wstring` minimal — `std::wstring_view` 우선 사용
- **Reserve 정책**: `entries.reserve(prev_entry_count or 4096)` enumeration 시작 시. realloc 회피.
- **Small vector**: batch payload (256 entries inline) — heap alloc 회피. `core/small-vector.h` 자체 구현.
- **Pool allocator**: M5 sort worker가 임시 `vector<uint32_t> tmpOrder` 매번 할당하지 않도록 pane-local pool.

#### 5.3.4 OS Working-Set Tuning

| Mechanism | Use | Timing |
|-----------|-----|--------|
| `SetProcessWorkingSetSizeEx(min=8MB, max=128MB, QUOTA_LIMITS_HARDWS_MIN_DISABLE \| QUOTA_LIMITS_HARDWS_MAX_DISABLE)` | hint only | startup 시 1회 |
| `EmptyWorkingSet(GetCurrentProcess())` | physical 강제 회수 | window minimize 또는 generation drop, throttled 1/sec |
| `CreateMemoryResourceNotification(LowMemoryResourceNotification)` | 시스템 low memory 이벤트 등록 | startup |
| `QueryMemoryResourceNotification` | low memory 시 → ImageList shrink + Format LRU clear + Icon per-file cache evict | periodic 1s tick + event |
| `SetPriorityClass(PROCESS_MODE_BACKGROUND_BEGIN)` | minimize 후 background priority | WM_SIZE SIZE_MINIMIZED |
| `SetPriorityClass(PROCESS_MODE_BACKGROUND_END)` | restore | WM_SIZE SIZE_RESTORED |

#### 5.3.5 ImageList Strategy

| Item | Decision |
|------|----------|
| Storage | `ImageList_Create(32, 32, ILC_COLOR32 \| ILC_MASK, 64, 32)` 초기 64 capacity, 32씩 증가 |
| Cache key | extension wide-char hash (case-insensitive ordinal) |
| Per-file exception | `.exe`, `.lnk`, `.url`, `desktop.ini` — 별도 LRU, cap 200 |
| Entry lookup | LVN_GETDISPINFO → `IconExtensionCache::lookup(entry.extensionView())` → image index 또는 placeholder (-1) |
| HiDPI | deferred. 32×32 only. (per-monitor scaling 시 stretched. M7 이후 upgrade 검토) |
| Eviction | LRU. 시스템 low memory 시 `ImageList_Remove(-1)` + cache clear |

Entry에 image index 저장 안 함 → entry당 **0 B** 추가 비용.

#### 5.3.6 Format LRU

```cpp
class FormatService {
  // size: uint64_t → wstring (e.g., "1.23 MB")
  // date: FILETIME → wstring (locale formatted)
  // Both bounded LRU, cap=1000 each.
  std::wstring_view formatSize(uint64_t bytes);
  std::wstring_view formatDate(FILETIME ft);
  void onLocaleChange();  // clear all
};
```

LVN_GETDISPINFO에서 cached `wstring_view` 직접 반환. 50 µs 예산 안전.

#### 5.3.7 Enforcement / Measurement

| Check | Method |
|-------|--------|
| `static_assert(sizeof(FileEntry) == 40)` | compile-time |
| `static_assert(alignof(FileEntry) == 8)` | compile-time |
| `FileModelStore::estimatedBytes()` | runtime, diag bar (debug mode) |
| `GetProcessMemoryInfo(pmc.WorkingSetSize)` poll | PerfTracker event `process.workingset.delta` |
| Bench gate (M7) | `process.peak_workingset` @ 100k pane ≤ 50 MB target, ≤ 100 MB budget |
| Generation drop test | 100k → empty → 100k → empty cycle 10회, working set 누적 증가 ≤ 5 MB |

### 5.4 Result Model

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

### 6.1 Threads And COM Apartments

| Thread | COM Apartment | Responsibility | COM API 허용 여부 |
|--------|:-------------:|----------------|:------:|
| UI thread | **STA** (`COINIT_APARTMENTTHREADED`) | message loop, controls, painting, command dispatch | 최소한만 (`OleInitialize` 호출 후, 미래 DnD 위해) |
| Shell worker (1개) | **STA** (자체 `PeekMessage` 루프) | `IFileOperation`, `SHGetFileInfoW`, `IShellItem*`, `ShellExecuteExW` 등 모든 Shell COM | **필수** |
| Icon worker pool (N개) | **STA each** (`COINIT_APARTMENTTHREADED` per thread) | `SHGetFileInfoW` 또는 `IShellItemImageFactory::GetImage` 호출 | 필수 |
| Core worker pool (M개) | **MTA** (`COINIT_MULTITHREADED`) | enumeration, sort, model preparation, hashing | **금지** (Shell COM 호출 안 함, Win32 API만) |
| Watcher thread | MTA | `ReadDirectoryChangesW` IOCP loop | 금지 |
| Benchmark process | STA in main only | repeatable CLI measurement | benchmark 항목에 따라 |

Worker pool 크기:
- Icon workers: `min(4, hardware_concurrency / 2)`
- Core workers: `max(2, hardware_concurrency - 2)`

UI thread는 `OleInitialize` 사용 (`CoInitializeEx`보다 상위, DnD 가능). Shell worker는 `CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)`.

**Rationale**: Shell extension proxy stub은 호출 thread가 STA가 아니면 OLE marshaler를 강제 삽입하여 reentrant deadlock을 유발한다. Core worker가 MTA인 이유는 Shell COM을 호출하지 않으므로 STA 메시지 펌프 비용을 피하기 위해서이다.

### 6.2 Task Priorities

| Priority | Work | Queue |
|----------|------|-------|
| P0 | open folder, first enumeration batch, cancellation propagation | core pool front |
| P1 | follow-up enumeration batches, sorting requested by user, FS watch events | core pool |
| P2 | icon extraction for visible rows | icon pool |
| P3 | icon extraction for offscreen prefetch, optional metadata extraction | icon pool low |

TaskScheduler 정책:
- Per-priority FIFO 4개. P0/P1은 core pool, P2/P3는 icon pool.
- **No aging / starvation 회피**: P0/P1은 enumeration 1개 폴더당 유한하므로 starvation 위험 낮음. 단 sort 작업은 측정된 시간 초과 시 P1로 demote.
- 같은 (paneId, generation) 의 P3 작업은 새 generation 도착 시 즉시 drop.

### 6.3 Cancellation Layers

generation token만으로는 stale result 폐기는 가능하지만 진정한 interrupt는 아니다. 3계층 cancellation 모델로 명시한다.

| Layer | Mechanism | Latency | Note |
|-------|-----------|:-------:|------|
| **L1 — UI ignore** | UI가 incoming message의 `(paneId, generation)`을 현재 pane state와 비교, 불일치면 payload 폐기 | **<= 50 ms** | 사용자 체감 cancel 게이트의 1차 책임. Worker가 계속 돌아도 UI는 영향 받지 않음. |
| **L2 — Worker abort** | 각 pane/generation에 `std::stop_source`. Worker는 `FindNextFileW` loop의 매 batch boundary와 매 1024 entries마다 `stop_requested` 확인 → 즉시 return | 평균 best-effort, worst case ≤ 1 batch (~5 ms) | CPU/메모리 낭비 방지. `FindNextFileW` 자체는 interruptible 아님. |
| **L3 — Shell op abort** | `IFileOperationProgressSink::PreXxx`에서 `S_FALSE` 반환 | best-effort | IFileOperation 진행 중 사용자 cancel 신호. |

**SHGetFileInfo cancel 불가** → fire-and-forget + 결과 폐기 패턴 사용. Icon worker는 stop_token 확인 후 호출. 호출 중 cancel 도착 시 결과 도착하면 generation mismatch로 폐기.

#### 6.3.1 Generation Token Flow

1. 사용자가 pane에서 path를 연다.
2. Pane이 generation을 증가시키고 이전 `stop_source.request_stop()` 호출.
3. 새 `stop_source` 발급, 새 enumeration 시작 `(paneId, generation, path, stop_token)`.
4. Worker는 매 batch boundary에서 `stop_requested` 확인 → return.
5. Worker가 UI에 batch 메시지 post. 메시지에는 `(paneId, generation)` 포함.
6. UI는 메시지 수신 시 `pane.generation == msg.generation` 확인. 일치하면 적용, 아니면 폐기.
7. 모든 in-flight Shell call은 generation mismatch로 결과 폐기.

### 6.4 UI Message Boundary

Background workers never mutate UI controls directly. They post compact messages to the UI thread.

```cpp
// All Fast Explorer custom messages use WM_APP + offset to avoid system conflicts.
constexpr UINT WM_FE_BASE             = WM_APP + 0x100;
constexpr UINT WM_FE_ENUM_BATCH       = WM_FE_BASE + 0x01;
constexpr UINT WM_FE_ENUM_COMPLETE    = WM_FE_BASE + 0x02;
constexpr UINT WM_FE_ENUM_ERROR       = WM_FE_BASE + 0x03;
constexpr UINT WM_FE_SORT_COMPLETE    = WM_FE_BASE + 0x04;
constexpr UINT WM_FE_ICON_BATCH       = WM_FE_BASE + 0x05;
constexpr UINT WM_FE_OPERATION_RESULT = WM_FE_BASE + 0x06;
constexpr UINT WM_FE_FS_CHANGE        = WM_FE_BASE + 0x07;  // ReadDirectoryChangesW
constexpr UINT WM_FE_PERF_EVENT       = WM_FE_BASE + 0x08;
```

Message payload ownership:
- `PostMessage` lParam은 `unique_ptr<Payload>::release()`로 heap 소유권을 이전한다.
- UI handler는 payload를 `unique_ptr` 으로 재흡수하여 처리 후 자동 release.
- 메시지 큐 적체 방지: 동일 (paneId, type) 메시지는 최신 것만 우선 처리, 이전은 coalesce (batch는 예외).

### 6.5 Filesystem Change Watch (MVP 포함)

`ReadDirectoryChangesW` 기반 변경 감지를 MVP에 포함한다. 없으면 rename/create 후 수동 refresh 강제 → native explorer parity를 깨뜨림.

| Item | Decision |
|------|----------|
| API | `ReadDirectoryChangesW` + IOCP completion port (1개 watcher thread, MTA) |
| Watch flags | `FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES` |
| Buffer size | 64 KB per pane (overflow 시 generation 증가 + 전체 refresh) |
| Recursive | **No** (MVP는 현재 pane 폴더만. sub-tree watch는 deferred) |
| Per pane | 별도 watch handle. pane 닫힐 때 `CancelIoEx` + `CloseHandle` |
| Coalescing | UI thread에서 100 ms 내 같은 이벤트는 묶어 처리 |
| Network drive | watch 시도하지 않음 (UNC 거부 정책 §7.3에 따름) |
| OneDrive 폴더 | hydration 회피를 위해 watch 켜되 SIZE 변경 시 placeholder 비트 재확인만 |

---

## 7. Directory Enumeration

### 7.1 API

Initial API choice:

- `FindFirstFileExW(path, FindExInfoBasic, ..., FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH)`
- `FindNextFileW`
- `FindClose`

**`FindExInfoBasic` 효과 (명시)**: `cAlternateFileName` (8.3 short name)을 채우지 않는다. NTFS 8dot3 lookup을 건너뛰어 enumeration이 평균 20~40 % 빨라진다. 8.3 name은 사용자에게 노출하지 않으므로 안전.

**`FIND_FIRST_EX_LARGE_FETCH`**: Windows 7+. 시스템이 더 큰 internal buffer를 사용하도록 hint. 10k+ flat directory에서 측정 효과 큼.

**Plan B (M2 측정 결과에 따라)**: `GetFileInformationByHandleEx(handle, FileIdBothDirectoryInfo, ...)` — 단일 syscall로 수천 entries 일괄 획득. NTFS large-flat 200 ms 게이트가 FindFirstFileExW로 불충분하면 전환. M2 exit criteria에 head-to-head 측정 포함.

**재시도 정책**:
- `ERROR_SHARING_VIOLATION` (32): 1회 100 ms 대기 후 재시도
- `ERROR_DIRECTORY_NOT_SUPPORTED`, `ERROR_NOT_READY` (드라이브 미준비): 즉시 error result 반환
- `ERROR_ACCESS_DENIED` (5): 부분 enumeration이 가능하면 partial result + warning, 아니면 error
- `ERROR_PATH_NOT_FOUND` / `ERROR_FILE_NOT_FOUND`: 명확한 path not found error

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

#### 7.3.1 Internal vs Display Path

- 내부 경로는 항상 **`\\?\` prefix 정규화**한 `std::wstring`. 길이는 ~32,767 wide chars까지 허용.
- 표시 경로(AddressBar, breadcrumb, tooltip)는 `\\?\` prefix 제거 + 사용자 원본 casing 유지.
- `path_utils::toInternal(displayPath)` 와 `path_utils::toDisplay(internalPath)` 두 함수가 경계.

#### 7.3.2 Long Path

- app manifest `longPathAware=true` 옵트인 (§2.1.1 참조).
- `\\?\` prefix가 붙은 경로는 path normalization이 **비활성화**됨. `.`, `..`, 상대경로 사용 불가. internal layer는 normalized 형태만 다룸.
- `MAX_PATH` (260) 초과는 정상 케이스로 취급, error 아님.

#### 7.3.3 UNC / Network Drive

- **MVP는 로컬 드라이브 letter만 허용**. UNC 입력(`\\server\share`)은 명시적 거부:
  - AddressBar 입력 시 "UNC paths are not supported in MVP." 안내 + path 영역 빨강 강조
  - settings에 저장된 last path가 UNC면 default drive root로 폴백
- Mapped network drive (예: `Z:` for `\\server\share`)는 로컬 drive letter로 보이므로 허용되지만 성능 게이트에서 제외.

#### 7.3.4 Reparse Point / Junction / Symlink

- enumeration 시 `dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT` 검사하여 `flags.isReparse = true`.
- `FindFirstFileExW`의 `dwReserved0` 에서 `IO_REPARSE_TAG_*` 추출하여:
  - `IO_REPARSE_TAG_SYMLINK` → attribute 컬럼 `L` 마커
  - `IO_REPARSE_TAG_MOUNT_POINT` (junction) → `J` 마커
  - 기타 (AppExecLink 등) → `R` 마커
- **Recursive follow 금지**. 사용자가 명시적으로 enter 했을 때만 target 폴더로 navigate.
- Junction 순환 차단: navigation history에 동일 normalized target이 반복 등장하면 차단 + warning.
- 오버레이 아이콘은 deferred. attribute 컬럼 문자 마커만 MVP.

#### 7.3.5 Cloud Placeholder (OneDrive, Google Drive 등)

- enumeration 시 다음 비트 검사하여 `flags.isCloudPlaceholder = true`:
  - `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS` (0x400000)
  - `FILE_ATTRIBUTE_RECALL_ON_OPEN` (0x40000)
  - `FILE_ATTRIBUTE_OFFLINE` (0x1000) — legacy HSM
- attribute 컬럼에 `C` 마커.
- **Hydration trigger 회피 규칙**:
  - Icon 추출 시 `SHGFI_USEFILEATTRIBUTES` flag 사용 → 실파일 접근 안 함. 일반 file type icon만 사용.
  - 파일 size는 `WIN32_FIND_DATAW`의 값 그대로 사용 (이미 placeholder size). 별도 size query 호출 금지.
  - Thumbnail 추출은 MVP에서 자체적으로 안 함 (제외 항목).
  - 사용자가 명시적으로 파일을 open할 때만 hydration이 발생 (`ShellExecuteExW`).
- **Rationale**: Documents/Downloads 폴더가 OneDrive 미러일 때 hydration trigger를 일으키면 200 ms 게이트가 즉시 깨진다.

#### 7.3.6 Encoding And Comparison

- 정렬/비교: `CompareStringOrdinal(s1, len1, s2, len2, TRUE)` (case-insensitive ordinal). MVP에서 locale-aware natural sort는 deferred.
- 표시 포맷: 날짜/숫자는 `GetUserDefaultLocaleName` 기반 `GetDateFormatEx` / `GetNumberFormatEx`.
- file path는 wide-char ordinal로만 비교. surrogate pair 안전.

#### 7.3.7 Other Rules

- separator는 항상 backslash `\`. forward slash 입력은 boundary에서 변환.
- Preserve original casing for display.
- Support Unicode names including surrogate pairs.
- Do not trim trailing spaces/dots inside valid file names.

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

### 9.2 Cache (Process-Global)

ImageList는 process-global single instance. 모든 pane 공유. (§5.3.5)

```cpp
class IconImageList {
public:
  // 32×32 BGRA, 64 initial / 32 grow / cap 1024.
  HIMAGELIST himagelist() const noexcept;

  // returns image index, or -1 if not cached (caller shows placeholder).
  int lookupByExtension(std::wstring_view ext, uint32_t attrMask) const;

  // worker thread calls this after SHGetFileInfoW.
  int insertExtension(std::wstring_view ext, HICON icon);
  int insertPerFile(std::wstring_view fullPath, HICON icon);

  // low-memory hook
  void shrinkToCap(size_t newCap);
  void clear();
};
```

| Cache | Key | Cap | Eviction |
|-------|-----|-----|----------|
| Extension cache | `(ext_hash, attrMask & ATTR_DIRECTORY)` | 500 entries | LRU |
| Per-file cache | full path hash (lower-cased) | 200 entries | LRU + watch invalidation |
| ImageList size | 32×32 BGRA = 4 KB / icon | 700 × 4 KB = **2.8 MB cap** | shrinkToCap on low memory |

Per-file exceptions: `.exe`, `.lnk`, `.url`, `desktop.ini` 파일은 path 기반 cache (각 파일이 고유 icon 보유 가능). 그 외는 extension cache로 충분.

**FileEntry에 image index 미저장**: LVN_GETDISPINFO 시 `IconExtensionCache::lookup(entry.extensionView(), entry.attributes)` 로 조회. Entry당 추가 비용 **0 B**.

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

| Operation | API | Apartment | Notes |
|-----------|-----|:---------:|-------|
| Open file | `ShellExecuteExW(SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI)` | Shell worker (STA) | default app launch |
| Open folder | internal navigation | UI thread | same pane |
| Rename | `IFileOperation::RenameItem` | Shell worker (STA) | single item only. Shell COM init 실패 시에만 `MoveFileExW` fallback |
| Create folder | `CreateDirectoryW` | Core worker (MTA) | conflict-safe default name (`New folder`, `New folder (2)`, ...) |
| Delete | `IFileOperation::DeleteItems` + `FOFX_RECYCLEONDELETE` | Shell worker (STA) | recycle-bin only. permanent delete out of scope. |

### 10.2 IFileOperation Operational Details

#### 10.2.1 Lifecycle

```cpp
// On Shell worker thread (STA):
CComPtr<IFileOperation> op;
CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
op->SetOperationFlags(FOF_NOCONFIRMMKDIR
                    | FOFX_ADDUNDORECORD       // Windows shell undo stack에 추가
                    | FOFX_RECYCLEONDELETE     // delete는 recycle bin
                    | FOFX_EARLYFAILURE        // 검증 단계에서 빠르게 실패
                    | FOFX_SHOWELEVATIONPROMPT // 권한 부족 시 elevation prompt
                    );
op->SetOwnerWindow(mainWindowHwnd);  // UI HWND. cross-thread 안전 (HWND는 process-global).
// Sink 등록:
DWORD cookie;
CComPtr<IFileOperationProgressSink> sink = new FastExplorerProgressSink(generation);
op->Advise(sink, &cookie);
// 작업 추가:
op->DeleteItems(itemArray);  // 또는 RenameItem/MoveItems
HRESULT hr = op->PerformOperations();
op->Unadvise(cookie);
```

#### 10.2.2 ProgressSink Callbacks (수집해야 할 결과)

`IFileOperationProgressSink` 구현 의무:

| Callback | 처리 |
|----------|------|
| `StartOperations` / `FinishOperations` | 시작/종료 시점 perf 이벤트 기록 |
| `PreRenameItem` / `PostRenameItem` | 개별 항목 결과 수집. `hrRename != S_OK` 시 partial failure list 추가. `S_FALSE` 반환으로 cancel 가능. |
| `PreDeleteItem` / `PostDeleteItem` | 동일 |
| `PreCopyItem` / `PostCopyItem` | MVP는 사용 안 함 (copy/cut/paste deferred) |
| `UpdateProgress` | UI에 진행률 알림 (긴 작업) |
| `PauseTimer` / `ResumeTimer` | benchmark 정확도 위해 elapsed 계산 시 제외 |

PostXxx 콜백은 cross-thread (UI HWND owner 이므로). Sink 내부에서 `PostMessage(WM_FE_OPERATION_RESULT)` 로 UI thread에 결과 전달.

#### 10.2.3 Owner HWND Lifetime

- `SetOwnerWindow(mainWindowHwnd)` — `HWND`는 process-global handle. cross-thread 사용 안전. 단:
- 메인 윈도우 destroy 시 진행 중 Shell op 가 있으면 UI 메시지 펌프 종료 → modal dialog hang 위험.
- 종료 시퀀스: `WM_CLOSE` 수신 → ShellWorker에 cancel signal → Shell op `PerformOperations` return 대기 (최대 2 sec) → window destroy.

#### 10.2.4 Undo Policy

- `FOFX_ADDUNDORECORD` 사용 → 작업이 OS shell undo stack에 등록됨.
- Fast Explorer는 자체 Ctrl+Z를 구현하지 않음 (MVP). 사용자가 Windows Explorer에서 Ctrl+Z 시 동일 작업 undo 가능.
- Undo stack은 process-global이 아니라 user session 단위. 다른 프로세스 작업과 격리 — **추측** (Shell API 공식 문서 명시 부족, 실측 필요).

### 10.3 Shell Worker

Shell operations run through `ShellWorker`, not the UI thread.

Design rules:

- ShellWorker thread는 STA, `CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)` + `PeekMessage` 루프.
- Shell COM API call을 직렬화 (1개 thread).
- Return structured `OperationResult { kind: Success|PartialSuccess|Canceled|Failed, items: vector<ItemResult> }`.
- UI remains responsive while operations run.
- File list refresh: ProgressSink의 PostXxx 결과 + `ReadDirectoryChangesW` 이벤트 둘 다 수신, deduplicate.
- Generation mismatch 시 결과 폐기.

### 10.4 Safety Rules

- No permanent delete in MVP.
- No admin elevation automation. `FOFX_SHOWELEVATIONPROMPT`는 사용자 명시 확인 후에만 활성화.
- No recursive custom delete implementation.
- Confirm destructive-looking actions when recycle-bin behavior cannot be guaranteed (예: USB drive without recycle bin).
- Never issue an operation if source/target path validation fails.
- Report partial failures explicitly with per-item error.
- Long path 작업 대상은 `\\?\` prefix 적용한 internal path 사용 (IFileOperation은 long path 지원).

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
ui.scroll.frame          # individual frame sample for p95
op.start
op.complete
fs.watch.event
```

Events include timestamp (QPC tick), pane id, generation id, path hash or sanitized path policy, item count, and duration when applicable.

### 11.1.1 Measurement Backend

| Backend | Use | Decision |
|---------|-----|----------|
| `QueryPerformanceCounter` | 모든 timestamp, duration 계산 | **MVP 1차 백엔드.** sub-microsecond 정밀도. |
| ETW custom provider | Windows Performance Analyzer / Windows Performance Recorder 분석 | **Stretch goal (M7 이후).** `TraceLoggingRegister` + ETW manifest 생성. |
| `RDTSC` | per-callback budget 측정 (LVN_GETDISPINFO 50 µs) | 보조. QPC overhead보다 가벼움. CPU migration 주의. |

In-process ring buffer (last 10,000 events) + 비동기 file dump on app close. Crash 시 `MiniDumpWriteDump` 콜백에서 ring buffer 함께 dump.

### 11.2 Logging Backend

자체 minimal ringbuffer logger 사용. MVP에서 spdlog 등 외부 의존성 도입 안 함.

| Item | Decision |
|------|----------|
| Backend | 자체 `RingLogger` (lock-free MPSC ring with per-slot publication seq + overflow drop counter) + background writer thread (MTA) |
| Location | `%LOCALAPPDATA%\FastExplorer\logs\fast-explorer-YYYYMMDD.log` (portable mode 시 `<portable_root>\logs\`) |
| Rotation | daily + 10 MB cap. 7 days retention |
| Format | `[ISO8601] [LEVEL] [thread] message` |
| Levels | TRACE / DEBUG / INFO / WARN / ERROR / FATAL |
| Flush | INFO+ 즉시 flush, TRACE/DEBUG는 buffered |
| Async writer | dedicated thread (MTA), background priority |

Path sanitization:

- Debug build: full path 로깅 허용
- Release build: `<USER>\Downloads` 같이 user profile prefix만 마스킹. 사용자 명시적으로 `--diag` flag 시 full path
- benchmark CLI output은 dataset path 그대로 (사용자 의도)

### 11.3 Crash Dump (MVP 포함)

| Item | Decision |
|------|----------|
| Handler | `SetUnhandledExceptionFilter` + `_set_invalid_parameter_handler` + `_set_purecall_handler` |
| Dump API | `MiniDumpWriteDump` |
| Dump type | `MiniDumpWithDataSegs | MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo` (사용자 동의 시) / `MiniDumpNormal` (default) |
| Path | `%LOCALAPPDATA%\FastExplorer\crashdumps\fast-explorer-PID-YYYYMMDD-HHMMSS.dmp` |
| Privacy | dump 생성 후 다음 실행 시 사용자 동의 dialog ("crash dump가 발견됐습니다. Anthropic-internal sharing 안 함, 로컬 파일만 유지하시겠습니까?") |
| WER 위임 | OFF. 자체 핸들러로 in-process dump 작성. 안정성 위해 별 thread fork + suspend pattern은 deferred. |
| Path redaction | dump 자체에는 redaction 안 함 (디버깅 가치 우선). 외부 공유 시 사용자 책임. |
| Ring buffer dump | PerfTracker ring + RingLogger ring을 user-stream으로 dump에 첨부 |

### 11.4 UI Stall Probe

The app records potential UI stalls by measuring message-loop gaps.

| Rule | Value |
|------|-------|
| Threshold | 50 ms message-loop gap |
| Measurement | message handler entry/exit QPC, 매 메시지 처리 후 gap 계산 |
| Log entry | active command name, focused pane id, current loading state, top 3 in-flight tasks |
| In release | INFO 레벨 (debug는 매번, release는 50 ms 이상만) |
| 100 ms 초과 | WARN, instrumentation 캡처 |
| 500 ms 초과 | ERROR + 자동 mini-trace dump (PerfTracker ring buffer flush) |

### 11.5 CI Regression Gate

Benchmark JSON 결과를 baseline과 비교.

| Metric | Regression Threshold |
|--------|---------------------|
| Large folder first visible rows | +15 % 또는 +30 ms 시 fail |
| UI stall count | baseline 대비 +50 % 시 fail |
| Scroll frame p95 | +20 % 시 fail |
| Memory @ 100k | +10 MB 시 fail |
| Enumeration full time | +20 % 시 warn |

Baseline은 main branch 최신 commit의 `bench-results/main/`에 저장. CI는 PR branch 결과를 baseline과 비교하여 GitHub status check report.

### 11.6 Memory Telemetry

| Event / Counter | Source | When |
|-----------------|--------|------|
| `process.workingset.delta` | `GetProcessMemoryInfo(WorkingSetSize)` | pane open / pane close / generation reset / 1s tick |
| `process.privatebytes` | `PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` | 1s tick |
| `pane.memory.estimate` | `FileModelStore::estimatedBytes()` (entries + arena + visibleOrder) | pane.first_batch / pane.enumeration.complete |
| `imagelist.cap` | `ImageList_GetImageCount` | low-memory event, periodic 10s |
| `imagelist.shrunk` | shrinkToCap 호출 | event-triggered |
| `mem.lownotify.fired` | `WAIT_OBJECT_0` from notification handle | 발생 시점 |
| `mem.caches.dropped` | low-memory 응답 시 evict 항목 수 | drop 직후 |

Debug build의 diag bar에 per-pane bytes + total resident + ImageList count 실시간 표시. Release에서는 `--diag` flag로 활성화.

Memory soak test (M7):
- 100k → 0 → 100k cycle 10회. Δ working set ≤ 5 MB (누적 leak 검출)
- 다중 pane (dual + dual nav 50회). Δ working set ≤ 10 MB

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

| Metric | Source | Aggregation |
|--------|--------|-------------|
| first entry discovered | CLI timing | median of 5 runs |
| first batch posted | core timing | median of 5 runs |
| first visible rows | app instrumentation | median + p95 of 5 runs |
| full enumeration | CLI and app | median |
| sort duration | core timing | median |
| memory snapshot | process memory query (`GetProcessMemoryInfo`) | peak working set |
| UI stall count | app instrumentation | total count over scenario |
| **scroll frame p95** | app instrumentation (per-frame QPC) | p95 over 1000 frames |
| **LVN_GETDISPINFO p99** | app instrumentation (per-callback QPC) | p99 over scroll session |
| cancellation latency | pane generation event pair | median + max |
| **icon-disabled vs enabled delta** | app instrumentation | first_visible 시간 차이 percent |

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

MVP는 dependency-free `core-tests.exe` (self-contained assert macro + simple test registry). Catch2/doctest 도입은 Milestone 7 이후 재검토.

| Area | Test |
|------|------|
| path utils | normalization, `\\?\` prefix add/strip, long path, UNC rejection, invalid path rejection |
| file model | append batches, clear generation, row lookup, name arena overflow |
| selection | stable selection after sort |
| sorting | name/type/size/date deterministic order, secondary key tiebreak |
| cancellation | stale generation discarded (L1), stop_token observed within batch boundary (L2) |
| error model | Win32 error conversion |
| benchmark generator | expected file counts and names |
| FileEntry layout | `static_assert(sizeof(FileEntry) <= 64)` |
| WM_FE_* message IDs | no overlap with WM_APP system reserved range |

### 13.1.1 FS Backend Abstraction

`IFsBackend` interface로 enumeration / file ops 추상화. unit test는 in-memory backend, integration test는 real Win32 backend.

```cpp
class IFsBackend {
public:
  virtual Result<EnumerationHandle> openEnumeration(const std::wstring& path, std::stop_token tok) = 0;
  virtual Result<std::optional<FileEntry>> next(EnumerationHandle&) = 0;
  // ...
};
```

real backend는 `Win32FsBackend` (FindFirstFileExW), test backend는 `MemoryFsBackend` (predefined directory tree).

### 13.2 Integration Tests

Use generated folders under `D:\tmp\fast-explorer-test` by default. 또는 환경변수 `FAST_EXPLORER_TEST_ROOT` override.

**Benchmark는 RAM disk 우선 사용** (M7 결정):
- ImDisk Virtual Disk Driver로 RAM disk mount (`R:` 권장)
- OS file cache, Windows Defender, Search Indexer 영향 최소화
- bench result JSON에 `medium: ramdisk` 또는 `medium: ssd` 기록

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
| per-monitor DPI change (window 이동 between monitors) | UI 즉시 rescale, blur 없음 |
| sort large folder | command accepted quickly; UI remains responsive |
| dual pane load | panes load independently |
| icon enabled/disabled | file names always render first |
| delete to recycle bin | operation result is explicit |
| OneDrive 폴더 진입 (placeholder 파일 다수) | hydration 트리거 없이 즉시 표시 |
| junction/symlink 표시 | attribute 컬럼 J/L 마커 표시, recursive enter 차단 |
| long path (>260 chars) | 정상 enumeration + open |
| UNC path 입력 | 명시적 거부 메시지 |
| missing path | clear error, no crash |
| 1시간 soak test | crash 없음, 메모리 정상 |
| crash dump 생성 | 다음 실행 시 동의 dialog, 동의 시 보존 |

### 13.4 UI Automation (Stretch, M7)

- Framework 결정 deferred (FlaUI vs WinAppDriver). Plan §12.1 N3 참고.
- Smoke 자동화 시나리오 후보:
  - launch → AddressBar 입력 → first row visible 검증 (timing assertion)
  - rapid folder switch 10회 → 모든 generation mismatch 결과가 폐기되는지
  - sort 명령 → 50 ms 내 command accepted, 결과는 background

### 13.5 Test Determinism Rules

- Real FS dependent test는 RAM disk 또는 sandbox folder 사용. user profile data 접근 금지.
- Defender exclusion: bench root path를 Windows Defender exclude list에 등록 (수동 또는 setup script).
- Indexer 차단: bench root에 `desktop.ini` 또는 attribute로 인덱서 제외.
- 시간 dependent test는 `IClock` 추상화로 mock.

---

## 14. Implementation Order

각 milestone exit criteria는 해당 단계 성능 게이트 측정값 포함. 기준 미달 발견 시 다음 milestone로 진행하기 전 architecture 재검토.

### 14.1 Milestone 1: Native Scaffold

Deliverables:
- CMake project (`/MD` shared CRT, MSVC v143)
- Application manifest (longPathAware, DPI v2, common controls v6) — §2.1.1
- Win32 app entry point + `OleInitialize` (STA)
- main window with WM_DPICHANGED handler
- command bar/address bar placeholder
- empty file pane
- RingLogger + crash handler skeleton (§11.2, §11.3)
- local logging directory + portable mode override

Exit criteria:
- app launches and closes cleanly on Win10 + Win11
- **warm launch ≤ 500 ms** 측정값 기록
- crash handler가 가짜 crash로 dump 생성 검증
- per-monitor DPI 전환 시 UI 즉시 rescale 검증
- **startup process working set ≤ 25 MB** (빈 window 상태, 아직 pane 없음)
- `SetProcessWorkingSetSizeEx` 호출 + low-memory notification 등록 동작 확인

### 14.2 Milestone 2: Core Enumeration

Deliverables:
- path utilities (`toInternal`/`toDisplay`, `\\?\` prefix, UNC reject)
- `IFsBackend` + `Win32FsBackend` + `MemoryFsBackend`
- `DirectoryEnumerator` (FindFirstFileExW + FindExInfoBasic + LARGE_FETCH)
- `FileEntry` (`static_assert(sizeof <= 64)`) + name arena
- `FileModelStore`
- first benchmark CLI command (`generate`, `enumerate`)
- `QueryPerformanceCounter` 기반 PerfTracker

Exit criteria:
- CLI enumerates generated small/medium/large-flat datasets
- core tests cover path, model, FileEntry layout, cancellation L2
- `static_assert(sizeof(FileEntry) == 40)` 통과 + name arena commit/decommit 동작 검증
- **CLI에서 small folder ≤ 50 ms, medium ≤ 100 ms** 측정값 기록
- **FindFirstFileExW vs GetFileInformationByHandleEx head-to-head 측정값 기록** → final API 확정 (Plan §12.1 N1 해소)
- **100k entries pane memory ≤ 15 MB** (CLI 측정, structural + arena만)

### 14.3 Milestone 3: Virtual List UI

Deliverables:
- `LVS_OWNERDATA` list control with `LVS_EX_DOUBLEBUFFER`
- LVN_GETDISPINFO / LVN_ODCACHEHINT / LVN_ODSTATECHANGED / NM_CUSTOMDRAW 핸들러
- batch append from worker to UI via `WM_FE_ENUM_BATCH`
- format LRU cache for size/modified
- loading/partial/ready/error states
- UI stall probe (§11.4)

Exit criteria:
- UI opens local folder
- 10k folder remains interactive during loading
- **UI에서 medium folder first visible rows ≤ 100 ms** 측정값
- **LVN_GETDISPINFO p99 ≤ 50 µs** 측정값 (100k row scroll)
- UI stall ≤ 50 ms 검증

### 14.4 Milestone 4: Navigation And Cancellation + FS Watch

Deliverables:
- address bar navigation (Ctrl+L)
- enter folder, up (Alt+Up), back/forward (Alt+Left/Right), refresh (F5)
- per-pane history
- generation token + `std::stop_source` cancellation (L1 + L2)
- ReadDirectoryChangesW + IOCP watcher thread (§6.5)
- WM_FE_FS_CHANGE 처리 + coalesce

Exit criteria:
- rapid folder switching does not apply stale results
- **cancellation latency ≤ 50 ms** 측정값
- ReadDirectoryChangesW 이벤트 수신 후 UI 100 ms 내 반영
- 100k folder rapid switch 10회 soak — generation mismatch 결과 0% UI 도달

### 14.5 Milestone 5: Sorting And Selection

Deliverables:
- name/type/size/modified sort (CompareStringOrdinal IgnoreCase)
- visibleOrder vector model
- stable selection by FileEntry::id
- 2,000 row threshold (direct sort vs background sort)
- keyboard (`F2`, `Enter`, `Delete`, `Ctrl+1`/`Ctrl+2`, `Ctrl+H`, `Tab`) + mouse basics

Exit criteria:
- sort 명령 ≤ 50 ms accepted (UI feedback)
- large sort (100k) does not block UI > 50 ms
- selected rows remain coherent after sort
- sort tiebreak deterministic

### 14.6 Milestone 6: Icons And Basic Operations

Deliverables:
- placeholder icons + extension-level icon cache (LRU bounded)
- IconProvider (STA worker pool) using `SHGetFileInfoW` with `SHGFI_USEFILEATTRIBUTES` for placeholders
- cloud placeholder 회피 (§7.3.5)
- ShellWorker (STA) — `IFileOperation` lifecycle (§10.2)
- IFileOperationProgressSink 구현
- open file (`ShellExecuteExW`), rename, create folder, recycle-bin delete

Exit criteria:
- icon loading never delays file names (icon enabled/disabled delta ≤ 20 % on first_visible)
- file operations return structured `OperationResult`
- OneDrive 폴더 enumeration에서 hydration trigger 0건 검증
- Crash dump path가 portable mode override를 따름
- **ImageList cap ≤ 3 MB** 측정 + low-memory notification 시 shrink 동작 확인

### 14.7 Milestone 7: Benchmark And Stabilization

Deliverables:
- full dataset generator presets (small/medium/large-flat/mixed-names/mixed-types/many-dirs/deep-tree)
- memory snapshot (`GetProcessMemoryInfo`)
- UI stall probe full integration
- scroll frame p95 / LVN_GETDISPINFO p99 측정
- benchmark result JSON with machine info
- baseline 비교 CI script (§11.5)
- 1-hour soak test checklist
- Optional: ETW custom provider, UI automation smoke (Plan §12.1 N2/N3/N4 해소)

Exit criteria:
- **large folder first row ≤ 200 ms** 종합 측정
- **UI stall single ≤ 50 ms** 100k 시나리오 검증
- **scroll p95 ≤ 16.7 ms** 측정
- **100k entries process working set ≤ 50 MB target / ≤ 100 MB budget** 측정
- **Memory soak: 100k→0→100k cycle 10회 누적 working set Δ ≤ 5 MB**
- **Multi-pane soak: dual nav 50회 누적 working set Δ ≤ 10 MB**
- `EmptyWorkingSet` 호출 후 working set 회복 ≤ 200 ms 검증
- Low-memory notification 시 caches drop 검증
- 1-hour soak: crash 0, memory leak 0
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
| First visible rows priority | Sections 2.3, 4.4.1, 7, 11, 12 |
| UI thread never blocked | Sections 3, 6 (COM apartment), 7, 8, 10 |
| Virtualized rows | Section 4.4 |
| Async icons/metadata | Section 9 |
| Benchmark harness | Section 12 |
| Basic navigation | Sections 4.5, 14.4 |
| Multi-pane UX | Sections 4.2, 14.4 |
| Error model | Sections 5.3, 7.4, 10 |
| Safety-first operations | Sections 10, 15 |
| Long path / Unicode | Sections 7.3.1, 7.3.2, 7.3.6 |
| Cancellation correctness | Sections 6.3 (3-layer) |
| Crash + soak test stability | Sections 11.3, 11.4, 13.3 |
| Native explorer parity (FS watch) | Section 6.5 |
| Plan §16.1 Locked Decisions | Sections 2.1, 6.1 |
| Plan §16.2 Threading Lock | Section 6.1 |
| Plan §16.3 Cancellation Lock | Section 6.3 |
| Plan §16.4 FS Edge Case Lock | Section 7.3 |
| Plan §16.5 Observability Lock | Sections 11.1.1, 11.2, 11.3, 11.4, 11.5 |
| Plan §16.6 DPI v2 + manifest in MVP | Sections 2.1, 2.1.1 |
| Plan §16.7 ReadDirectoryChangesW in MVP | Section 6.5 |

---

## 17. Deferred Decisions

These are intentionally not part of MVP implementation:

### 17.1 Feature Deferred

- Direct2D/DirectWrite custom file list
- quad layout as a release gate
- dual-vertical layout
- Shell context menu
- drag-and-drop (OLE drop target/source)
- thumbnails (`IThumbnailProvider`)
- folder recursive size
- plugin system
- archive browsing (zip-as-folder)
- network drive optimization (UNC, SMB)
- cloud provider 직접 통합 (OneDrive/Google Drive 전용 API)
- Windows Explorer replacement registration
- app updater, code signing, installer (MSIX or MSI)
- Ctrl+Z 자체 undo (OS shell undo stack 사용)
- copy / cut / paste 큐 (M6은 단일 명령만)
- 사용자별 column 설정, column reorder persist
- Filter / search-as-you-type
- 다국어 UI strings (English only in MVP)

### 17.2 Platform / System Deferred

- Dark mode (`SetWindowTheme(L"DarkMode_Explorer", ...)` + undocumented uxtheme #135)
- High contrast theme 특화 처리
- HiDPI 아이콘 (`IShellItemImageFactory::GetImage` 256x256). MVP는 `SHGetFileInfoW` 32x32만
- Accessibility custom UIA provider (MVP는 List-View 기본 MSAA로 커버)
- IME 커스텀 처리 (MVP는 기본 EDIT 컨트롤로 커버)
- Window snap layouts custom integration
- Tablet/touch optimization

### 17.3 Observability / Build Deferred

- ETW custom provider (M7 stretch)
- spdlog 또는 외부 logging library
- WER (Windows Error Reporting) 통합
- 외부 telemetry (Application Insights 등)
- Catch2 / doctest test framework
- UI 자동화 (FlaUI, WinAppDriver)
- MSIX packaging
- Static analyzer (PVS-Studio, Clang-Tidy) CI 통합
- AddressSanitizer / UndefinedBehaviorSanitizer 통합

이 항목들은 benchmark와 responsiveness 게이트 통과 후 재검토.

---

## 18. Design Completion Checklist

- [x] Architecture defined
- [x] UI structure defined
- [x] Threading model defined (incl. COM apartment)
- [x] Cancellation model defined (3-layer)
- [x] Directory enumeration design defined
- [x] Virtual list design defined (incl. LVN_GETDISPINFO budget)
- [x] Data model defined (incl. FileEntry sizeof bound)
- [x] File operation design defined (incl. IFileOperation ProgressSink)
- [x] Error model defined
- [x] Benchmark design defined
- [x] Test plan defined (incl. IFsBackend mock, RAM disk policy)
- [x] Implementation order defined (with per-milestone perf gates)
- [x] Application manifest defined (longPathAware, DPI v2, common controls v6)
- [x] CRT linkage + MSVC toolset + Windows SDK locked
- [x] Crash handler + logging backend defined
- [x] Long path / reparse / UNC / cloud placeholder policy defined
- [x] Filesystem watch (ReadDirectoryChangesW) MVP scope decided
- [x] CI regression gate defined

