# v0.4.0 Design — Multi-Pane (up to 4) + Grid Splitter

Status: draft for review
Date: 2026-05-20
Source: brainstorming session 2026-05-20

## Goal

Support 1 to 4 simultaneous file-explorer panes with named-preset layouts and user-draggable splitters between them. Performance must match the current 2-pane build — no per-frame layout cost, no extra hot-path branches for inactive layout slots. Ratios persist across restarts. Design preserves clean seams for a future tab-per-pane extension.

User request (verbatim): "분할을 4개까지 확장하자. 거기에 grid splitter 로 크기 조정가능하도록 기능 추가하고" + "사용자가 배치를 결정하게도 할수 있어야 함" + "A 를 기본값으로 다양하게 제공하자" + "추후 tab 기반으로 확장할 수 있도록 고려도 해주고".

## Decisions made in brainstorming

| Item | Decision |
|---|---|
| 3-pane presets | A (TC: left-full + right top/bottom) default; B (top-full + bottom left/right); C (3 vertical columns) |
| 4-pane presets | A (2×2 grid) default; B (4 columns); C (4 rows); D (left-full + right 3-stack) |
| Splitter visual | 1-px hairline + invisible 4–5 DIP grab area |
| Drag feedback | Ghost line (XOR) during drag, one real relayout on mouse-up |
| Preset switching | Same-key cycle (Ctrl+3 → A→B→C→A; Ctrl+4 → A→B→C→D→A). Ctrl+1 always returns to Single. |

## Architecture

### Component responsibilities

| Component | Responsibility | v0.4.0 change |
|---|---|---|
| `PaneController` | Single folder context (path, history, listview state) | unchanged — becomes the future "Tab" body |
| `PaneManager` | Owns N slots, tracks active slot | API generalised to append/pop, N up to 4 |
| `MainWindow` | HWND ownership, message routing, layout host | per-slot arrays grow to size 4; adds `enterLayout(preset)` |
| `pane-layout.h/cpp` | Pure layout computation | adds `LayoutPreset`, expands `computePaneLayout` |
| `PaneSplitter` (new) | Splitter HWND class — cursor, hit-test, drag, paint | new file `ui/pane-splitter.{h,cpp}` |
| `settings-store` | Session persistence | schema v4 → v5 with migration |

### Tab-extension seams (do not foreclose)

- `PaneManager` API exposes only slot indices (`at(slotIdx)`, `count()`). When tabs land, slots gain a `vector<PaneController> + activeTabIdx`; the public API is unaffected — `at(slotIdx)` returns the slot's active tab.
- Layout engine works on slot rects only. It does not know what is rendered inside a slot. A future tabstrip lives inside the slot rect (above the listview) — no layout-engine change needed.
- SessionState v5 persists `pane_paths: array<string, 4>`. v6 (tabs) replaces each entry with `{ tabs: [{path,...}], active_tab: i }` via a one-step migrator (`path → tabs[0].path`).
- Per-slot view toggles (`showHidden`, `showExtensions`) stay window-global in v0.4.0. They migrate into `PaneController` when tabs land — out of scope here.

## PaneManager API

```cpp
class PaneManager {
 public:
  std::size_t openInitial(HWND host);                                 // slot 0, once

  // Appends slot at index count(). Returns new slot index (1..3).
  // initialPath falls back to active().path() when empty.
  // No-op + returns count() when already at 4 slots.
  std::size_t openPane(HWND host, const std::wstring& initialPath);

  // Pops the last slot. No-op when count() == 1.
  // activeIndex_ is clamped to count()-1 after the pop.
  void closePane() noexcept;

  bool setActive(std::size_t idx) noexcept;
  [[nodiscard]] std::size_t count() const noexcept;                   // 1..4
  [[nodiscard]] std::size_t activeIndex() const noexcept;
  [[nodiscard]] PaneController& active();
  [[nodiscard]] PaneController& at(std::size_t i);
};
```

