---
date: 2026-05-14T10:58:49Z
git_commit: 54c0260
branch: main
status: handoff
project_type: windows-desktop-cpp
---

# 핸드오프: fast-explorer M1 완료, M2 sub-step 1–2 완료

## 작업 내용

PDCA Phase: **Do (active)** — milestone 진행도 M1 ✅ / M2 진행 중.

- **M1 Native Scaffold** — ✅ Completed (`3e3f010`, doc mark `04e45f5`)
  - Walking skeleton, PerfTracker (QPC + per-slot seq publish), RingLogger (MPSC + async writer + UTF-8 file), CrashHandler (MiniDumpWriteDump + CRT handlers + PerfTracker user-stream), ProcessMemoryService (working set hint + EmptyWorkingSet throttle + low-memory notifier), AppServices DI aggregate
  - Exit gates 측정: warm launch 21–36 ms, startup working set 10.3 MB, real SEH dump 831 KB
- **M2 sub-step 1** — ✅ Test bootstrap (`ee9905a`)
  - Self-contained `tests/test-harness.h` (registry + FE_ASSERT_*), `test-main.cpp` runner, CMake `core-tests` target
- **M2 sub-step 2** — ✅ path-utils 확장 (`a3de0e2`) + review fixes (`54c0260`)
  - `toInternal` / `toDisplay` / `isUncPath` / `PathConvertError` (Design §7.3)
  - 27 단위 테스트 통과
  - L1+L2+PR review 결과 7건 fix (C1 ensureDirectoryRecursive iterative, H1–H6 ADS colon / forward-slash UNC / drive root / test harness DRY / AssertionFailure std::exception 파생 / EnvOverride 2-pass)
- **M2 sub-step 3 이상** — ⏳ 미시작
  - 다음 선택지: FileEntry POD vs NameArena vs 둘 묶음 vs IFsBackend interface 먼저
  - 사용자에게 `AskUserQuestion`으로 질문하고 선택받기 (메모리 규칙)

## 핵심 참조 문서

- `docs/02-design/features/fast-explorer-core.design.md` v1.0.3 — §5.1 FileEntry (40 B), §5.2.1 NameArena, §5.3 Memory Optimization, §7 Enumeration, §14 Milestone exit criteria
- `docs/01-plan/features/fast-explorer-core.plan.md` v1.0.2 — §16 Locked Decisions (tech stack/threading/memory)

## 최근 변경사항

