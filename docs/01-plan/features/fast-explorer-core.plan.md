# fast-explorer-core - Plan Document

> **Summary**: Windows용 초고성능 파일 탐색기 MVP의 제품 방향, 성능 목표, 범위, 리스크, 검증 기준을 정의한다.
>
> **Author**: Codex
> **Created**: 2026-05-14
> **Status**: Review
> **Version**: 1.0.2
> **Level**: Starter

---

## Version History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 1.0.0 | 2026-05-14 | 초기 상세 계획 문서 작성 | Codex |
| 1.0.1 | 2026-05-14 | Design 결정 역반영 (Conditional Scope 확정), Schedule milestone별 성능 게이트 분산, Open Questions 해소 표 추가, §16 Locked Decisions 추가 (Teammate review 결과 반영) | Claude |
| 1.0.2 | 2026-05-14 | §16.8 Memory Lock 추가 (FileEntry 40 B, per-pane ≤ 10 MB, process working set target ≤ 50 MB, name arena VirtualAlloc, ImageList ≤ 3 MB, OS working set 핸들러, STL exclusions, memory soak gate). Design v1.0.2와 페어. | Claude |

## Related Documents

- Design: [fast-explorer-core.design.md](../../02-design/features/fast-explorer-core.design.md)
- Analysis: `docs/03-analysis/fast-explorer-core.analysis.md` 예정
- Report: `docs/04-report/features/fast-explorer-core.report.md` 예정
- Brainstorm visual notes: `.superpowers/brainstorm/`

---

## 1. Overview

### 1.1 Purpose

`fast-explorer-core`의 목적은 Windows 환경에서 Q-Dir, Directory Opus, Total Commander, XYplorer, Files, OneCommander, Windows 파일 탐색기와 경쟁할 수 있는 파일 탐색기 애플리케이션의 첫 번째 MVP를 정의하는 것이다.

이 MVP의 핵심 가치는 기능 수가 아니라 **체감 반응성**이다. 사용자가 폴더에 진입했을 때 목록이 즉시 보이고, 대용량 폴더에서도 UI가 멈추지 않으며, 아이콘/썸네일/폴더 크기/메타데이터 같은 비핵심 정보는 뒤에서 안전하게 채워져야 한다.

이 문서는 구현 문서가 아니라 Plan 단계 문서다. 따라서 세부 클래스 구조, 메시지 루프, 렌더링 방식, 파일 열거 API 선택은 다음 Design 단계에서 확정한다. 다만 제품 목표와 기술 방향은 아래처럼 명확히 고정한다.

- 제품 방향: 성능 우선 Windows 파일 탐색기 대체재
- 우선 대상: 로컬 디스크
- 성능 기준: 전체 로딩 완료보다 첫 화면 반응성 우선
- 기술 방향: C++ 네이티브 Windows 앱
- UI 방향: C++ 기반 Win32 중심, 필요 시 Direct2D/DirectWrite 또는 owner-data virtual list 검토
- Core 방향: C++ 네이티브 파일 시스템 엔진 우선

### 1.2 Background

사용자는 Q-Dir처럼 여러 폴더를 동시에 다룰 수 있는 Windows 파일 탐색기 애플리케이션을 원한다. 동시에 기존 타사 제품과 Windows 파일 탐색기를 면밀히 분석해 차별화해야 하며, 가장 중요한 요구사항은 성능과 안정성이다.

초기 브레인스토밍에서 다음 결정이 확정되었다.

| Decision | Selected Direction |
|----------|--------------------|
| MVP 방향 | 성능 우선 탐색기 대체재 |
| 우선 저장소 | 로컬 디스크 |
| 성능 기준 | 체감 반응성 우선 |
| UI 기술 관점 | C#보다 C++ 네이티브 선호 |
| Core 기술 관점 | Rust도 가능하지만 MVP 일관성은 C++ 네이티브 우선 |

경쟁 제품의 큰 패턴은 다음과 같다.

| Product | Main Strength | Planning Implication |
|---------|---------------|----------------------|
| Q-Dir | 4분할 화면, 포터블, Windows 탐색기와 유사한 사용성 | 다중 패널 UX는 차별화 요소가 아니라 기본 기대치다. |
| Directory Opus | 강력한 커스터마이징, 네이티브 성능, 파일 작업, 검색, 스크립팅 | 기능 깊이를 정면 승부하기보다 응답성 기준을 더 엄격히 잡아야 한다. |
| Total Commander | 키보드 중심 듀얼 패널, 파일 작업, 플러그인, FTP/SFTP, 압축 | 작업 기능은 장기 경쟁 영역이며 MVP에서는 범위를 제한한다. |
| XYplorer | 가벼움, 포터블, 탭, 검색, 태그, 스크립팅 | 가벼운 실행과 빠른 탐색은 직접 비교 대상이다. |
| Files / OneCommander | 현대적 UI, 탭, 프리뷰, 태그, 컬럼 탐색 | 보기 좋은 UI보다 큰 폴더에서 멈추지 않는 UI가 우선이다. |
| Windows File Explorer | OS 통합, 기본 앱, 셸 호환성 | 셸 호환성과 안정성을 무시하면 실사용 대체재가 될 수 없다. |

### 1.3 Product Hypothesis

대부분의 파일 탐색기는 기능이 많아질수록 체감 성능과 예측 가능성이 흔들린다. Fast Explorer는 반대로 접근한다.

**가설**: 로컬 디스크 탐색에서 "폴더 진입 즉시 반응", "UI 무정지", "대용량 폴더 스크롤 안정성", "비동기 메타데이터 로딩"을 제품 정체성으로 삼으면, 기능 수가 적은 MVP라도 파워유저에게 명확한 차별점을 줄 수 있다.

검증은 감상이 아니라 벤치마크와 사용자 체감 테스트로 한다.

### 1.4 Operating Principles

1. UI thread must stay responsive.
2. First visible rows matter more than full metadata completion.
3. Anything slow must be asynchronous, cancellable, and deprioritized.
4. Shell integration must never block the primary file list.
5. Benchmarks are product requirements, not optional QA.
6. No feature may enter MVP if it breaks the performance budget.
7. File operations must prioritize data safety over clever shortcuts.
8. Native Windows behavior should be respected unless it conflicts with responsiveness.