Append-only / pop-last keeps `0..count()-1` contiguous and removes index-shift confusion. `openSecond` / `closeSecond` / `isDual()` are removed. `MainWindow::enterDualMode/enterSingleMode` become thin wrappers around the new `enterLayout(preset)` adapter or are removed outright.

## Layout engine

### Preset enum (`src/core/layout-preset.h` new)

```cpp
enum class LayoutPreset : std::uint8_t {
  Single  = 0,
  Dual_V  = 1,   // left | right
  Dual_H  = 2,   // top  / bottom
  Tri_A   = 3,   // left-full + right top/bottom
  Tri_B   = 4,   // top-full + bottom left/right
  Tri_C   = 5,   // 3 vertical columns
  Quad_A  = 6,   // 2×2 grid
  Quad_B  = 7,   // 4 columns
  Quad_C  = 8,   // 4 rows
  Quad_D  = 9,   // left-full + right 3-stack
};

[[nodiscard]] constexpr std::size_t slotCountForPreset(LayoutPreset) noexcept;
[[nodiscard]] constexpr LayoutPreset nextPresetInCycle(
    LayoutPreset current, std::size_t targetSlotCount) noexcept;
```

`nextPresetInCycle` policy:
- targetSlotCount == 1 → Single
- targetSlotCount == 2 → Dual_V (entering from non-dual); Dual_V/H toggle handled by existing `resolveLayoutToggle`
- targetSlotCount == 3 → if currently Tri_A/B/C, advance within {Tri_A, Tri_B, Tri_C} cyclically; else Tri_A
- targetSlotCount == 4 → if currently Quad_A/B/C/D, advance within {Quad_A, Quad_B, Quad_C, Quad_D} cyclically; else Quad_A

All constexpr, static-assertable in unit tests.

### Pure layout function (`src/ui/pane-layout.h` extended)

```cpp
enum class SplitterOrientation : std::uint8_t { Vertical = 0, Horizontal = 1 };

struct SplitterRect {
  RECT hitRect;                       // includes 4–5 DIP grab padding
  RECT visualRect;                    // 1-px hairline location
  SplitterOrientation orient;
  std::uint8_t ratioId;               // 0..2: which ratio in SplitterRatios this drives
};

struct PaneLayoutResult {
  std::array<RECT, 4> slots{};                  // unused slots zeroed
  std::array<SplitterRect, 3> splitters{};      // unused entries zeroed
  std::size_t slotCount = 0;
  std::size_t splitterCount = 0;
};

[[nodiscard]] PaneLayoutResult computePaneLayout(
    LayoutPreset preset,
    const SplitterRatios& ratios,                 // ratios for THIS preset
    int clientWidth, int clientHeight,
    int reservedTop, int reservedBottom) noexcept;
```

Single switch on `preset`, no allocation, no recursion. `reservedTop` / `reservedBottom` carve out window-global strips above/below the slot grid; the current build passes `reservedTop = 0` because each slot internally hosts its own `PaneToolbarRow` at the top of its returned rect, and that internal layout is unchanged. `reservedBottom` is the status-bar height. Parameter kept so a future window-wide menu/toolbar can use it without re-plumbing.

**Ratio semantics per preset.** Each `ratios[i]` is a split-line position in [0,1] along the relevant axis. Interpretation is per-preset and lives in the implementation:
- `Dual_V`: `ratios[0]` = vertical seam x-position as fraction of width.
- `Tri_A`: `ratios[0]` = vertical seam x; `ratios[1]` = horizontal seam y inside the right column as fraction of height.
- `Quad_A`: `ratios[0]` = vertical seam x; `ratios[1]` = horizontal seam y in left column; `ratios[2]` = horizontal seam y in right column.
- `Quad_B` (4 columns): `ratios[0..2]` = three cumulative vertical seam positions, each clamped to `[ratios[i-1]+ε, ratios[i+1]-ε]` so columns can't invert. Same shape for `Quad_C` (rows).
- `Quad_D`: `ratios[0]` = vertical seam x; `ratios[1]`, `ratios[2]` = two cumulative horizontal seam positions in the right column.