- `src/core/path-utils.h:30-46` — `PathConvertError` enum, `isUncPath`/`toInternal`/`toDisplay` 선언
- `src/core/path-utils.cpp:58-94` — `ensureDirectoryRecursive` iterative 재작성 (heap-backed std::wstring, `isUncreatableRoot` base case)
- `src/core/path-utils.cpp:25-42` — `containsInvalidPathChar` ADS colon 거부 (caller가 drive prefix skip 후 호출)
- `src/core/path-utils.cpp:62-80` — `isUncPath` forward-slash 검출
- `src/core/path-utils.cpp:82-126` — `toInternal` `isAbsoluteDrivePath`로 drive root 요구 (`X:\` 필수)
- `tests/test-harness.h` — `AssertionFailure : std::exception`, `failAssertion` 헬퍼, `fe_lhs__`/`fe_rhs__` 식별자
- `tests/path-utils-tests.cpp:25-50` — `EnvOverride` 2-pass `GetEnvironmentVariableW`
- `tests/path-utils-tests.cpp:170-220` — 7 regression cases (forward-slash UNC, ADS colon, bare drive, drive root)

## 학습한 내용

### 메모리 규칙 (`C:\Users\user\.claude\projects\D--work-private-fast-explorer\memory\MEMORY.md`)
1. **AskUserQuestion 강제** — 분기 질문은 모두 `AskUserQuestion` 툴 사용. plain-text 질문 금지. (`feedback_use_question_tool.md`)
2. **Milestone 단위 batch review** — milestone 내 sub-step 사이에 review 끼우지 않음. 모든 minimum 구현 후 team agent + skill 일괄 리뷰. (`feedback_batch_review.md`)
3. **Caveman mode 활성** — 응답은 fragments OK, 관사/필러 drop. 단 commit/PR/security 본문은 정상 영어.

### 빌드 환경
- VS 2026 Professional, MSVC v143, C++20, `/MD` shared CRT
- Build: `cmake -S . -B build -G Ninja` → `cmake --build build --config Release`
- VS Dev Shell 진입 후 cmake 호출 필요 (PowerShell: `Launch-VsDevShell.ps1 -Arch amd64`)
- `core-tests.exe`로 단위 테스트 (27/27 pass), `FastExplorer.exe` 131 KB

### COM apartment 정책 (Design §6.1)
- UI thread: STA (`OleInitialize`)
- ShellWorker (M6 도입): STA
- Core worker pool (M2~): MTA, Shell COM 금지
- RingLogger writer thread, ProcessMemoryService notifier: MTA

### Crash handler 재진입 회피 (M1 review에서 fix됨)
- Crash handler 안에서 `RingLogger::*` 호출 금지. `OutputDebugStringW` + 스택 버퍼만.
- 동일 패턴을 M2 신규 코드에도 유지할 것.

### Singleton 정책
- 4 service singleton 제거됨 (`3e3f010` AppServices). 새 service는 `AppServices`에 멤버로 추가하고 logger 등의 의존성은 생성자 주입.

### 디스크 파일 정책
- 로그: `%LOCALAPPDATA%\FastExplorer\logs\fast-explorer-YYYYMMDD.log` (또는 `$FAST_EXPLORER_PORTABLE_ROOT\logs\`)
- Crash dump: 동일 root의 `crashdumps\fast-explorer-<PID>-YYYYMMDD-HHMMSS.dmp`

### Clang LSP false-positive
- 프로젝트 LSP가 Windows SDK include path 미설정. `<windows.h>` 관련 진단 다수 표시되지만 MSVC 빌드는 정상. 무시 가능.

## 생성된 산출물

신규 파일:
- `tests/test-harness.h`
- `tests/test-main.cpp`
- `tests/path-utils-tests.cpp`
- `src/core/path-utils.{h,cpp}` (M1 시점에 생성, M2에서 확장)
- `src/core/perf-tracker.{h,cpp}`
- `src/core/ring-logger.{h,cpp}`
- `src/core/crash-handler.{h,cpp}`
- `src/core/process-memory.{h,cpp}`
- `src/app/app-services.{h,cpp}`
- `src/app/main.cpp`
- `src/ui/main-window.{h,cpp}`
- `resources/FastExplorer.exe.manifest`, `FastExplorer.rc`
- `CMakeLists.txt`, `.gitattributes`
- `docs/01-plan/features/fast-explorer-core.plan.md` v1.0.2
- `docs/02-design/features/fast-explorer-core.design.md` v1.0.3
- `C:\Users\user\.claude\projects\D--work-private-fast-explorer\memory\` (3 파일)

PDCA state (gitignored): `.rkit/state/pdca-status.json`

## 다음 작업 항목

1. `AskUserQuestion`으로 M2 sub-step 3 선택 받기:
   - FileEntry POD + static_assert 단독
   - NameArena 단독
   - 둘 묶음 (자주 쓰이는 짝)
   - IFsBackend interface 먼저
2. 선택된 sub-step **최소 단위** 구현 + 단위 테스트 동시 작성 (`tests/` 에 추가)
3. 빌드 + `core-tests.exe` 통과 확인 → commit + push
4. 이후 sub-step (4–11) 연속 진행, milestone 사이 review 금지 (batch 규칙)
5. M2 모든 minimum 구현 완료 후:
   - `rkit:code-analyzer` (L1) + `rkit:c-cpp-reviewer` (L2) + `pr-review-toolkit:code-reviewer` (PR) 병렬 spawn
   - Critical/High 우선 적용 → commit
6. Design §14.2 exit gate 측정값 기록 → M2 done mark commit → M3 진입

### M2 남은 sub-step (계획)

| # | Component |
|:-:|---|
| 3 | FileEntry POD + `static_assert(sizeof == 40)` |
| 4 | NameArena (VirtualAlloc 16 MB reserve + 64 KB commit + decommit) |
| 5 | IFsBackend interface |
| 6 | MemoryFsBackend (test mock) |
| 7 | Win32FsBackend (FindFirstFileExW + LARGE_FETCH) |
| 8 | FileModelStore |
| 9 | DirectoryEnumerator (worker + std::stop_token) |
| 10 | bench CLI `FastExplorerBench.exe` (`generate` + `enumerate`) |
| 11 | head-to-head 측정 (Plan §12.1 N1 해소) |

### M2 Exit Gates (Design §14.2)

- `static_assert(sizeof(FileEntry) == 40)` 컴파일 통과
- NameArena commit/decommit 검증
- CLI: small ≤ 50 ms, medium ≤ 100 ms 측정값 기록
- 100k entries pane memory ≤ 15 MB
- core-tests.exe 통과
- FindFirstFileExW vs GetFileInformationByHandleEx head-to-head

## 기타 참고사항

**Project type**: Windows native C++ 데스크톱 (임베디드/WPF 아님). Target: Windows 11 x64 first, Windows 10 1809+ best-effort. MSVC v143, Win11 SDK 10.0.22621.0+.

**Test 실행**:
```powershell
& "build\core-tests.exe"
# Expected: 27 passed, 0 failed (current head 54c0260)
```

**Manual crash test**:
```powershell
& "build\FastExplorer.exe" --crash-test           # manual dump
& "build\FastExplorer.exe" --crash-test=throw     # real SEH (exit 0xC0000005)
& "build\FastExplorer.exe" --crash-test=invalid   # _set_invalid_parameter_handler
```

**Repository**: `https://github.com/solitasroh/fast-explorer.git` (origin main)

**활성 메모리 규칙 (반드시 준수)**:
- 분기 질문 시 `AskUserQuestion` 툴 사용 (plain-text 금지)
- Milestone 진행 중 review agent spawn 금지 — 모든 minimum 구현 후 batch
- Caveman mode 응답 (fragments OK, drop fluff). 단 commit/PR 본문은 정상.

**알려진 deferred 항목 (M2 후반에서 처리)**:
- CMake OBJECT library (`src/core/*` 중복 빌드 해소)
- `SHGetKnownFolderPath` unique_ptr RAII
- RingLogger LogFormatter/LogSink/LogRing 책임 분리 (다중 sink 추가 시)
- PerfTracker Event 확장 (paneId/generation 필드 — M2 enumeration 등장 시)