### 1.5 Target Users

Primary users:

- Windows에서 파일을 많이 다루는 개발자
- 대량 이미지, 문서, 로그, 빌드 산출물을 관리하는 파워유저
- 여러 폴더를 동시에 비교하고 이동하는 사용자
- Windows 파일 탐색기의 느린 폴더 진입, 멈춤, 탭/창 관리 한계에 불만이 있는 사용자

Secondary users:

- Q-Dir 사용 경험이 있는 멀티패널 선호 사용자
- Total Commander 계열의 듀얼 패널 워크플로우를 선호하지만 더 현대적이고 빠른 UI를 원하는 사용자
- Directory Opus 수준의 복잡한 커스터마이징은 원하지 않지만 빠르고 신뢰할 수 있는 탐색기를 원하는 사용자

### 1.6 Product Positioning

초기 포지션은 다음 한 문장으로 정의한다.

> Fast Explorer is a native Windows file explorer focused on instant local-folder responsiveness and stable multi-pane navigation.

한국어 제품 정의:

> Fast Explorer는 로컬 폴더를 즉시 열고, 대용량 폴더에서도 멈추지 않는 네이티브 Windows 멀티패널 파일 탐색기다.

---

## 2. Goals

### 2.1 Primary Goals

- [ ] Windows 네이티브 C++ 애플리케이션으로 MVP를 설계한다.
- [ ] 로컬 디스크 폴더 진입 시 첫 목록 표시를 최우선 성능 목표로 둔다.
- [ ] 대용량 폴더에서도 UI thread가 장시간 차단되지 않는 구조를 만든다.
- [ ] 파일 목록 렌더링은 가상화 기반으로 설계한다.
- [ ] 아이콘, 썸네일, 폴더 크기, 상세 메타데이터는 지연 로딩한다.
- [ ] 성능 벤치마크 하네스를 MVP의 일부로 포함한다.
- [ ] Windows 파일 탐색기와 주요 타사 탐색기 대비 성능 비교가 가능한 측정 기준을 정의한다.
- [ ] 기본 파일 탐색 워크플로우를 제공한다: 경로 이동, 상위 폴더 이동, 새로고침, 정렬, 선택, 열기.
- [ ] 최소한의 멀티패널 UX를 제공한다: 단일/듀얼/쿼드 레이아웃은 Design 단계에서 최종 결정한다.
- [ ] 오류가 사용자 작업을 조용히 실패시키지 않도록 명시적 오류 모델을 둔다.

### 2.2 Performance Goals

아래 수치는 초기 목표다. Design 또는 첫 벤치마크 결과에서 비현실적이거나 너무 약한 기준으로 드러나면 문서를 갱신한다.

#### 2.2.1 Core Responsiveness Metrics

| Metric | Initial Target | Notes |
|--------|----------------|-------|
| App warm launch to interactive | <= 500 ms | 이미 OS 캐시가 있는 상태 기준 |
| App cold launch to interactive | <= 1,500 ms | 환경 차이가 크므로 초기 목표로만 사용 |
| Small folder first visible rows | <= 50 ms | 1,000개 이하 항목, 로컬 SSD |
| Medium folder first visible rows | <= 100 ms | 10,000개 항목 |
| Large folder first visible rows | <= 200 ms | 100,000개 항목 |
| UI blocked duration | No single block > 50 ms | 모든 사용자 입력 처리 기준 |
| Scroll interaction | p95 frame <= 16.7 ms after data ready | 60Hz 기준 |
| Sort command accepted | <= 50 ms | 정렬 완료가 아니라 명령 접수/피드백 기준 |
| Folder switch cancellation | <= 50 ms to stop obsolete UI updates | 이전 폴더 로딩 결과가 새 화면을 오염시키면 실패 |
| 100k base entry memory | <= 100 MB incremental | 아이콘/썸네일 제외한 파일 모델 기준 |

#### 2.2.2 Perceived Performance Rules

- 폴더에 들어가면 전체 열거 완료 전이라도 첫 배치가 표시되어야 한다.
- 사용자는 로딩 중에도 스크롤, 경로 변경, 취소, 새 탭/패널 전환을 할 수 있어야 한다.
- 정렬이 오래 걸리면 진행 상태를 보여주고 UI를 계속 열어둔다.
- 아이콘이 늦게 뜨더라도 파일명과 기본 정보가 먼저 표시되어야 한다.
- 썸네일과 폴더 크기는 MVP 기본 경로에서 자동 전체 계산하지 않는다.
- 백그라운드 작업 결과는 현재 화면 세대와 일치할 때만 적용한다.

#### 2.2.3 Benchmark Dataset Plan

| Dataset | Size | Purpose |
|---------|------|---------|
| `bench-small` | 200 files | 일반 폴더 진입 기준 |
| `bench-medium` | 10,000 files | 개발 폴더/문서 폴더 기준 |
| `bench-large-flat` | 100,000 files | 극단적 단일 폴더 기준 |
| `bench-mixed-names` | 50,000 files | Unicode, 긴 이름, 공백, 특수문자 |
| `bench-mixed-types` | 30,000 files | 다양한 확장자, 아이콘 요청 부하 |
| `bench-many-dirs` | 20,000 folders | 폴더 우선 정렬/탐색 기준 |
| `bench-deep-tree` | depth 20+ | 경로 처리, 상위 이동, 긴 경로 |

초기 MVP는 로컬 NVMe SSD를 기준으로 측정한다. HDD, USB, 네트워크 드라이브, 클라우드 동기화 폴더는 별도 단계에서 다룬다.

### 2.3 Stability Goals

- [ ] 잘못된 경로, 권한 없음, 삭제된 파일, 잠긴 파일, 긴 경로를 명시적으로 처리한다.
- [ ] Shell/COM 호출 실패가 앱 전체 크래시로 이어지지 않도록 격리한다.
- [ ] 취소된 폴더 로딩 결과가 현재 UI에 반영되지 않도록 세대 토큰을 둔다.
- [ ] 파일 작업은 실패/부분 성공/사용자 취소를 구분해 보고한다.
- [ ] 모든 백그라운드 작업은 앱 종료 시 안전하게 정리된다.
- [ ] 대용량 폴더를 반복 이동해도 메모리 누수가 없어야 한다.
- [ ] MVP 완료 전 최소 1시간 이상 탐색/전환 soak test를 통과한다.