The clamp logic lives in `computeNewRatio` (called by `PaneSplitter` on drag end) for the non-cumulative case, and in a sibling `computeNewCumulativeRatio` for `Quad_B/C/D` cumulative splitters.

### Ratio storage

Each preset has up to 3 splitter ratios. Storage is per-preset so switching preserves the user's tuning per layout:

```cpp
struct SplitterRatios {
  std::array<float, 3> ratios{0.5f, 0.5f, 0.5f};
};

// In SessionState:
std::array<SplitterRatios, 10> ratiosPerPreset;   // indexed by LayoutPreset value
```

Total persisted: 30 floats. Defaults: see Persistence section.

## PaneSplitter (`src/ui/pane-splitter.{h,cpp}` new)

Registered class name: `FastExplorer.PaneSplitter`. Child of MainWindow. One HWND per concurrently visible splitter. Pre-create the worst-case count (3) at startup, `ShowWindow(SW_HIDE)` when the active preset doesn't use that slot — no per-cycle create/destroy.

### Per-splitter context

```cpp
struct SplitterContext {
  SplitterOrientation orient;
  std::uint8_t ratioId;
  LayoutPreset* presetPtr;                       // borrowed; MainWindow owns
  SplitterRatios* ratiosPtr;                     // borrowed; MainWindow owns
  std::function<void()> onCommit;                // MainWindow::relayout
};
```

Set via `SetWindowLongPtrW(GWLP_USERDATA, ...)` at `WM_NCCREATE` (from `CREATESTRUCTW::lpCreateParams`).

### Message handling

| Message | Behavior |
|---|---|
| `WM_SETCURSOR` | `SetCursor(LoadCursorW(NULL, orient==V ? IDC_SIZEWE : IDC_SIZENS))`, return TRUE |
| `WM_LBUTTONDOWN` | `SetCapture(hwnd)`. Cache start mouse pos (screen coords) + start ratio. Draw initial ghost line on parent DC via `R2_NOTXORPEN` |
| `WM_MOUSEMOVE` (captured) | XOR-erase previous ghost line; compute clamped new line position; XOR-draw new line. Ratio not yet committed |
| `WM_LBUTTONUP` | `ReleaseCapture()`. XOR-erase final ghost. Commit new ratio via pure helper `computeNewRatio(...)`. Call `onCommit()` → `MainWindow::relayout()` |
| `WM_PAINT` | Fill 1-px hairline at slot edge using theme-aware color (`COLOR_3DSHADOW` light / `0x3F3F3F` dark). Rest of HWND is transparent (no `WM_ERASEBKGND` fill) |
| `WM_ERASEBKGND` | return 1 (no flicker) |

### Pure ratio helper (`src/ui/splitter-resize.h` new)

```cpp
[[nodiscard]] constexpr float computeNewRatio(
    float startRatio,
    int startMouseAlongAxis,
    int currentMouseAlongAxis,
    int axisLength,
    float minRatio = 0.1f,
    float maxRatio = 0.9f) noexcept;
```

Pure, constexpr, ctest-friendly. Clamping prevents a pane from being collapsed to zero (which would make it unrecoverable without resort to keyboard).

## Persistence — SessionState v5

### New fields

```cpp
struct SessionState {
  int windowX, windowY, windowWidth, windowHeight;  // unchanged

  std::array<std::wstring, 4> panePaths;            // unused slots empty
  std::size_t paneCount = 1;                        // 1..4
  std::size_t activePane = 0;

  LayoutPreset preset = LayoutPreset::Single;
  std::array<SplitterRatios, 10> ratiosPerPreset;

  bool showHidden = false;
  bool showExtensions = true;
};
```

### JSON shape (schema_version = 5)

```json
{
  "schema_version": 5,
  "window": { "x": ..., "y": ..., "w": ..., "h": ... },
  "pane_paths": ["c:/...", "d:/...", "", ""],
  "pane_count": 2,
  "active_pane": 0,
  "preset": "dual_v",
  "ratios": {
    "dual_v": [0.5],
    "tri_a":  [0.4, 0.5],
    "quad_a": [0.5, 0.5, 0.5]
  },
  "show_hidden": false,
  "show_extensions": true
}
```

