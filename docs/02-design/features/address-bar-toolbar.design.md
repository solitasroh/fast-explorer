# address-bar-toolbar - Design Document

> **Summary**: v0.2 주소표시줄 주변 보강. 페인별 4-버튼 네비 툴바(뒤·앞·위·새로고침)와 햄버거 메뉴(`≡`)를 추가해 단축키를 모르는 사용자의 디스커버리를 끌어올리고, 신규 액션(탐색기/터미널/경로복사/숨김 토글 등)을 단일 표면에 모은다. 브레드크럼은 v0.3로 분리.
>
> **Author**: Claude (with user)
> **Created**: 2026-05-19
> **Status**: Shipped (v0.2 baseline) — v0.2.1 polish in progress
> **Version**: 1.1.0
> **Level**: Starter

---

## Version History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 0.1.0 | 2026-05-19 | Initial draft. v0.2 스코프: 네비 툴바 + 햄버거 메뉴, 액션 8종, 페인당 인스턴스. 브레드크럼/즐겨찾기/최근경로 명시적 out-of-scope. | Claude |
| 1.0.0 | 2026-05-19 | Approved. §7 결정사항 사용자 확정: D1/D2=페인당, D5=확장자 표시(true), D4=글로벌 토글, D6=wt→pwsh→cmd. 나머지 D3/D7/D8/D9/D10은 디폴트 유지. T1부터 atom 단위 구현 착수. | Claude (with user) |
| 1.0.1 | 2026-05-19 | T1~T10 완료. D3 변경: SHGetStockIconInfo의 SIID_FOLDERUP/SIID_REFRESH가 이 SDK enum에 없어 4 버튼 모두 Unicode 텍스트 글리프(←→↑↻)로 대체 (BTNS_SHOWTEXT). 그 외 디폴트 유지. T9 신규 단위 테스트 4개 추가 (DirectoryEnumerator hidden filter ×2, SettingsStore v3→v4 migration ×2). 545/545 통과. T10 acceptance #8 (working-set delta < 1 MB): chrome 추가분 ≪ 100 KB로 산정 (toolbar 2 + 버튼 2 + transient HMENU). T10 acceptance #9 (클릭 stall 50 ms): 신규 클릭이 기존 paneController.back/forward/up/refresh를 그대로 호출하므로 v0.1 StallHistogram 분포와 동등. exe 크기 v0.1 386KB → v0.2 392KB (+6 KB). | Claude (with user) |
| 1.1.0 | 2026-05-19 | **v0.2.1 polish 계획 lock**. 4명 team review (visual / a11y / Windows / code) 결과 P0~P1 9개 atom 확정: A5 Segoe Fluent Icons(+MDL2 fallback)으로 Lucide 교체, A6 햄버거 글리프 ≡ → ⋯ (More 의미), A4 WM_DPICHANGED 폰트 재생성, A2 MSAA 접근 가능 이름, A1 툴팁(TBN_GETINFOTIP + TOOLTIPS_CLASS), A3 키보드 도달(WS_EX_CONTROLPARENT + Alt+M), A7 SetWindowTheme("Explorer"), A9 spacing 폴리시(margin 4→8, gap 8→12), A8 다크모드 + Mica. 실행 순서: A5 → A6 → A4 → A2 → A1 → A3 → A7 → A9 → A8. | Claude (with user) |

## Related Documents

- Parent design: [fast-explorer-core.design.md](./fast-explorer-core.design.md)
- Release pipeline: [../../../README.md](../../../README.md), [../../../docs/RELEASING.md](../../RELEASING.md)

---

## 1. Overview

v0.1에서 주소표시줄은 페인당 `WC_COMBOBOXEXW` 하나로, 셸 네임스페이스 트리 드롭다운(`AddressBarPopup`)과 Enter 커밋만 지원한다. 네비게이션(뒤/앞/위/새로고침)과 폴더 컨텍스트 액션(새 폴더, 탐색기/터미널, 경로복사 등)은 모두 단축키에 묶여 있어, 단축키를 모르는 사용자는 메뉴 표면 없이 기능을 발견할 수 없다.

v0.2는 이 표면을 두 부분으로 보강한다.

1. **네비 툴바** — 주소표시줄 좌측에 4개 버튼 (뒤·앞·위·새로고침). 기존 단축키와 1:1 매핑.
2. **햄버거 메뉴 (`≡`)** — 주소표시줄 우측 버튼. 클릭 시 폴더 컨텍스트 액션 메뉴 팝업.