### 2.4 UX Goals

- [ ] 첫 화면은 실제 파일 탐색 화면이어야 하며 랜딩/마케팅 페이지를 두지 않는다.
- [ ] 키보드와 마우스 양쪽 모두 빠르게 사용할 수 있어야 한다.
- [ ] 기본 조작은 Windows 파일 탐색기 사용자에게 낯설지 않아야 한다.
- [ ] 성능 상태를 과한 애니메이션 없이 조용하고 명확하게 표현한다.
- [ ] 레이아웃 전환은 사용자가 폴더 작업 흐름을 잃지 않게 해야 한다.
- [ ] 다중 패널은 성능을 해치지 않도록 각 패널 로딩을 독립적으로 취소/스케줄링한다.
- [ ] 오류 메시지는 기술적 원인을 숨기지 않되 사용자가 다음 행동을 알 수 있게 한다.

### 2.5 Engineering Goals

- [ ] C++20 또는 C++23 기준으로 시작한다.
- [ ] Windows x64를 우선 타깃으로 한다.
- [ ] Windows 11을 1차 타깃으로 하고 Windows 10 지원 여부는 Design 단계에서 결정한다.
- [ ] 빌드는 MSVC 기반으로 구성한다.
- [ ] 테스트 가능한 core와 UI shell을 분리한다.
- [ ] 벤치마크는 수동 감상이 아니라 실행 가능한 CLI 또는 테스트 하네스로 만든다.
- [ ] 외부 의존성은 성능, 안정성, 유지보수성이 검증된 경우에만 추가한다.
- [ ] 모든 source file naming은 kebab-case 또는 기존 Windows/C++ 관례와 충돌하지 않는 규칙으로 정한다.

---

## 3. Non-Goals

MVP에서 하지 않을 일은 명확히 제외한다. 제외 범위는 제품을 약하게 만들기 위한 것이 아니라 성능 코어를 먼저 증명하기 위한 것이다.

- [ ] 네트워크 드라이브 최적화
- [ ] SMB/NAS 특화 처리
- [ ] OneDrive, Google Drive, Dropbox 같은 클라우드 provider 직접 통합
- [ ] FTP/SFTP/WebDAV 클라이언트
- [ ] 압축 파일을 폴더처럼 탐색하는 기능
- [ ] 플러그인 시스템
- [ ] 스크립팅/매크로 시스템
- [ ] 전체 텍스트 검색 또는 인덱서
- [ ] 중복 파일 찾기
- [ ] 디렉터리 동기화
- [ ] 고급 일괄 이름 변경
- [ ] 이미지/문서 전체 미리보기 패널
- [ ] 썸네일 갤러리 뷰
- [ ] MTP/휴대폰 장치 탐색
- [ ] 레지스트리/제어판/가상 Shell namespace 전체 지원
- [ ] Windows 파일 탐색기의 완전 대체 등록
- [ ] 관리자 권한 상승 파일 작업 자동화
- [ ] 상용 배포/코드 서명/업데이터

---

## 4. Scope

### 4.1 In Scope

#### 4.1.1 App Shell

- Native Windows desktop executable
- Main window
- Toolbar or compact command surface
- Address/path input
- Status area
- One or more file panes
- Layout mode foundation: single, dual, quad 후보
- Session state foundation: last path, layout, window size

#### 4.1.2 File Navigation

- Open local folder path
- Navigate into folder
- Navigate up
- Refresh current folder
- Back/forward history per pane
- Drive/root access foundation
- Path validation
- Long path handling strategy
- Unicode path support

#### 4.1.3 File List

- Details/list view first
- Virtualized rows
- File name, extension, size, modified time, attributes
- Directory/file visual distinction
- Incremental population
- Stable selection model
- Keyboard navigation
- Mouse selection
- Multi-select foundation
- Sorting by name, type, size, modified time
- Loading indicator that does not block interaction

#### 4.1.4 Metadata Loading

- Basic file attributes during enumeration
- Icons via background worker
- Shell overlays only if they do not block primary list
- Folder size not calculated automatically by default
- Thumbnail generation excluded from initial default path
- Metadata cache invalidation strategy to be designed

#### 4.1.5 Basic File Actions

MVP의 파일 작업은 데이터 안전을 최우선으로 하며, 고성능 복사 엔진을 처음부터 만들지 않는다.

- Open file with default application
- Open folder in current pane
- Rename single item
- Create folder
- Delete to Recycle Bin
- Copy/cut/paste foundation
- Drag-and-drop foundation only if it does not destabilize MVP

`IFileOperation` 사용 여부는 Design 단계에서 결정한다. Windows Shell 동작과 안전한 오류 처리가 중요하므로 직접 `DeleteFile`/`MoveFile`만으로 모든 작업을 처리하지 않는다.

#### 4.1.6 Benchmark Harness

- Synthetic dataset generator
- Directory enumeration benchmark
- First visible rows timing
- Full enumeration timing
- Sort timing
- Memory usage snapshot
- UI responsiveness probe
- Benchmark output file
- Comparison notes against Windows File Explorer and selected third-party tools

#### 4.1.7 Diagnostics

- Local-only diagnostic logging
- Performance event markers
- Slow operation warnings in debug mode
- Crash dump or structured failure capture strategy
- No external telemetry in MVP

### 4.2 Out of Scope

Out-of-scope 항목은 Non-Goals와 동일하되, 특히 다음은 MVP에서 의도적으로 미룬다.

- 고급 파일 작업 큐
- 복사 속도 제한/일시정지/재개
- 폴더 동기화
- 파일 비교
- 아카이브 내부 탐색
- 원격 서버 탐색
- 사용자 플러그인
- 디자인 테마 시스템
- 다국어 UI 전체 지원
- Windows Store 패키징

### 4.3 Conditional Scope — Resolved (v1.0.1)

Design 단계에서 다음과 같이 확정되었다.