`ratios` only serializes presets the user has touched; unloaded presets fall back to defaults. Unknown `preset` strings are treated as `single` (lenient forward-compat); wrong-typed values fail the load (strict against corruption) — same policy as v4.

Preset string ↔ enum mapping (one place: `settings-store.cpp`):

```
"single" ↔ Single,  "dual_v" ↔ Dual_V,  "dual_h" ↔ Dual_H,
"tri_a"  ↔ Tri_A,   "tri_b"  ↔ Tri_B,   "tri_c"  ↔ Tri_C,
"quad_a" ↔ Quad_A,  "quad_b" ↔ Quad_B,  "quad_c" ↔ Quad_C,  "quad_d" ↔ Quad_D
```

The same mapping is used as the JSON object keys under `"ratios"`. Each ratio array's length matches the splitter count for that preset (1 for dual, 2 for tri, 3 for quad). Loading tolerates shorter-than-expected arrays (missing entries fall back to defaults) and longer arrays (extras ignored).

### v4 → v5 migration (in `loadSessionState`)

If `schema_version` is missing or ≤ 4, parse v4 fields and map:

| v4 field | v5 field |
|---|---|
| `last_path` | `pane_paths[0]` |
| `second_path` (if present) | `pane_paths[1]` |
| `layout_mode == single` | `preset = Single`, `pane_count = 1` |
| `layout_mode == dual` + `orientation == vertical` | `preset = Dual_V`, `pane_count = 2` |
| `layout_mode == dual` + `orientation == horizontal` | `preset = Dual_H`, `pane_count = 2` |
| (no v4 ratios persisted) | all `ratiosPerPreset` left at defaults |

Next save writes v5.

### Default ratios (first use)

| Preset | Defaults |
|---|---|
| Dual_V / Dual_H | 0.5 |
| Tri_A | rV=0.4, rH_right=0.5 |
| Tri_B | rH=0.4, rV_bottom=0.5 |
| Tri_C | 0.333, 0.667 |
| Quad_A | rV=0.5, rH_left=0.5, rH_right=0.5 |
| Quad_B | 0.25, 0.5, 0.75 |
| Quad_C | 0.25, 0.5, 0.75 |
| Quad_D | rV=0.5, rH_1=0.333, rH_2=0.667 |

## Keybindings

| Key | Action |
|---|---|
| Ctrl+1 | `enterLayout(Single)` — always |
| Ctrl+2 | Single → `enterLayout(lastDualPreset_)` (defaults to `Dual_V`); Dual_V/H → existing `resolveLayoutToggle` (Alt+V/H still flips seam); Tri/Quad → `enterLayout(lastDualPreset_)`. `lastDualPreset_` is updated whenever a Dual_V/H is entered |
| Ctrl+3 | Tri_* → `nextPresetInCycle(current, 3)`; else `enterLayout(Tri_A)` |
| Ctrl+4 | Quad_* → `nextPresetInCycle(current, 4)`; else `enterLayout(Quad_A)` |
| Alt+V / Alt+H | Dual_V/H only (unchanged). No-op on other presets |
| Existing accels (F2, Ctrl+Shift+N, Delete, navigation, etc.) | Unchanged — target `paneManager_.active()` |

New `messages.h` constants: `kAccelLayoutTri` (Ctrl+3), `kAccelLayoutQuad` (Ctrl+4). Accel table in `src/app/main.cpp` extended. `lastDualPreset_` is a MainWindow private member, initialized to `Dual_V`, updated whenever `enterLayout(Dual_V|Dual_H)` runs.

## MainWindow integration

### Per-slot arrays — `[2]` → `[4]`

`listViews_`, `addressBars_`, `addressDropdownBtns_`, `dropTargets_`, `paneToolbarRows_`, `firstBatchSeen_`, `selectionSyncs_`, `iconCoords_`, `labelEdits_`. All become `std::array<T, 4>`. Slots beyond `count()` stay null/default; never indexed.

