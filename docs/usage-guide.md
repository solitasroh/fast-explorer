# Fast Explorer 사용 가이드

> 대상: 사용자 + 개발자
> 갱신 기준: v0.8.1 후보 작업 트리, 기준 릴리즈 v0.8.0, CMake 프로젝트 버전 0.8.0

Fast Explorer는 Windows 네이티브 C++20 파일 탐색기입니다. 큰 폴더를 빠르게 열고, 좌측 Navigation Tree, 탭/멀티 패널/필터/정렬/분류/파일 작업을 한 화면에서 다루는 데 초점을 둡니다.

---

## 1. 실행

빌드 산출물은 보통 `build\FastExplorer.exe`에 있습니다.

```powershell
& build\FastExplorer.exe
```

주소창에 절대 경로, UNC 경로, 또는 지원되는 Shell 위치를 입력해 탐색합니다.

지원 예:

| 입력 | 의미 |
|------|------|
| `C:\Users\me\Downloads` | 일반 파일시스템 경로 |
| `\\server` | 서버의 공유 목록 열거 |
| `\\server\share` | UNC 공유 탐색 |
| `shell:ThisPC` | 내 PC / 드라이브 루트 목록 |
| `shell:Network` | 네트워크 위치, mapped drive, Network Shortcuts |
| `shell:NetHood` | Windows Network Shortcuts 폴더 |
| `shell:RecycleBinFolder`, `휴지통` | 휴지통 |
| `갤러리` | Windows 11 갤러리 Shell namespace |
| `홈` | Windows 11 파일 탐색기 홈 Shell namespace |
| `shell:AppsFolder`, `::{CLSID}` | Windows가 파싱할 수 있는 일반 Shell namespace |

존재하지 않는 경로를 주소창에 입력하면 기존 목록, 선택, 히스토리는 보존되고 상태 표시줄에 오류만 표시됩니다.

---

## 2. 주요 기능

### Navigation Tree

- 창 좌측의 전역 Navigation Tree는 현재 활성 패널의 활성 탭 위치를 따라갑니다.
- Tree node를 선택하면 현재 활성 패널의 활성 탭이 해당 위치로 이동합니다.
- `Ctrl+B` 또는 햄버거 메뉴의 `Navigation tree` 항목으로 표시/숨김을 전환합니다.
- 좌측 splitter로 폭을 조절할 수 있으며, 표시 여부와 폭은 session restore에 저장됩니다.
- Home, Gallery, Desktop, Downloads, Documents, Pictures, Music, Videos, 사용자 프로필, This PC, Network, Network Shortcuts, Recycle Bin 등 Windows가 resolve 가능한 Shell 위치를 표시합니다.
- v0.8.1에서는 탐색 전용입니다. Tree에서 drag/drop, 이름 변경, 삭제, Shell context menu, favorites/pins는 지원하지 않습니다.

### 탭

- 각 패널은 자체 탭 목록을 가집니다.
- 탭은 닫기, 다른 탭 닫기, 오른쪽 탭 닫기, 재정렬을 지원합니다.
- 폴더 row에서 중간 클릭하면 해당 폴더를 같은 패널의 새 백그라운드 탭으로 엽니다.
- 탭과 활성 탭은 session restore에 저장됩니다.

### 멀티 패널

- 단일, dual, tri, quad 레이아웃을 지원합니다.
- 패널 splitter 비율은 저장/복원됩니다.
- 활성 패널만 상태바를 갱신하며, 비활성 패널은 시각적으로 구분됩니다.

### 필터 검색

- `Ctrl+F`로 필터 popup을 엽니다.
- plain substring, wildcard(`*`, `?`), regex(`r:` prefix)를 지원합니다.
- 필터 결과는 선택, 우클릭, 더블클릭, 삭제, 복사/잘라내기, drag/drop, 중간 클릭 새 탭 열기 중에도 유지됩니다.
- ESC 또는 명시적 clear 동작에서만 필터가 해제됩니다.
- 필터가 활성화된 상태의 status text, selection summary, context menu, clipboard, drag/drop, open item은 화면에 보이는 row 기준으로 동작합니다.

성능:

- 필터 입력은 UI timer로 debounce됩니다.
- plain 필터는 매 row마다 전체 이름 lowercase 복사를 만들지 않고 on-the-fly 비교합니다.
- plain 검색을 더 좁히는 입력은 이미 표시된 subset만 다시 훑어 비용을 줄입니다.