| Candidate | Resolution (Design v1.0.1) | Rationale |
|-----------|----------------------------|-----------|
| Quad layout | **Deferred (architecture-ready, not in MVP gate)** | `PaneManager` 구조는 확장 가능하지만 MVP gate는 single + dual만 포함 |
| Shell context menu | **Excluded from MVP** | UI thread block과 third-party shell extension reentrancy 리스크가 큼 |
| Drag-and-drop | **Excluded from MVP** | file operation 안정화 이후 OLE drop target/source 별도 설계 |
| File icons | **Included (background batch loading only)** | placeholder 우선 표시, 백그라운드 아이콘 적용. 파일명 표시를 지연시키지 않음 |
| Session restore | **Included (basic: last path, layout, window size)** | 구현 비용 낮음, settings.json 저장 |

---

## 5. Architecture Considerations

### 5.1 Technology Direction

기술 방향은 **C++ native Windows app**으로 잡는다.

| Layer | Planned Direction | Reason |
|-------|-------------------|--------|
| UI shell | C++ / Win32 중심 | UI thread, 메시지 루프, 가상화 리스트, Shell 통합을 직접 제어 |
| File list | Owner-data virtual list 또는 custom rendered list | 100k+ row 처리와 스크롤 성능 |
| Rendering | Win32 common controls 우선, 필요 시 Direct2D/DirectWrite | 초기 복잡도와 성능 제어의 균형 |
| Core engine | C++ native | UI와 언어 경계 제거, Windows API/COM 접근 단순화 |
| File operations | Shell API + Win32 API 조합 | 안전한 사용자 기대 동작과 저수준 성능 제어 |
| Benchmark | C++ CLI/test harness | 앱 외부에서도 반복 측정 가능 |

Rust core는 성능 측면에서 충분히 가능한 후보지만, MVP에서는 C++ UI와의 FFI 경계, 빌드 복잡도, 데이터 전달 비용을 줄이기 위해 C++ core를 우선한다. Rust는 별도 실험 브랜치나 향후 독립 모듈에서 재검토할 수 있다.

### 5.2 UI Architecture Principles

- UI thread는 윈도우 메시지 처리와 화면 갱신에 집중한다.
- 디렉터리 열거, 정렬, 아이콘 로딩, 파일 작업은 UI thread에서 수행하지 않는다.
- UI는 전체 파일 목록 객체를 소유하지 않고, 표시 가능한 slice를 빠르게 요청한다.
- 가상 리스트는 row 수와 row 데이터 요청을 분리한다.
- 빠른 placeholder 표시를 허용한다.
- 모든 백그라운드 결과에는 pane id와 generation id를 붙인다.
- 오래된 generation 결과는 폐기한다.

### 5.3 Core Architecture Principles

- Directory enumeration은 streaming/batched result로 UI에 전달한다.
- FileEntry는 표시와 정렬에 필요한 최소 정보로 시작한다.
- 느린 metadata는 별도 상태로 둔다.
- 정렬은 데이터 크기에 따라 즉시/백그라운드 전략을 나눈다.
- 캐시는 명시적 수명과 메모리 한도를 가진다.
- 취소 토큰은 모든 장기 작업에 전달된다.
- 오류는 예외로만 흘리지 않고 typed result로 표현한다.

### 5.4 Candidate Components

| Component | Responsibility |
|-----------|----------------|
| `AppHost` | 프로세스 초기화, COM 초기화, main message loop |
| `MainWindow` | 메인 윈도우, 메뉴/툴바/상태바 |
| `PaneManager` | 단일/듀얼/쿼드 패널 생성과 포커스 관리 |
| `FilePane` | 하나의 폴더 탐색 컨텍스트 |
| `PathBar` | 경로 입력, breadcrumb 후보 |
| `VirtualFileList` | 가상화 파일 목록 UI |
| `DirectoryEnumerator` | Windows 파일 열거 |
| `FileModelStore` | FileEntry storage, sorting, filtering |
| `IconProvider` | 비동기 아이콘 로딩과 캐시 |
| `OperationService` | rename/create/delete/copy foundation |
| `TaskScheduler` | priority-aware background work |
| `CancellationRegistry` | pane/generation별 취소 관리 |
| `PerfTracker` | 성능 이벤트 기록 |
| `BenchmarkRunner` | 재현 가능한 성능 측정 |
| `ErrorPresenter` | 사용자 오류 메시지와 복구 행동 |

이 컴포넌트 이름은 계획용 후보이며, 실제 파일/클래스 이름은 Design 단계에서 확정한다.

### 5.5 Windows API Candidates

Design 단계에서 아래 API를 검토한다.