New: `std::array<HWND, 3> splitters_` — pre-created at `onCreate`, hidden initially.

### `enterLayout(LayoutPreset target)`

1. `targetCount = slotCountForPreset(target)`
2. Adjust `paneManager_` to `targetCount`: while `count() < targetCount` call `openPane(...)`; while `count() > targetCount` call `closePane()`. New panes use `active().path()` as fallback.
3. `preset_ = target`
4. `relayout()` — sync `splitters_` visibility, reposition all HWNDs.

`enterSingleMode` / `enterDualMode` removed; old call sites use `enterLayout`.

### `relayout()`

1. Get client rect.
2. Compute reservedTop (per-pane toolbar row height inside each slot's own area — stays as today). reservedBottom = status bar.
3. Call `computePaneLayout(preset_, ratiosPerPreset_[size_t(preset_)], W, H, top, bottom)`.
4. Position each slot's HWNDs into `slots[i]` (existing per-pane internal layout — toolbar row at top of rect, listview below).
5. Position+show `splitterCount` splitter HWNDs, hide the rest.
6. `applyStatusParts(W)` resizes status bar to N parts.

No branching by preset in step 4 — uniform per-slot loop.

## Testing strategy

### Unit (`core-tests`, ctest)

- `computePaneLayout` per preset (10 presets × ~5 client sizes including edges) — slot rects union covers client area minus splitter strips; no overlap.
- `slotCountForPreset` — all 10 enums; `static_assert`s.
- `nextPresetInCycle` — every (current, targetCount) combination.
- `computeNewRatio` — boundary, mid-range, clamp.
- v4 → v5 migration — 4 scenarios (single, dual-v, dual-h, missing/corrupted layout_mode).
- SessionState v5 round-trip (write → read).
- Existing 558 tests — all must still pass.

### Manual integration

- Each preset entry → drag each splitter → relayout happens once on mouse-up; no flicker.
- Ctrl+3 spam (5 quick presses) → no preset skipped, no leaked splitters.
- App restart → last preset + ratios + paths restored.
- 4-pane with 10k items per listview → preset switch frame time within 2× of single-pane (perf-tracker dump).
- Win10 (Segoe UI fallback) + Win11 — visual parity for splitter hairline color.

### Performance assertions (compile-time + runtime)

- Hot path: `WM_SIZE` → `relayout()` is the only layout call. Splitter drag uses XOR ghost — zero layout calls during drag.
- `computePaneLayout` is `noexcept`, no allocation. Single function called once per resize.
- Splitter HWND count is fixed (3); preset switching toggles `SW_HIDE/SW_SHOW`, never create/destroy.

## Files touched

| Path | Change |
|---|---|
| `src/core/layout-preset.h` | new — enum + helpers |
| `src/core/layout-orientation.h` | retained (used by Dual_V/H seam) |
| `src/core/settings-store.{h,cpp}` | v5 schema, migration |
| `src/ui/pane-layout.{h,cpp}` | expand to all 10 presets + splitter rects |
| `src/ui/pane-manager.{h,cpp}` | append/pop API; drop dual-only methods |
| `src/ui/pane-splitter.{h,cpp}` | new — registered class + message handlers |
| `src/ui/splitter-resize.h` | new — pure ratio helper |
| `src/ui/main-window.{h,cpp}` | per-slot arrays size 4; `enterLayout`; splitter HWND wiring |
| `src/ui/messages.h` | accel IDs for Ctrl+3 / Ctrl+4 |
| `src/app/main.cpp` | accel table entries |
| `tests/...` | new test files matching above |

## Out of scope (deferred)

- Tab-per-pane UI (designed for, not built).
- Splitter snap behavior.
- Drag-to-swap-panes.
- Drag splitter to extreme edge to "minimize" a pane (current min clamp = 0.1).
- Network folder tree expansion (still blocked by Shell API behavior on this machine — see handoff).