두 요소 모두 **페인당 1세트**로 두어 (예: 좌우 dual-pane 각각 자기 툴바와 햄버거를 가짐) 컨텍스트가 흐려지지 않게 한다.

명시적으로 v0.2에서 다루지 **않는** 것:

- 브레드크럼 (주소표시줄 자체 형태 변경) — 비용 큼, v0.3 단독 milestone
- 드롭다운에 히스토리/즐겨찾기/환경변수 매크로 채우기 — 영속 스키마 결정이 선행돼야 함
- 글로벌 메뉴바 / 리본 — Phase 3 README 작성 시 미니멀 chrome 방향 합의 유지

---

## 2. Visual Layout

페인당 1행 추가. 기존 주소표시줄과 같은 가로 영역을 공유.

```
┌──────────────────────────────────────────────────────────────────┐
│ [<] [>] [^] [⟳]  [ C:\Users\SOOJANG\dev\github\fast-explorer ▼ ] [≡] │  ← 페인 0 toolbar row
├──────────────────────────────────────────────────────────────────┤
│ name              size      modified            type             │  ← list-view header
│ ...                                                              │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

dual-pane (vertical split) 인 경우:

```
┌───────────────────────────┬───────────────────────────┐
│ [<][>][^][⟳] [ path... ▼][≡]│ [<][>][^][⟳] [ path... ▼][≡] │  ← 페인 0 / 1 각각
├───────────────────────────┼───────────────────────────┤
│ list 0                    │ list 1                    │
└───────────────────────────┴───────────────────────────┘
```

높이는 시스템 DPI 기준 ~26 px (현재 주소표시줄과 동일 row에 통합). 추가 vertical space 차지 없음.

---

## 3. Components

### 3.1 Navigation Toolbar (`PaneNavToolbar`)

- 컨트롤: Win32 `TOOLBARCLASSNAME`, `TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | CCS_NORESIZE | CCS_NODIVIDER`
- 버튼 4개:

  | ID | 아이콘 | 단축키 | 동작 | enable 조건 |
  |---|---|---|---|---|
  | `kTbBack` | `SIID_BACK` 또는 `←` glyph | Alt+← | `kAccelNavBack` 동등 | 페인 history.back 가능 |
  | `kTbForward` | `SIID_FORWARD` | Alt+→ | `kAccelNavForward` 동등 | 페인 history.forward 가능 |
  | `kTbUp` | `SIID_UP` | Alt+↑ | `kAccelNavUp` 동등 | 현재 경로의 parent 존재 |
  | `kTbRefresh` | `SIID_REFRESH` | F5 | `kAccelRefresh` 동등 | 항상 활성 |

- 아이콘: `SHGetStockIconInfo`로 시스템 stock 아이콘 16×16 또는 20×20. 자체 아이콘 셋 없이 OS 비주얼 일관성 확보.
- 클릭 처리: `TBN_DROPDOWN` 미사용 (단순 푸시 버튼). `WM_COMMAND`로 페인 컨트롤러에 라우팅.
- enable/disable 갱신: `kWmFeEnumComplete` 처리 후 페인 history/path 상태를 보고 `TB_SETSTATE`로 일괄 갱신.

### 3.2 Hamburger Button + Popup Menu (`PaneToolMenu`)

- 컨트롤: 단일 `BUTTON` (BS_PUSHBUTTON, `≡` 또는 시스템 stock dots 아이콘) — 또는 동일 toolbar 컨트롤에 dropdown 버튼 1개 추가
- 트리거: 클릭 시 `TrackPopupMenuEx`로 HMENU 표시
- 메뉴 항목 (v0.2 확정안):

  | 그룹 | 항목 | 단축키 | 액션 |
  |---|---|---|---|
  | 폴더 | 새 폴더 | Ctrl+Shift+N | `kAccelCreateFolder` 동등 |
  | 폴더 | 새로 고침 | F5 | `kAccelRefresh` 동등 |
  | — separator — | | | |
  | 열기 | 탐색기에서 열기 | — | `ShellExecuteExW("explorer.exe", path)` |
  | 열기 | 터미널에서 열기 | — | Windows Terminal `wt -d <path>` 시도 → 실패 시 `pwsh.exe -NoExit -WorkingDirectory <path>` 폴백 |
  | 열기 | 경로 복사 | Ctrl+Shift+C | 현재 페인 경로를 `CF_UNICODETEXT`로 클립보드 복사 |
  | 열기 | 폴더 속성 | Alt+Enter | `SHObjectProperties(...)` 셸 다이얼로그 |
  | — separator — | | | |
  | 보기 | 숨김 항목 표시 | — | 글로벌 토글, ✓ 마크 |
  | 보기 | 확장자 표시 | — | 글로벌 토글, ✓ 마크 |

- 라우팅: `WM_COMMAND` 메뉴 ID는 페인 인덱스를 패킹 (`(menuItemId << 8) | paneIdx`).
- "보기" 그룹은 글로벌 상태이므로 페인 0/1 햄버거 둘 다 동일한 ✓ 마크를 보여야 함 (메뉴 빌드 시 현재 상태 조회).

### 3.3 Pane row layout (`PaneToolbarRow`)

- 페인 컨테이너 내부에 새 child window `PaneToolbarRow` 하나 추가, 그 안에 nav toolbar + 기존 address bar + hamburger button을 좌→우로 배치.
- `WM_SIZE` 처리: 좌측 toolbar (고정폭 ~104 px), 우측 햄버거 (~26 px), 중간 address bar는 stretch.
- 기존 `addressBars_[i]` 멤버는 그대로 유지. 새 컨트롤들은 `paneToolbarRows_[i]` 신규 멤버로 묶음.

---

## 4. State / Settings

### 4.1 Per-pane (in-memory)

- nav 버튼 enable 상태 — `PaneController`의 history depth/current path에서 파생, 별도 저장 불필요

### 4.2 Global (persisted in `settings.json` schema v4)

- `view.showHidden: bool` — 기본 `false`
- `view.showExtensions: bool` — 기본 `true` (Windows 기본과 반대 — 개발자/파워유저 타깃 가정)
- 스키마 버전: v3 → v4 마이그레이션 (기본값 채움)

### 4.3 Hidden-files 처리 위치

- `Win32FsBackend::enumerate`에서 `FILE_ATTRIBUTE_HIDDEN` 항목을 결과 리스트에서 필터링하는 게 가장 단순.
- 단점: 토글 변경 시 현재 페인 다시 enumerate해야 함 → 받아들임 (사용자가 토글한 직후이므로 expected cost).

### 4.4 Extension 표시

- `FileEntry`는 항상 full name 보존. `ColumnFormatter::formatName`이 `view.showExtensions` 조회해서 표시 시점에만 strip.
- 리네임/검색 UX는 영향 없음 (내부는 full name 유지).

---

## 5. Acceptance Criteria

v0.2 릴리스 게이트:

1. 신규 사용자가 단축키 노출 없이 마우스만으로 뒤/앞/위/새로고침/새폴더/탐색기로 열기를 모두 수행 가능
2. 페인 좌/우 각각의 햄버거가 자기 페인의 경로를 컨텍스트로 사용 (헷갈림 없음)
3. 페인 활성 전환 시 nav 버튼 enable 상태가 즉시 활성 페인 기준으로 갱신
4. "숨김 토글" 변경 후 다음 enumerate에 즉시 반영
5. 단축키 동작은 v0.1과 동일 (회귀 없음)
6. dual-pane vertical/horizontal 두 레이아웃 모두에서 layout이 깨지지 않음
7. DPI 96/144/192에서 아이콘/버튼 폭이 비례 스케일
8. 메모리 working-set delta < 1 MB (toolbar/버튼/이미지리스트 추가 비용)
9. 측정: nav 버튼 클릭 → 첫 list-view 갱신까지 50 ms 가이드라인 (기존 stall 측정 인프라 재사용)

---

## 6. Implementation Plan (atom 단위)

| Atom | 작업 | 검증 |
|---|---|---|
| T1 | `PaneToolbarRow` child window 도입, 기존 addressBar를 그 안으로 reparent, `WM_SIZE` 레이아웃 | dual-pane 양 레이아웃에서 chrome 안 깨짐 |
| T2 | `PaneNavToolbar` 4-버튼 + stock 아이콘 + WM_COMMAND 라우팅 | 클릭이 기존 accel 핸들러와 동일 결과 |
| T3 | nav 버튼 enable/disable 갱신 (`kWmFeEnumComplete` 훅) | history 끝/시작에서 버튼 비활성 |
| T4 | 햄버거 버튼 + 빈 popup menu skeleton | 클릭 시 메뉴 뜸 |
| T5 | 메뉴 항목 1군 (새 폴더 / 새로 고침) — 기존 accel 재사용 | 메뉴 클릭 동작 동등 |
| T6 | 메뉴 항목 2군 (탐색기 / 터미널 / 경로복사 / 속성) — 신규 액션 4개 | 각 액션 수동 검증 |
| T7 | 설정 v4 스키마 + showHidden / showExtensions 토글 영속 | 재시작 후 토글 유지 |
| T8 | Win32FsBackend hidden 필터 + ColumnFormatter ext strip | 토글 즉시 반영 |
| T9 | 단위 테스트 추가 — menu 항목 빌드 / enable 상태 계산 / 설정 마이그레이션 v3→v4 | tests pass |
| T10 | 측정 — working-set delta, 버튼 클릭 stall 분포 | acceptance 9 게이트 통과 |

T1~T6은 UI 통합이라 단위 테스트 적용이 제한적. T7~T9에 테스트 집중.

---

## 7. Decisions to Confirm

문서 작성자가 디폴트로 선택한 결정사항. 사용자 검토 단계에서 override 가능.

| # | 항목 | 디폴트 | 대안 |
|---|---|---|---|
| D1 | nav 버튼 위치 | 페인당 좌측 (페인 컨텍스트와 함께) | 글로벌 1세트 상단 |
| D2 | 햄버거 위치 | 페인당 우측 | 글로벌 1개 상단 우측 |
| D3 | 아이콘 소스 | `SHGetStockIconInfo` (OS 스타일) | 자체 ICO 16×16 셋 |
| D4 | 보기 토글 스코프 | 글로벌 (양 페인 동일) | per-pane |
| D5 | 확장자 표시 기본값 | `true` | `false` (Windows 기본) |
| D6 | 터미널 우선순위 | Windows Terminal (`wt`) → pwsh → cmd | pwsh 우선 |
| D7 | "경로 복사" 단축키 | Ctrl+Shift+C | 단축키 없음 (메뉴만) |
| D8 | "폴더 속성" 단축키 | Alt+Enter | 단축키 없음 (메뉴만) |
| D9 | 메뉴 ID 패킹 방식 | `(item << 8) \| pane` | 페인별 base offset (`pane * 1000 + item`) |
| D10 | 설정 마이그레이션 | v3 → v4 자동 (디폴트 채움) | 사용자 prompt 없이 silent |

---

## 8. Out of Scope (deferred to v0.3+)

- 브레드크럼 (주소표시줄을 segment-clickable 형태로 전환)
- 드롭다운에 최근 경로 / 즐겨찾기 / 환경변수 매크로
- 글로벌 메뉴바 / 리본
- 사용자 정의 툴바 (커스터마이즈)
- 메뉴 항목 키보드 가속(Alt+밑줄 문자) — 현재 chrome에 menu bar가 없어 Alt-mnemonic 충돌 없음, 추후 검토
- 다국어 — 현재 한국어/영어 혼용 그대로

---

## 9. Open Questions

1. **터미널 검출** — Windows Terminal 미설치 환경에서 graceful degrade는 pwsh.exe로 OK? cmd.exe까지 폴백 필요한가? (현재 디폴트는 wt → pwsh → cmd)
2. **숨김 토글의 즉시성** — 토글 시 전체 페인 재enum이 무난한가, 아니면 in-place 필터로 처리해야 하나? 후자가 빠르지만 view 일관성 측면에서 전자가 단순.
3. **메뉴 폰트** — `SystemParametersInfo(SPI_GETNONCLIENTMETRICS)`의 `lfMenuFont` 사용 vs 명시 폰트. 디폴트는 시스템 폰트.
4. **dual-pane 비활성 페인의 nav 버튼** — 비활성 페인의 toolbar도 enable인가? (현재 디폴트는 yes, 비활성 페인 클릭 시 그 페인이 활성화되며 동시에 액션 실행)

---

## 10. Test Strategy

- **단위 (core-tests)**
  - `PaneNavToolbar`의 enable 상태 계산 로직 (history depth, current path → boolean 4개)
  - 설정 v3 → v4 마이그레이션 (showHidden=false, showExtensions=true 기본값)
  - Win32FsBackend hidden 필터 옵션 토글
  - ColumnFormatter ext strip 토글

- **수동 (체크리스트 in `docs/02-design/runbooks/v0.2-toolbar-checklist.md`)**
  - acceptance criteria 1~9 각각
  - DPI 변경 시 핫리로드
  - 활성 페인 전환 시 enable 상태 갱신 visual check

- **회귀**
  - 기존 541개 단위 테스트 그대로 통과
  - dual-pane soak (M9 체크리스트) 반복 — chrome 추가로 stall 분포 변화 없음