| Area | Candidate API |
|------|---------------|
| Directory enumeration | `FindFirstFileExW`, `FindNextFileW`, `FindClose` |
| Path handling | `PathCch*`, long path `\\?\` strategy |
| File attributes | `WIN32_FIND_DATAW`, `GetFileInformationByHandleEx` |
| File operations | `IFileOperation`, `SHCreateItemFromParsingName`, `IShellItem` |
| Recycle Bin delete | Shell operation path |
| File open | `ShellExecuteExW` |
| Icons | `SHGetFileInfoW`, image lists, async extraction strategy |
| Context menu | `IContextMenu` family, conditional |
| Drag and drop | OLE drag/drop interfaces |
| UI list | Win32 List-View with `LVS_OWNERDATA` or custom |
| Rendering | GDI initially, Direct2D/DirectWrite if custom list |
| Threading | `std::jthread`, Windows thread pool, custom priority queues |

### 5.6 Data Model Draft

Initial `FileEntry` concept:

```text
FileEntry
- stable_id or index generation
- parent pane generation
- name
- extension range or cached extension
- attributes
- size
- modified time
- created time optional
- is_directory
- is_hidden
- is_system
- icon_state
- metadata_state
- error_state optional
```

Design rule: avoid storing duplicate full paths for every entry if parent path + name is enough. Full path construction should be lazy and bounded.

### 5.7 Performance Budget Gates

No implementation phase should be considered done unless these gates exist.

- Benchmark command can generate datasets.
- Benchmark command can measure enumeration time.
- UI can report first visible rows timing.
- Debug build can detect UI thread stalls above threshold.
- Release build can run benchmark without debugger.
- Results can be saved to a local artifact file.
- At least Windows File Explorer baseline notes are captured manually or semi-manually.

---

## 6. Success Criteria

### 6.1 Product Success Criteria

- [ ] 사용자는 앱을 실행하자마자 파일 탐색 화면을 볼 수 있다.
- [ ] 사용자는 로컬 폴더 경로를 열 수 있다.
- [ ] 사용자는 폴더 안으로 들어가고, 상위 폴더로 올라가고, 새로고침할 수 있다.
- [ ] 사용자는 대용량 폴더 로딩 중에도 UI를 조작할 수 있다.
- [ ] 사용자는 파일명, 크기, 수정일, 유형/확장자를 확인할 수 있다.
- [ ] 사용자는 이름/크기/수정일 기준으로 정렬할 수 있다.
- [ ] 사용자는 단일/다중 선택을 할 수 있다.
- [ ] 사용자는 기본 앱으로 파일을 열 수 있다.
- [ ] 사용자는 최소한의 파일 작업을 안전하게 수행할 수 있다: 새 폴더, 이름 변경, 휴지통 삭제.

### 6.2 Performance Success Criteria

- [ ] 1,000개 이하 로컬 폴더에서 첫 목록 표시가 50 ms 목표에 근접한다.
- [ ] 10,000개 로컬 폴더에서 첫 목록 표시가 100 ms 목표에 근접한다.
- [ ] 100,000개 로컬 폴더에서 첫 목록 표시가 200 ms 목표에 근접한다.
- [ ] 100,000개 폴더에서도 UI thread 단일 차단이 50 ms를 넘지 않는다.
- [ ] 100,000개 폴더 스크롤에서 체감 끊김이 없고 p95 frame 기준을 측정할 수 있다.
- [ ] 아이콘 로딩을 꺼도 켜도 파일명 우선 표시가 유지된다.
- [ ] 폴더 전환을 빠르게 반복해도 이전 결과가 현재 pane에 섞이지 않는다.
- [ ] benchmark output으로 이전 commit 대비 성능 회귀를 확인할 수 있다.

### 6.3 Reliability Success Criteria

- [ ] 존재하지 않는 경로를 열면 앱이 크래시하지 않고 명확한 오류를 표시한다.
- [ ] 권한 없는 폴더를 열면 앱이 크래시하지 않고 명확한 오류를 표시한다.
- [ ] 로딩 중 폴더를 삭제하거나 이름 변경해도 앱이 크래시하지 않는다.
- [ ] 파일 작업 실패 시 실패 항목과 원인을 보여준다.
- [ ] 1시간 이상 폴더 전환/정렬/스크롤 반복 테스트에서 크래시가 없어야 한다.
- [ ] 대용량 폴더 반복 진입 후 메모리 사용량이 계속 증가하지 않아야 한다.

### 6.4 Documentation Success Criteria

- [ ] Plan 문서가 작성되고 bkit Plan phase가 완료된다.
- [ ] Design 문서가 Plan의 결정 사항을 구체 아키텍처로 변환한다.
- [ ] Implementation 전 `bkit_pre_write_check`를 모든 source file에 대해 수행한다.
- [ ] Significant code change 후 `bkit_post_write`를 수행한다.
- [ ] Check 단계에서 design vs implementation gap analysis를 수행한다.

---

## 7. Schedule

아래 일정은 2026-05-14 기준이며, v1.0.1에서 **성능 게이트가 milestone마다 분산 측정**되도록 수정됐다. 기존 Plan은 모든 성능 게이트를 마지막 Performance Check에 몰아놓아 아키텍처 회귀 발견이 너무 늦었다.

| Phase | Target Date | Status | Deliverable | Performance Gate (v1.0.1) |
|-------|-------------|--------|-------------|---------------------------|
| Plan | 2026-05-14 | Completed | `fast-explorer-core.plan.md` | — |
| Plan Review | 2026-05-14 | Completed | 사용자 확인 및 수정 | — |
| Design | 2026-05-14 ~ 2026-05-15 | Completed | architecture/design document | — |
| Prototype Scaffold (M1) | 2026-05-15 | Pending | C++ native app skeleton | warm launch ≤ 500 ms 첫 측정 |
| Benchmark Harness + Core Enumeration (M2) | 2026-05-14 ~ 2026-05-15 | **Completed** | dataset + CLI + DirectoryEnumerator + head-to-head | small 0.176 ms / medium 5.03 ms / 100k 43.8 ms·6.43 MB. N1 해소 (FindFirstFileExW 유지). Design §14.2 측정값. |
| Virtual File List (M3) | 2026-05-15 | **Completed** | responsive list UI + format LRU + DPI scale + stall probe | small 4.05 ms / medium 3.62 ms / 100k 29.83 ms first-batch. UI stall 0. Design §14.3 측정값. LVN_GETDISPINFO p99 → M7. |
| Navigation + Cancellation (M4) | 2026-05-15 | **Completed** | address bar + history + L1/L2 generation + FsWatcher + coalesce | generation token 양층 검증 (244/244 tests). 100k rapid-switch soak + cancellation latency 정량은 M7로 defer (UI automation 필요). Design §14.4 측정값. |
| Sorting + Selection (M5) | 2026-05-15 (compressed) | **Completed** | 4-key sort + tiebreak + visibleOrder + publishedCount race fix + 2k threshold worker + raw-index stable selection + LVN_ITEMCHANGED routing | medium(10k) Name asc sort **2.75 ms** (50 ms budget의 5.5%). 298/298 tests. 100k UI-stall histogram + sort accept latency 정량은 M7로 defer. Design §14.5 측정값. |
| Icons + Basic Operations (M6) | 2026-05-16 (compressed) | **Completed (icons + file ops) / Partial** | IconCache + ExtensionIconCache LRU + IconProvider STA worker + ShellExecuteExW open + ShellWorker STA + IFileOperation rename/createFolder/delete + ComScope RAII | icon delay 및 OneDrive hydration은 SHGFI_USEFILEATTRIBUTES로 by-construction 만족 (≤ 20% / 0건). ImageList cap 258 KB ≪ 3 MB budget. OperationResult 구조화 / IFileOperationProgressSink / low-memory shrink / portable crash dump 는 M7로 defer. 345/345 unit tests. Design §14.6 측정값. |
| Benchmark + Stabilization (M7) | 2026-05-22 ~ 2026-05-24 | Pending | full bench, soak test | **large folder first row ≤ 200 ms, UI stall ≤ 50 ms, scroll p95 ≤ 16.7 ms, 100k ≤ 100 MB** 종합 검증 |
| PDCA Check/Report | 2026-05-24 ~ 2026-05-25 | Pending | gap analysis and report | — |

**Rule**: 각 milestone exit criteria는 해당 단계 성능 게이트 측정값 기록을 포함한다. 기준 미달이 발견되면 다음 milestone로 진행하기 전에 architecture 재검토를 한다.

---

## 8. Risks & Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Win32/C++ 개발 복잡도가 높아 초기 속도가 느려짐 | High | High | MVP 기능 범위를 강하게 제한하고 benchmark/list/navigation부터 구현 |
| custom list를 직접 만들면 접근성/키보드/선택 처리가 복잡해짐 | High | Medium | 먼저 `LVS_OWNERDATA` virtual list 검증 후 한계가 확인될 때 custom render로 이동 |
| Shell icon/context menu 호출이 UI를 멈춤 | High | High | UI thread에서 호출 금지, background worker와 timeout, context menu는 conditional scope |
| 대용량 폴더 정렬이 UI를 막음 | High | Medium | sort worker, incremental feedback, cancelable sort, stable generation check |
| 아이콘/메타데이터 캐시가 메모리를 과하게 사용 | Medium | Medium | bounded cache, LRU, icon quality 단계화 |
| Windows 파일 작업에서 데이터 손상 위험 | Critical | Low | destructive operation 직접 구현 최소화, Recycle Bin 우선, Shell API 검토, confirm/error model |
| 긴 경로/Unicode/특수문자 처리 누락 | High | Medium | 초기 benchmark dataset에 포함, wide-char only path policy |
| 권한 없는 폴더/잠긴 파일에서 예외 처리 누락 | High | Medium | explicit error result, targeted tests |
| 백신/인덱서/동기화 클라이언트가 benchmark 결과를 흔듦 | Medium | High | 반복 측정, median/p95 기록, 환경 기록 |
| 경쟁 제품 비교가 주관적으로 흐름 | Medium | Medium | 동일 dataset과 동일 작업 시나리오로 측정 |
| Windows 10/11 차이로 UI/API 동작이 달라짐 | Medium | Medium | Windows 11 우선, Windows 10은 Design에서 지원 여부 결정 |
| bkit project level이 Starter로 감지되어 desktop/native 현실과 맞지 않음 | Low | High | 문서에 native desktop 특성을 별도로 명시하고 PDCA 흐름은 Starter level 템플릿만 사용 |
| 너무 빠르게 기능을 늘려 성능 정체성이 흐려짐 | High | High | Non-Goals를 엄격히 유지, 성능 budget gate 없이는 기능 추가 금지 |

---

## 9. Competitive Analysis Plan

### 9.1 Products To Compare

1. Windows File Explorer
2. Q-Dir
3. Total Commander
4. XYplorer
5. Directory Opus
6. Files
7. OneCommander

### 9.2 Comparison Dimensions

| Dimension | Why It Matters |
|-----------|----------------|
| First folder display | 사용자가 가장 먼저 느끼는 속도 |
| Full enumeration | 전체 로딩 완료 속도 |
| Large folder scroll | 실제 대용량 폴더 사용성 |
| Sort responsiveness | 작업 중 멈춤 여부 |
| Memory footprint | 장시간 사용 안정성 |
| Multi-pane cost | 2~4개 패널 사용 시 성능 유지 |
| Error behavior | 실사용 신뢰도 |
| Shell compatibility | Windows 대체재로서 필수 |

### 9.3 Benchmark Rules

- 동일한 machine, 동일한 dataset, 동일한 power mode에서 측정한다.
- 첫 실행과 warm 실행을 구분한다.
- Windows Defender/인덱서/클라우드 sync 상태를 기록한다.
- 측정 불가능한 제품은 수동 관찰과 screen recording으로 보조한다.
- 우리 앱은 내부 `PerfTracker` 결과와 외부 관찰 결과를 함께 기록한다.

---

## 10. Quality Strategy

### 10.1 Test Layers

| Layer | Test Type |
|-------|-----------|
| Core path utilities | unit tests |
| Directory enumeration | deterministic integration tests |
| Sorting/filtering | unit + benchmark tests |
| File model store | unit tests |
| UI responsiveness | instrumentation + manual verification |
| File operations | sandbox folder integration tests |
| Long path/Unicode | generated dataset tests |
| Error handling | permission/missing/deleted path scenarios |

### 10.2 Manual QA Scenarios

- 앱 실행 후 기본 경로 표시
- `C:\Users\<user>\Downloads` 열기
- 10,000개 파일 폴더 열기
- 100,000개 파일 폴더 열기
- 로딩 중 다른 폴더로 이동
- 로딩 중 창 크기 변경
- 로딩 중 정렬 명령
- 파일 선택 후 rename
- 새 폴더 생성
- 휴지통 삭제
- 권한 없는 폴더 열기
- 긴 경로 폴더 열기
- Unicode 파일명 폴더 열기
- 다중 패널에서 서로 다른 폴더 동시 로딩

### 10.3 Performance QA Scenarios

- Explorer baseline run
- Fast Explorer cold start
- Fast Explorer warm start
- Small/medium/large folder first visible rows
- Full enumeration complete
- Scroll p95 frame time
- Sort by name/size/date
- Repeated folder switch 100회
- Memory after large folder open/close 20회
- Icon loading enabled/disabled comparison

---

## 11. Security And Safety

MVP는 로컬 파일을 다루므로 데이터 안전이 중요하다.

- 삭제는 기본적으로 Recycle Bin을 사용한다.
- 영구 삭제는 MVP 기본 기능에서 제외한다.
- 관리자 권한 상승 자동 처리는 제외한다.
- 파일 작업 전 대상 경로와 작업 종류를 명확히 구성한다.
- path traversal 개념은 로컬 앱에서도 내부 명령 모델에서 방지한다.
- 외부 telemetry를 보내지 않는다.
- 로그에 민감한 전체 경로를 남기는 정책은 Design 단계에서 결정한다.
- crash dump가 개인 파일 경로를 포함할 수 있으므로 수집/저장 정책을 명시한다.
- shell extension은 신뢰 경계 밖 코드로 보고 UI thread 차단과 crash 영향 범위를 최소화한다.

---

## 12. Open Questions — Resolved (v1.0.1)

Design v1.0.1 시점 결정 상태.

| # | Question | Status | Resolution |
|---|----------|:------:|------------|
| 1 | List-View `LVS_OWNERDATA` vs custom Direct2D list | ✅ | `LVS_OWNERDATA` 우선. Direct2D는 측정된 한계 이후로 deferred. |
| 2 | Quad vs dual layout | ✅ | single + dual MVP. Quad는 architecture-ready로만 둠. |
| 3 | Windows 10 vs 11 only | ✅ | Windows 11 x64 first, Windows 10 best-effort. |
| 4 | Icon API + cache strategy | ✅ | `SHGetFileInfoW` (deferred: `IShellItemImageFactory` for HiDPI) + extension-level cache 우선, LRU bounded. |
| 5 | Shell context menu | ✅ | MVP 제외. |
| 6 | `IFileOperation` vs Win32 API | ✅ | rename/delete는 `IFileOperation` 중심, create folder는 `CreateDirectoryW` (확실히 빠르고 안전), Shell COM 실패 시에만 Win32 fallback. |
| 7 | Benchmark harness 위치 | ✅ | 별도 CLI + 앱 instrumentation 둘 다 둠. |
| 8 | Build system | ✅ | CMake + MSVC generator. |
| 9 | Test framework | ✅ | MVP는 dependency-free `core-tests.exe` (self-contained assert macro). Catch2/doctest는 Milestone 7 이후 재검토. |
| 10 | Native controls vs custom chrome | ✅ | Native controls 우선. Custom chrome은 deferred. |
| 11 | Settings storage | ✅ | `%LOCALAPPDATA%\FastExplorer\settings.json`. Portable 모드는 Q12 참고. |
| 12 | Portable mode | ⚠ | MVP 미포함. Settings 경로를 환경변수 `FAST_EXPLORER_PORTABLE_ROOT` override 가능하게 두어 향후 portable 모드를 막지 않음 (Design §2.1 패치 참고). |

### 12.1 New Open Questions (Teammate Review 결과)

| # | New Question | Owner | Resolution Target |
|---|--------------|-------|-------------------|
| N1 | `FindFirstFileExW` vs `GetFileInformationByHandleEx(FileIdBothDirectoryInfo)` 어느 쪽이 large-flat 200 ms 게이트에 유리한가? | M2 | **해소 (2026-05-15)**: FindFirstFileExW 유지. large-flat 100k에서 head-to-head 측정 결과 FindFirstFileExW + LARGE_FETCH median 37.9 ms vs GFIBHE median 60.7 ms (find 60% 빠름). Design §14.2 측정값 참조. |
| N2 | Crash dump를 `MiniDumpWithoutOptionalData` 수준으로 채택할지 WER 위임할지 | M6 | M6 milestone 진입 시점 |
| N3 | UI 자동화 (FlaUI vs WinAppDriver) 도입 시점 | M7 | M7 stabilization 단계 |
| N4 | Benchmark용 RAM disk(ImDisk) 강제 사용 vs SSD 측정 허용 | M7 | M7 exit 전 결정 |

---

## 13. Deliverables

### 13.1 Plan Phase Deliverables

- [x] Feature name defined: `fast-explorer-core`
- [x] Product direction defined
- [x] Initial technology direction defined
- [x] Scope and non-goals defined
- [x] Performance success criteria defined
- [x] Risk register defined
- [x] References listed
- [ ] User review completed

### 13.2 Design Phase Deliverables

- [ ] Architecture diagram
- [ ] Threading model
- [ ] UI message/data flow
- [ ] Directory enumeration design
- [ ] Virtual list design
- [ ] File operation design
- [ ] Error model
- [ ] Benchmark design
- [ ] Test plan
- [ ] Implementation order

### 13.3 Implementation Phase Deliverables

- [ ] Native app scaffold
- [ ] Main window
- [ ] File pane
- [ ] Directory enumeration core
- [ ] Virtual file list
- [ ] Sorting
- [ ] Basic navigation
- [ ] Basic operations
- [ ] Benchmark harness
- [ ] Performance report

---

## 14. References

### 14.1 Competitor References

- Q-Dir official info: https://www.q-dir.com/?seite=en-us%2FInfo
- Directory Opus official site: https://www.gpsoft.com.au/
- Total Commander feature list: https://www.ghisler.com/featurel.htm
- XYplorer product page: https://www.xyplorer.com/product.php?free=1
- XYplorer feature list: https://www.xyplorer.com/features.php
- Files official site: https://files.community/
- OneCommander official site: https://onecommander.com/v3.html

### 14.2 Windows Technical References

- Win32 API reference: https://learn.microsoft.com/en-us/windows/win32/api/
- WinUI 3 reference: https://learn.microsoft.com/en-gb/windows/apps/winui/
- File search APIs: https://learn.microsoft.com/en-us/windows/win32/fileio/searching-for-one-or-more-files
- `FindFirstFileW`: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew
- Win32 List-View controls: https://learn.microsoft.com/en-us/windows/win32/controls/list-view-controls-overview
- `IFileOperation`: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifileoperation
- `IContextMenu` guidance: https://learn.microsoft.com/en-us/windows/win32/shell/how-to-implement-the-icontextmenu-interface

---

## 15. Next Action

Design 완료. 다음은 Do phase 진입.

Do 진입 전 Design v1.0.1 보완 (§16 Locked Decisions 참고).

---

## 16. Locked Decisions — Plan ↔ Design Sync (v1.0.1)

Design 단계에서 새로 확정되어 Plan에도 영향을 미치는 의사결정.

### 16.1 기술 스택 Lock

| Item | Locked Value |
|------|--------------|
| Language | C++20 (C++23 미사용) |
| Compiler | MSVC v143 (Visual Studio 2022 17.x) |
| Windows SDK | 10.0.22621.0 (Windows 11 SDK) 이상 |
| Build | CMake 3.24+ + Ninja or MSVC generator |
| CRT linkage | `/MD` (shared CRT) + VC++ Redistributable 동봉 (or `/MT` 정적 — Design §2.1 참조) |
| Target OS | Windows 11 x64 first, Windows 10 1809+ best-effort |
| Charset | Wide-character only (UTF-16) |

### 16.2 응답성 / Threading Lock

- UI thread는 STA (`COINIT_APARTMENTTHREADED`)로 초기화한다.
- Shell worker thread는 STA, 자체 PeekMessage 루프를 가진다.
- Core worker pool은 MTA. Shell COM API 호출 금지.
- 모든 작업에 `(paneId, generation)` + `std::stop_token` 부착.
- IFileOperation은 `IFileOperationProgressSink` 기반 콜백 수집, owner HWND는 메인 윈도우 사용.

### 16.3 Cancellation Lock (3계층)

| Layer | Mechanism | Latency Target |
|-------|-----------|----------------|
| Layer 1 (UI ignore) | generation token mismatch → 결과 폐기 | ≤ 50 ms |
| Layer 2 (worker abort) | `std::stop_source` per pane/generation, batch boundary check | best-effort (single batch 내) |
| Layer 3 (shell op abort) | `IFileOperationProgressSink::Pre*` → `S_FALSE` | best-effort |

SHGetFileInfo는 cancel 불가하므로 fire-and-forget + 결과 폐기 패턴 사용.

### 16.4 File System Edge Case Lock

| Edge Case | Decision |
|-----------|----------|
| Long path (> MAX_PATH) | app manifest `longPathAware=true` opt-in + 내부 경로 `\\?\` 정규화 |
| UNC path (`\\server\share`) | MVP는 로컬 드라이브 letter만 허용. UNC 입력은 명시적 거부 + 안내 |
| Reparse point / junction / symlink | enumeration 시 표시 마커 (attribute 컬럼에 `J/L` 문자), recursive follow 금지 |
| OneDrive / cloud placeholder | `FILE_ATTRIBUTE_RECALL_ON_*` 비트 감지하여 hydration trigger 호출 회피. `SHGetFileInfo SHGFI_USEFILEATTRIBUTES` 경로 우선. |
| Long file name (UTF-16 surrogate pair, RTL) | enumeration/sort 모두 wide-char ordinal로 처리 |

### 16.5 Observability Lock

- 측정 백엔드: `QueryPerformanceCounter` (1차), ETW custom provider (stretch goal).
- 로깅 백엔드: 자체 ringbuffer + 비동기 file writer (MVP는 spdlog 도입 안 함).
- Crash handler: `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` (MiniDumpWithoutOptionalData 수준, 사용자 동의 시만 디스크 저장).
- Stall probe: 50 ms 초과 시 debug log + active command 캡처.
- CI 회귀 게이트: bench-results JSON baseline 비교 (p95 frame +20 %, first-visible +15 % 시 경고).

### 16.6 Out of MVP — Explicit (HiDPI/DPI v2 예외)

다음은 MVP에 포함된다 (사후 도입 비용이 크기 때문):
- Per-monitor DPI v2 (`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`)
- common controls v6 manifest dependency (themed List-View 필수)
- Crash dump 핸들러
- 자체 로깅 ringbuffer

다음은 MVP 제외, deferred:
- Dark mode (`SetWindowTheme(L"DarkMode_Explorer", ...)`)
- HiDPI 아이콘 (`IShellItemImageFactory::GetImage` 256x256)
- Accessibility (UIA custom provider — List-View 기본 MSAA로 커버)
- IME 커스텀 처리 (기본 EDIT 컨트롤로 커버)
- 다국어 UI strings

### 16.7 File System Watch (M4 결정)

`ReadDirectoryChangesW` 기반 변경 감지는 **MVP 포함**으로 결정. 없으면 rename/create 후 수동 refresh 강제 → MVP scope의 "user expects native explorer parity"를 깸. 단 구현은 M4(navigation/cancellation)에서 최소 기능(파일 추가/삭제/이름변경 알림)만 포함하고, sub-tree recursive watch는 deferred.

### 16.8 Memory Lock (Design v1.0.2)

100 MB budget 대비 **2× 마진 (~50 MB target)** 으로 강화. 핵심 lock:

| Item | Lock |
|------|------|
| `FileEntry` sizeof | **40 B** (was 64, was 128). `static_assert` 강제 |
| Per-pane 100k 메모리 | ≤ 10 MB (entries 4 + arena 4.8 + visibleOrder 0.4 + 기타) |
| Process working set @ 100k pane | target ≤ 50 MB, budget ≤ 100 MB |
| Startup working set (no pane) | ≤ 25 MB |
| ImageList cap | ≤ 3 MB (process-global, 700 icon × 32×32 BGRA) |
| Name arena backing | per-pane `VirtualAlloc` 16 MB reserve, 64 KB chunk commit, generation reset 시 decommit |
| OS hint | `SetProcessWorkingSetSizeEx` startup, `EmptyWorkingSet` on minimize/drop, `CreateMemoryResourceNotification(LowMemoryResourceNotification)` 등록 후 caches drop |
| STL exclusions | `std::filesystem`, `std::regex`, `std::iostream` 사용 금지 |
| CRT options | `/MD /GL /LTCG /Gw /Gy /OPT:REF /OPT:ICF` Release. `/GR-` M2 결정 |
| Memory soak | 100k→0→100k × 10 누적 Δ ≤ 5 MB. Dual nav × 50 누적 Δ ≤ 10 MB |
| Bench gate (M7) | `process.peak_workingset @ 100k` 측정값 baseline 등록 + 회귀 임계 +10 MB |

Design §5.3 (Memory Optimization Strategy), §5.3.1~7 참고.