### 정렬과 분류

- 컬럼 헤더로 이름, 크기, 유형, 수정 시각 정렬을 토글합니다.
- 큰 폴더의 정렬은 백그라운드 worker로 분리됩니다.
- 분류는 이름, 수정한 날짜, 유형 기준을 지원합니다.
- 정렬/분류/필터 중에도 선택은 raw entry index 기준으로 추적됩니다.

### 파일 작업

일반 파일시스템 위치에서는 다음 작업을 지원합니다.

- Enter / 더블클릭: 폴더 진입 또는 OS 기본 앱으로 파일 열기
- Delete: 휴지통으로 이동
- F2: 이름 바꾸기
- Ctrl+Shift+N: 새 폴더 만들기 후 즉시 이름 편집
- Ctrl+C / Ctrl+X / Ctrl+V: Shell clipboard 기반 복사, 잘라내기, 붙여넣기
- 우클릭: Windows Shell context menu
- drag/drop: Shell data object 기반 drag/drop

Shell 가상 위치(`shell:ThisPC`, `shell:Network`, `shell:NetHood`, 휴지통, 갤러리, 홈, `shell:*`, `::{CLSID}`)에서는 안전을 위해 직접 삭제/이름변경/새 폴더/클립보드/drag 시작은 거부됩니다. row를 열어 실제 드라이브, UNC, 또는 폴더 대상에 들어가면 기존 파일 작업이 다시 활성화됩니다. 가상 위치 row의 중간 클릭과 "새 탭에서 열기"는 실제 대상 경로나 다음 Shell namespace로 동작합니다.

### 네트워크 위치

- `\\server`는 `WNetEnumResource` 경로로 공유 목록을 열거합니다.
- `\\server\share`와 하위 UNC 경로는 일반 폴더처럼 탐색합니다.
- mapped drive는 `shell:ThisPC`와 `shell:Network`에서 드라이브 row로 표시될 수 있습니다.
- Windows의 "네트워크 위치 추가" 항목은 `FOLDERID_NetHood` / Network Shortcuts를 조사하고, 가능한 경우 내부 `.lnk` target을 해석해 실제 대상 경로로 연결합니다.
- Shell link가 PIDL-only target이거나 Windows가 파일시스템/UNC target을 제공하지 않는 경우에는 제한될 수 있습니다.

### Session Restore

종료 시 다음 상태가 저장됩니다.

- 창 위치와 크기
- 패널 수, 활성 패널, 레이아웃 preset, splitter 비율
- 각 패널의 탭 목록과 활성 탭
- 일반 경로와 Shell location 문자열(`shell:*`, `::{CLSID}` 포함)
- Navigation Tree 표시 여부와 폭
- 숨김 파일 표시, 확장자 표시, 테마 override

복원 시 경로가 사라졌거나 접근할 수 없으면 홈 폴더로 fallback합니다. Shell location은 `location` 필드로 복원되며, 이전 설정 파일의 `path`만 있는 탭도 계속 읽을 수 있습니다.

### 테마

- 시스템 테마를 따르며, 수동 toggle을 지원합니다.
- dark mode는 title bar, toolbar, tab strip, list view, scrollbar, header까지 재적용됩니다.

---

## 3. 단축키

| 단축키 | 동작 |
|--------|------|
| `Ctrl+L` | 주소창 포커스 |
| `Ctrl+B` | 좌측 Navigation Tree 표시/숨김 |
| `Enter` in address bar | 입력 위치로 이동 |
| `Alt+Left` | 뒤로 |
| `Alt+Right` | 앞으로 |
| `Alt+Up` | 상위 폴더 |
| `F5` | 새로고침 |
| `Ctrl+F` | 필터 popup |
| `Esc` in filter popup | 필터 해제 |
| `F2` | 이름 바꾸기 |
| `Delete` | 휴지통으로 이동 |
| `Ctrl+Shift+N` | 새 폴더 |
| `Ctrl+C` | 복사 |
| `Ctrl+X` | 잘라내기 |
| `Ctrl+V` | 붙여넣기 |
| `Ctrl+A` | 전체 선택 |
| `Ctrl+W` | 현재 탭 닫기 |
| `Ctrl+Tab` | 다음 탭 |
| `Ctrl+Shift+Tab` | 이전 탭 |
| `Ctrl+1` | 단일 패널 |
| `Ctrl+2` | dual 패널 |
| `Ctrl+3` | tri 패널 |
| `Ctrl+4` | quad 패널 |
| `Ctrl+H` | dual 방향 전환 |
| `Ctrl+Shift+D` | light/dark theme toggle |

---

## 4. 개발 환경

### 요구사항

| 항목 | 내용 |
|------|------|
| OS | Windows 10/11 x64 |
| Toolchain | MSVC, Visual Studio 2022 이상 |
| Workload | Desktop development with C++ |
| SDK | Windows 10/11 SDK |
| Build | CMake 3.24 이상 |

PowerShell에서 그냥 `cmake --build build`를 실행하면 MSVC/SDK 환경 변수가 없어서 실패할 수 있습니다. 각 세션에서 `VsDevCmd.bat`로 개발자 환경을 먼저 잡습니다.

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake -B build -S . && cmake --build build'
```

설치 SKU에 따라 `Professional` 대신 `Community`, `Enterprise`, `BuildTools`를 사용합니다. 특정 SDK가 필요하면 다음처럼 지정합니다.

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -winsdk=10.0.22621.0 -no_logo && cmake --build build'
```

---

## 5. 테스트

권장 검증 순서:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build'
ctest --test-dir build --output-on-failure
build\core-tests.exe
build\winui_lite_tests.exe
```

테스트 범위:

- path-utils, Win32 backend, Memory backend, DirectoryEnumerator
- FileModelStore, NameArena, sort, grouping, filter
- PaneController navigation, selection, Shell locations, missing path protection
- adapters for item source, context menu, clipboard, drag/drop
- settings migration and session restore schema
- winui_lite chrome/ports/layout tests
- bench CLI and JSON formatting

---

## 6. 벤치마크

데이터셋 생성:

```powershell
build\FastExplorerBench.exe generate --preset small --out %TEMP%\fe-small
build\FastExplorerBench.exe generate --preset medium --out %TEMP%\fe-medium
build\FastExplorerBench.exe generate --preset large-flat --out %TEMP%\fe-large
```

열거 측정:

```powershell
build\FastExplorerBench.exe enumerate --path %TEMP%\fe-medium --runs 5
build\FastExplorerBench.exe enumerate --path %TEMP%\fe-large --runs 5
```

Win32 API 비교:

```powershell
build\FastExplorerBench.exe head-to-head --path %TEMP%\fe-medium --runs 5
build\FastExplorerBench.exe head-to-head --path %TEMP%\fe-large --runs 5
```

JSON 출력은 CI 회귀 비교에 사용할 수 있습니다.

```powershell
cmd /c "build\FastExplorerBench.exe enumerate --path %TEMP%\fe-large --runs 5 --format json > current.json"
```

PowerShell의 `>`는 UTF-16으로 저장될 수 있으므로 JSON redirect는 `cmd /c` 사용을 권장합니다.

---

## 7. 문제 해결

| 증상 | 확인 사항 |
|------|-----------|
| `cmake` 빌드에서 MSVC를 찾지 못함 | `VsDevCmd.bat`로 개발자 환경에 들어갔는지 확인 |
| Windows SDK 관련 오류 | Visual Studio Installer에서 Windows SDK 설치 여부와 `-winsdk` 값 확인 |
| 주소창에 없는 경로 입력 후 목록이 비지 않아야 함 | 정상 동작입니다. 기존 목록을 보존하고 status/error만 갱신합니다 |
| `shell:Network`가 비어 있음 | mapped drive나 Network Shortcuts가 없으면 빈 목록일 수 있습니다 |
| Network Shortcut이 열리지 않음 | `.lnk`가 filesystem/UNC target을 제공하지 않는 PIDL-only Shell target일 수 있습니다 |
| 100k 벤치가 느림 | Defender, indexer, cloud sync, 네트워크 드라이브 여부를 확인하고 로컬 SSD 임시 폴더에서 재측정 |
| 필터 결과에서 파일 작업 대상이 어긋남 | `FileModelStore::rawIndexForVisibleRow()` 경로를 확인해야 합니다 |

---

## 8. 관련 문서

| 문서 | 위치 |
|------|------|
| Release workflow | `docs/RELEASING.md` |
| Multi-pane design | `docs/superpowers/specs/2026-05-20-multi-pane-splitter-design.md` |
| Grouping design | `docs/superpowers/specs/2026-05-22-file-grouping-design.md` |
| Tabs design | `docs/superpowers/specs/2026-05-28-tabs-design.md` |
| Refactor notes | `docs/refactor-completion.md` |
