# Multi-Pane (1..4) + Grid Splitter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend FastExplorer from 2 panes to up to 4 panes with named-preset layouts (Tri_A..C, Quad_A..D), a registered `PaneSplitter` HWND class with ghost-line drag, same-key cycling (Ctrl+3 / Ctrl+4), and SessionState v5 persistence (with a v4 migrator).

**Architecture:** Pure layout function (`computePaneLayout`) selects per-slot RECTs and splitter RECTs by `LayoutPreset` enum and per-preset `SplitterRatios`. `PaneManager` becomes append/pop with up to 4 slots. `MainWindow` owns 4-sized per-slot HWND arrays plus 3 pre-created splitter HWNDs (shown/hidden per preset). `PaneController` stays as the single-folder context — the future "Tab" body, untouched by this plan.

**Tech Stack:** C++17, Win32 API, custom JSON for settings, ctest with `FE_TEST_CASE` harness, CMake + Ninja, VS 2026 Pro. Spec: `docs/superpowers/specs/2026-05-20-multi-pane-splitter-design.md`.

**Build/Test commands (repo conventions, from CLAUDE.md memory):**

```
Enter-VsDevShell                                                         # PowerShell, once per shell
cmake --build build --config Release --target FastExplorer core-tests
ctest --test-dir build --output-on-failure -C Release
Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue
Start-Process build\FastExplorer.exe
```

`core-tests` is the test binary (registered in root `CMakeLists.txt`); new test files must be added to its `add_executable` source list.

---

## File map

**New files (created by tasks below):**

| Path | Purpose | Task |
|---|---|---|
| `src/core/layout-preset.h` | `LayoutPreset` enum + `slotCountForPreset` + `nextPresetInCycle` | 1, 2 |
| `src/ui/splitter-ratios.h` | `SplitterRatios` struct + per-preset defaults | 3 |
| `src/ui/splitter-resize.h` | Pure ratio recompute on drag | 4 |
| `src/ui/pane-splitter.h` | `PaneSplitter` HWND class declaration | 25 |
| `src/ui/pane-splitter.cpp` | `PaneSplitter` implementation | 25, 26 |
| `tests/layout-preset-tests.cpp` | Tests for enum helpers + cycle | 1, 2 |
| `tests/splitter-ratios-tests.cpp` | Defaults table tests | 3 |
| `tests/splitter-resize-tests.cpp` | Ratio recompute tests | 4 |

**Modified files:**

| Path | Change | Task |
|---|---|---|
| `src/ui/pane-layout.h` | Adds `SplitterOrientation`, `SplitterRect`, `PaneLayoutResult`, `computePaneLayout` | 5..12 |
| `src/ui/pane-layout.cpp` | Implementation per preset | 5..12 |
| `tests/pane-layout-tests.cpp` | Tests per preset | 5..12 |
| `src/ui/pane-manager.h` | New `openPane(host, path)` / `closePane()`; drop `openSecond/closeSecond/isDual` | 19 |
| `src/ui/pane-manager.cpp` | Implementation | 19 |
| `tests/pane-manager-tests.cpp` | Tests for 4-slot ops | 20 |
| `src/core/settings-store.h` | SessionState v5 fields; `LayoutPreset` member | 14 |
| `src/core/settings-store.cpp` | v5 reader, writer, v4→v5 migration | 15, 16, 17 |
| `tests/settings-store-tests.cpp` | Round-trip + migration tests | 18 |
| `src/ui/main-window.h` | Per-slot arrays grow to `[4]`; add `enterLayout`; add `lastDualPreset_`; add splitter HWND array | 22, 27, 28 |
| `src/ui/main-window.cpp` | Implementation; relayout uses `computePaneLayout` | 22, 28, 29, 30, 31, 33, 34, 35, 36 |
| `src/ui/messages.h` | New accel IDs `kAccelLayoutTri`, `kAccelLayoutQuad` | 32 |
| `src/app/main.cpp` | Accel table entries | 32 |
| `CMakeLists.txt` | Add new sources to `FastExplorer` + `core-tests` targets | 1, 3, 4, 14, 19, 25 |

---

## Phase 1 — Pure data and helpers (no UI dependency)

### Task 1: LayoutPreset enum + slotCountForPreset

**Files:**
- Create: `src/core/layout-preset.h`
- Create: `tests/layout-preset-tests.cpp`
- Modify: `CMakeLists.txt` (add new sources to both `FastExplorer` and `core-tests` source lists)

- [ ] **Step 1: Write the failing test**

`tests/layout-preset-tests.cpp`:
```cpp
#include "core/layout-preset.h"
#include "test-harness.h"

using fast_explorer::core::LayoutPreset;
using fast_explorer::core::slotCountForPreset;

FE_TEST_CASE(LayoutPreset_slotCount_Single)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Single), std::size_t{1}); }
FE_TEST_CASE(LayoutPreset_slotCount_Dual_V)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Dual_V), std::size_t{2}); }
FE_TEST_CASE(LayoutPreset_slotCount_Dual_H)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Dual_H), std::size_t{2}); }
FE_TEST_CASE(LayoutPreset_slotCount_Tri_A)   { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Tri_A),  std::size_t{3}); }
FE_TEST_CASE(LayoutPreset_slotCount_Tri_B)   { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Tri_B),  std::size_t{3}); }
FE_TEST_CASE(LayoutPreset_slotCount_Tri_C)   { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Tri_C),  std::size_t{3}); }
FE_TEST_CASE(LayoutPreset_slotCount_Quad_A)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Quad_A), std::size_t{4}); }
FE_TEST_CASE(LayoutPreset_slotCount_Quad_B)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Quad_B), std::size_t{4}); }
FE_TEST_CASE(LayoutPreset_slotCount_Quad_C)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Quad_C), std::size_t{4}); }
FE_TEST_CASE(LayoutPreset_slotCount_Quad_D)  { FE_ASSERT_EQ(slotCountForPreset(LayoutPreset::Quad_D), std::size_t{4}); }
```

- [ ] **Step 2: Add new sources to CMakeLists.txt**

In `CMakeLists.txt`, add `src/core/layout-preset.h` to BOTH the `FastExplorer` `add_executable(...)` source list AND the `core-tests` source list (the `core-tests` target's source list is the second `add_executable(core-tests ...)` block).

Add `tests/layout-preset-tests.cpp` to the `core-tests` source list only.

- [ ] **Step 3: Run test to verify it fails (compile error)**

```powershell
cmake --build build --config Release --target core-tests
```

Expected: compile error — `core/layout-preset.h` not found.

- [ ] **Step 4: Write minimal implementation**

`src/core/layout-preset.h`:
```cpp
#pragma once

#include <cstdint>
#include <cstddef>

namespace fast_explorer::core {

enum class LayoutPreset : std::uint8_t {
  Single  = 0,
  Dual_V  = 1,
  Dual_H  = 2,
  Tri_A   = 3,
  Tri_B   = 4,
  Tri_C   = 5,
  Quad_A  = 6,
  Quad_B  = 7,
  Quad_C  = 8,
  Quad_D  = 9,
};

inline constexpr std::size_t kLayoutPresetCount = 10;

[[nodiscard]] constexpr std::size_t slotCountForPreset(LayoutPreset p) noexcept {
  switch (p) {
    case LayoutPreset::Single: return 1;
    case LayoutPreset::Dual_V: return 2;
    case LayoutPreset::Dual_H: return 2;
    case LayoutPreset::Tri_A:  return 3;
    case LayoutPreset::Tri_B:  return 3;
    case LayoutPreset::Tri_C:  return 3;
    case LayoutPreset::Quad_A: return 4;
    case LayoutPreset::Quad_B: return 4;
    case LayoutPreset::Quad_C: return 4;
    case LayoutPreset::Quad_D: return 4;
  }
  return 1;
}

}  // namespace fast_explorer::core
```

- [ ] **Step 5: Run tests to verify they pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R LayoutPreset_slotCount
```

Expected: all 10 cases PASS.

- [ ] **Step 6: Commit**

```powershell
git add src/core/layout-preset.h tests/layout-preset-tests.cpp CMakeLists.txt
git commit -m "feat(core): add LayoutPreset enum + slotCountForPreset"
```

---

### Task 2: nextPresetInCycle

**Files:**
- Modify: `src/core/layout-preset.h:end-of-namespace`
- Modify: `tests/layout-preset-tests.cpp:end`

- [ ] **Step 1: Add failing tests**

Append to `tests/layout-preset-tests.cpp`:
```cpp
using fast_explorer::core::nextPresetInCycle;

FE_TEST_CASE(NextPreset_TargetOne_FromAnything_ReturnsSingle) {
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Quad_C, 1), LayoutPreset::Single);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Single, 1), LayoutPreset::Single);
}

FE_TEST_CASE(NextPreset_TargetTwo_FromSingle_ReturnsDualV) {
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Single, 2), LayoutPreset::Dual_V);
}

FE_TEST_CASE(NextPreset_TargetTwo_FromDualV_ReturnsDualV_NoCycleInDual) {
  // Dual seam flip is handled by Alt+V/H via resolveLayoutToggle, not by cycle.
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Dual_V, 2), LayoutPreset::Dual_V);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Dual_H, 2), LayoutPreset::Dual_H);
}

FE_TEST_CASE(NextPreset_TargetThree_EnterFromOther_ReturnsTriA) {
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Single, 3), LayoutPreset::Tri_A);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Dual_V, 3), LayoutPreset::Tri_A);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Quad_A, 3), LayoutPreset::Tri_A);
}

FE_TEST_CASE(NextPreset_TargetThree_CycleA_B_C_A) {
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Tri_A, 3), LayoutPreset::Tri_B);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Tri_B, 3), LayoutPreset::Tri_C);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Tri_C, 3), LayoutPreset::Tri_A);
}

FE_TEST_CASE(NextPreset_TargetFour_EnterFromOther_ReturnsQuadA) {
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Single, 4), LayoutPreset::Quad_A);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Tri_B,  4), LayoutPreset::Quad_A);
}

FE_TEST_CASE(NextPreset_TargetFour_CycleA_B_C_D_A) {
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Quad_A, 4), LayoutPreset::Quad_B);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Quad_B, 4), LayoutPreset::Quad_C);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Quad_C, 4), LayoutPreset::Quad_D);
  FE_ASSERT_EQ(nextPresetInCycle(LayoutPreset::Quad_D, 4), LayoutPreset::Quad_A);
}
```

- [ ] **Step 2: Run test to verify it fails (compile error)**

```powershell
cmake --build build --config Release --target core-tests
```

Expected: compile error — `nextPresetInCycle` not declared.

- [ ] **Step 3: Add implementation**

Append inside the `fast_explorer::core` namespace in `src/core/layout-preset.h`, before the closing `}`:
```cpp
[[nodiscard]] constexpr LayoutPreset nextPresetInCycle(
    LayoutPreset current, std::size_t targetSlotCount) noexcept {
  if (targetSlotCount <= 1) return LayoutPreset::Single;
  if (targetSlotCount == 2) {
    // Dual seam flip is owned by resolveLayoutToggle (Alt+V/H);
    // entering from non-dual lands on Dual_V.
    if (current == LayoutPreset::Dual_V || current == LayoutPreset::Dual_H) {
      return current;
    }
    return LayoutPreset::Dual_V;
  }
  if (targetSlotCount == 3) {
    switch (current) {
      case LayoutPreset::Tri_A: return LayoutPreset::Tri_B;
      case LayoutPreset::Tri_B: return LayoutPreset::Tri_C;
      case LayoutPreset::Tri_C: return LayoutPreset::Tri_A;
      default:                  return LayoutPreset::Tri_A;
    }
  }
  // targetSlotCount >= 4
  switch (current) {
    case LayoutPreset::Quad_A: return LayoutPreset::Quad_B;
    case LayoutPreset::Quad_B: return LayoutPreset::Quad_C;
    case LayoutPreset::Quad_C: return LayoutPreset::Quad_D;
    case LayoutPreset::Quad_D: return LayoutPreset::Quad_A;
    default:                   return LayoutPreset::Quad_A;
  }
}
```

- [ ] **Step 4: Run tests to verify pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R NextPreset
```

Expected: all 7 cases PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/core/layout-preset.h tests/layout-preset-tests.cpp
git commit -m "feat(core): add nextPresetInCycle policy"
```

---

### Task 3: SplitterRatios struct + per-preset defaults

**Files:**
- Create: `src/ui/splitter-ratios.h`
- Create: `tests/splitter-ratios-tests.cpp`
- Modify: `CMakeLists.txt` (add new sources to both targets)

- [ ] **Step 1: Write failing tests**

`tests/splitter-ratios-tests.cpp`:
```cpp
#include "test-harness.h"
#include "ui/splitter-ratios.h"

using fast_explorer::core::LayoutPreset;
using fast_explorer::ui::defaultRatiosFor;
using fast_explorer::ui::SplitterRatios;

namespace {
bool approxEq(float a, float b) noexcept {
  return (a > b ? a - b : b - a) < 1e-4f;
}
}

FE_TEST_CASE(DefaultRatios_DualV_HalfHalf) {
  const auto r = defaultRatiosFor(LayoutPreset::Dual_V);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.5f));
}

FE_TEST_CASE(DefaultRatios_TriA_VerticalSeam40_RightInnerHorizontal50) {
  const auto r = defaultRatiosFor(LayoutPreset::Tri_A);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.4f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.5f));
}

FE_TEST_CASE(DefaultRatios_TriC_ThirdsApproximate) {
  const auto r = defaultRatiosFor(LayoutPreset::Tri_C);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 1.0f / 3.0f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 2.0f / 3.0f));
}

FE_TEST_CASE(DefaultRatios_QuadA_AllHalf) {
  const auto r = defaultRatiosFor(LayoutPreset::Quad_A);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.5f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.5f));
  FE_ASSERT_TRUE(approxEq(r.ratios[2], 0.5f));
}

FE_TEST_CASE(DefaultRatios_QuadB_QuartileColumns) {
  const auto r = defaultRatiosFor(LayoutPreset::Quad_B);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.25f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.5f));
  FE_ASSERT_TRUE(approxEq(r.ratios[2], 0.75f));
}

FE_TEST_CASE(DefaultRatios_Single_AllZero_Unused) {
  const auto r = defaultRatiosFor(LayoutPreset::Single);
  FE_ASSERT_TRUE(approxEq(r.ratios[0], 0.0f));
  FE_ASSERT_TRUE(approxEq(r.ratios[1], 0.0f));
  FE_ASSERT_TRUE(approxEq(r.ratios[2], 0.0f));
}
```

- [ ] **Step 2: Add sources to CMakeLists.txt**

Add `src/ui/splitter-ratios.h` to BOTH `FastExplorer` and `core-tests` source lists.
Add `tests/splitter-ratios-tests.cpp` to `core-tests` only.

- [ ] **Step 3: Run to verify failure (compile error)**

```powershell
cmake --build build --config Release --target core-tests
```

Expected: header not found.

- [ ] **Step 4: Write the header**

`src/ui/splitter-ratios.h`:
```cpp
#pragma once

#include <array>
#include <cstddef>

#include "core/layout-preset.h"

namespace fast_explorer::ui {

// Up to three split-line positions in [0,1]. Interpretation is
// per-preset; see docs/superpowers/specs/2026-05-20-multi-pane-splitter-design.md
// for the per-preset semantics (non-cumulative vs cumulative).
// Unused slots are 0.0f; computePaneLayout ignores them.
struct SplitterRatios {
  std::array<float, 3> ratios{0.0f, 0.0f, 0.0f};
};

[[nodiscard]] constexpr SplitterRatios defaultRatiosFor(
    fast_explorer::core::LayoutPreset p) noexcept {
  using P = fast_explorer::core::LayoutPreset;
  switch (p) {
    case P::Single: return {{0.0f, 0.0f, 0.0f}};
    case P::Dual_V: return {{0.5f, 0.0f, 0.0f}};
    case P::Dual_H: return {{0.5f, 0.0f, 0.0f}};
    case P::Tri_A:  return {{0.4f, 0.5f, 0.0f}};
    case P::Tri_B:  return {{0.4f, 0.5f, 0.0f}};
    case P::Tri_C:  return {{1.0f / 3.0f, 2.0f / 3.0f, 0.0f}};
    case P::Quad_A: return {{0.5f, 0.5f, 0.5f}};
    case P::Quad_B: return {{0.25f, 0.5f, 0.75f}};
    case P::Quad_C: return {{0.25f, 0.5f, 0.75f}};
    case P::Quad_D: return {{0.5f, 1.0f / 3.0f, 2.0f / 3.0f}};
  }
  return {};
}

}  // namespace fast_explorer::ui
```

- [ ] **Step 5: Run tests to verify pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R DefaultRatios
```

Expected: all 6 cases PASS.

- [ ] **Step 6: Commit**

```powershell
git add src/ui/splitter-ratios.h tests/splitter-ratios-tests.cpp CMakeLists.txt
git commit -m "feat(ui): add SplitterRatios + per-preset defaults"
```

---

### Task 4: computeNewRatio / computeNewCumulativeRatio pure helpers

**Files:**
- Create: `src/ui/splitter-resize.h`
- Create: `tests/splitter-resize-tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Failing tests**

`tests/splitter-resize-tests.cpp`:
```cpp
#include "test-harness.h"
#include "ui/splitter-resize.h"

using fast_explorer::ui::computeNewRatio;
using fast_explorer::ui::computeNewCumulativeRatio;

namespace {
bool approxEq(float a, float b) noexcept {
  return (a > b ? a - b : b - a) < 1e-4f;
}
}

FE_TEST_CASE(ComputeNewRatio_NoMovement_ReturnsStart) {
  const float out = computeNewRatio(0.5f, 640, 640, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.5f));
}

FE_TEST_CASE(ComputeNewRatio_RightMove_IncreasesRatio) {
  // 640 -> 800 across width 1280 = +160/1280 = +0.125
  const float out = computeNewRatio(0.5f, 640, 800, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.625f));
}

FE_TEST_CASE(ComputeNewRatio_ClampMin) {
  const float out = computeNewRatio(0.5f, 640, -2000, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.1f));
}

FE_TEST_CASE(ComputeNewRatio_ClampMax) {
  const float out = computeNewRatio(0.5f, 640, 9999, 1280);
  FE_ASSERT_TRUE(approxEq(out, 0.9f));
}

FE_TEST_CASE(ComputeNewRatio_AxisLengthZero_ReturnsStart) {
  const float out = computeNewRatio(0.3f, 100, 200, 0);
  FE_ASSERT_TRUE(approxEq(out, 0.3f));
}

FE_TEST_CASE(ComputeNewCumulativeRatio_RespectsNeighborBands) {
  // 4-column layout, dragging the middle splitter (ratios[1] start 0.5)
  // with neighbors at 0.25 and 0.75. Drag past neighbor must clamp.
  const float out = computeNewCumulativeRatio(0.5f, 640, 1100, 1280,
                                              /*prevRatio*/ 0.25f,
                                              /*nextRatio*/ 0.75f);
  // 1100/1280 ~= 0.859 > 0.75 -> clamp to 0.75 - epsilon (<=0.749)
  FE_ASSERT_TRUE(out < 0.75f);
  FE_ASSERT_TRUE(out > 0.74f);
}

FE_TEST_CASE(ComputeNewCumulativeRatio_LeftClamp) {
  const float out = computeNewCumulativeRatio(0.5f, 640, 100, 1280,
                                              0.25f, 0.75f);
  FE_ASSERT_TRUE(out > 0.25f);
  FE_ASSERT_TRUE(out < 0.26f);
}
```

- [ ] **Step 2: CMakeLists.txt — add `src/ui/splitter-resize.h` to both targets and `tests/splitter-resize-tests.cpp` to core-tests.**

- [ ] **Step 3: Run to verify fail (compile error).**

```powershell
cmake --build build --config Release --target core-tests
```

Expected: header not found.

- [ ] **Step 4: Implementation**

`src/ui/splitter-resize.h`:
```cpp
#pragma once

namespace fast_explorer::ui {

// Re-derive a split-line ratio after the user drags the splitter from
// `startMouseAlongAxis` to `currentMouseAlongAxis` along an axis of
// `axisLength` pixels. `startRatio` is the ratio at drag start.
// Clamped to [minRatio, maxRatio] so no pane collapses to zero (and
// stays recoverable by drag-back). Pure, constexpr.
[[nodiscard]] constexpr float computeNewRatio(
    float startRatio,
    int startMouseAlongAxis,
    int currentMouseAlongAxis,
    int axisLength,
    float minRatio = 0.1f,
    float maxRatio = 0.9f) noexcept {
  if (axisLength <= 0) return startRatio;
  const float delta = static_cast<float>(currentMouseAlongAxis -
                                          startMouseAlongAxis) /
                      static_cast<float>(axisLength);
  float out = startRatio + delta;
  if (out < minRatio) out = minRatio;
  if (out > maxRatio) out = maxRatio;
  return out;
}

// Variant for cumulative-ratio presets (Quad_B / Quad_C / Quad_D):
// the splitter's ratio must stay strictly between its neighbors so
// columns/rows cannot invert. `prevRatio` and `nextRatio` are the
// ratios of the splitters immediately before and after this one;
// pass 0.0f / 1.0f when no neighbor exists on that side.
[[nodiscard]] constexpr float computeNewCumulativeRatio(
    float startRatio,
    int startMouseAlongAxis,
    int currentMouseAlongAxis,
    int axisLength,
    float prevRatio,
    float nextRatio,
    float minGap = 0.05f) noexcept {
  const float raw = computeNewRatio(startRatio, startMouseAlongAxis,
                                     currentMouseAlongAxis, axisLength,
                                     0.0f, 1.0f);
  const float lo = prevRatio + minGap;
  const float hi = nextRatio - minGap;
  if (raw < lo) return lo;
  if (raw > hi) return hi;
  return raw;
}

}  // namespace fast_explorer::ui
```

- [ ] **Step 5: Run tests to verify pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R ComputeNewRatio
```

Expected: all 7 cases PASS.

- [ ] **Step 6: Commit**

```powershell
git add src/ui/splitter-resize.h tests/splitter-resize-tests.cpp CMakeLists.txt
git commit -m "feat(ui): pure ratio recompute helpers for splitter drag"
```

---

### Task 5: PaneLayoutResult + computePaneLayout for Single / Dual_V / Dual_H

This expands `pane-layout.{h,cpp}`. The legacy `computePaneRects` function is left in place (still used by `MainWindow`); a future task removes it once all callers are migrated.

**Files:**
- Modify: `src/ui/pane-layout.h`
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`

- [ ] **Step 1: Failing tests**

Append to `tests/pane-layout-tests.cpp`:
```cpp
#include "ui/splitter-ratios.h"

using fast_explorer::core::LayoutPreset;
using fast_explorer::ui::computePaneLayout;
using fast_explorer::ui::defaultRatiosFor;
using fast_explorer::ui::PaneLayoutResult;
using fast_explorer::ui::SplitterOrientation;

FE_TEST_CASE(ComputePaneLayout_Single_FullArea) {
  const auto out = computePaneLayout(LayoutPreset::Single,
                                     defaultRatiosFor(LayoutPreset::Single),
                                     1280, 800, /*top*/ 0, /*bottom*/ 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{1});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{0});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0, 0, 1280, 778));
}

FE_TEST_CASE(ComputePaneLayout_DualV_HalfSplit_OneVerticalSplitter) {
  const auto out = computePaneLayout(LayoutPreset::Dual_V,
                                     defaultRatiosFor(LayoutPreset::Dual_V),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{2});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{1});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0,   0, 640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 640, 0, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
}

FE_TEST_CASE(ComputePaneLayout_DualH_HalfSplit_OneHorizontalSplitter) {
  const auto out = computePaneLayout(LayoutPreset::Dual_H,
                                     defaultRatiosFor(LayoutPreset::Dual_H),
                                     1280, 800, 0, 22);
  FE_ASSERT_EQ(out.slotCount, std::size_t{2});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{1});
  // 800 - 22 = 778; half = 389.
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0, 0,   1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 0, 389, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Horizontal);
}
```

- [ ] **Step 2: Run, expect compile failure** (`computePaneLayout` undefined).

```powershell
cmake --build build --config Release --target core-tests
```

- [ ] **Step 3: Add types to `src/ui/pane-layout.h`**

Below the `PaneLayoutRects` struct, add:
```cpp
enum class SplitterOrientation : std::uint8_t { Vertical = 0, Horizontal = 1 };

struct SplitterRect {
  RECT hitRect{};                          // includes grab padding
  RECT visualRect{};                       // 1-px hairline location
  SplitterOrientation orient{SplitterOrientation::Vertical};
  std::uint8_t ratioId{0};
};

struct PaneLayoutResult {
  std::array<RECT, 4> slots{};
  std::array<SplitterRect, 3> splitters{};
  std::size_t slotCount{0};
  std::size_t splitterCount{0};
};

[[nodiscard]] PaneLayoutResult computePaneLayout(
    fast_explorer::core::LayoutPreset preset,
    const struct SplitterRatios& ratios,    // forward-declared via the include below
    int clientWidth,
    int clientHeight,
    int reservedTop,
    int reservedBottom) noexcept;
```

Make sure `pane-layout.h` `#include`s `"core/layout-preset.h"` and `"ui/splitter-ratios.h"`. Bump the `std::array<RECT, 2>` in the existing `PaneLayoutRects` definition to `std::array<RECT, 2>` (unchanged — legacy struct survives until removed in Task 37).

- [ ] **Step 4: Implementation in `src/ui/pane-layout.cpp`**

Add helper + Single/Dual cases. Add `#include "ui/splitter-ratios.h"` at the top.

```cpp
namespace {

constexpr int kSplitterThicknessDip = 1;     // 1-px visual hairline
constexpr int kSplitterGrabHalfDip  = 2;     // 2 px on each side -> 4-5 px grab

// Carves a vertical splitter into a horizontal band at the given x.
// Returns the splitter rect with hit area and visual edge populated.
SplitterRect makeVerticalSplitter(int x, int top, int bottom,
                                  std::uint8_t ratioId) noexcept {
  SplitterRect s;
  s.orient = SplitterOrientation::Vertical;
  s.ratioId = ratioId;
  s.hitRect = {x - kSplitterGrabHalfDip, top,
               x + kSplitterGrabHalfDip + kSplitterThicknessDip, bottom};
  s.visualRect = {x, top, x + kSplitterThicknessDip, bottom};
  return s;
}

SplitterRect makeHorizontalSplitter(int y, int left, int right,
                                    std::uint8_t ratioId) noexcept {
  SplitterRect s;
  s.orient = SplitterOrientation::Horizontal;
  s.ratioId = ratioId;
  s.hitRect = {left, y - kSplitterGrabHalfDip,
               right, y + kSplitterGrabHalfDip + kSplitterThicknessDip};
  s.visualRect = {left, y, right, y + kSplitterThicknessDip};
  return s;
}

}  // namespace

PaneLayoutResult computePaneLayout(fast_explorer::core::LayoutPreset preset,
                                   const SplitterRatios& ratios,
                                   int clientWidth,
                                   int clientHeight,
                                   int reservedTop,
                                   int reservedBottom) noexcept {
  using P = fast_explorer::core::LayoutPreset;
  PaneLayoutResult out{};

  const int top = reservedTop;
  const int bot = clientHeight - reservedBottom;
  if (clientWidth <= 0 || bot <= top) return out;
  const int W = clientWidth;
  const int totalH = bot - top;
  (void)totalH;

  switch (preset) {
    case P::Single: {
      out.slots[0] = {0, top, W, bot};
      out.slotCount = 1;
      return out;
    }
    case P::Dual_V: {
      const int x = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
      out.slots[0] = {0, top, x, bot};
      out.slots[1] = {x, top, W, bot};
      out.splitters[0] = makeVerticalSplitter(x, top, bot, 0);
      out.slotCount = 2;
      out.splitterCount = 1;
      return out;
    }
    case P::Dual_H: {
      const int y = top + static_cast<int>(static_cast<float>(totalH) *
                                            ratios.ratios[0]);
      out.slots[0] = {0, top, W, y};
      out.slots[1] = {0, y,   W, bot};
      out.splitters[0] = makeHorizontalSplitter(y, 0, W, 0);
      out.slotCount = 2;
      out.splitterCount = 1;
      return out;
    }
    default:
      // Tri_* / Quad_* implemented in later tasks.
      return out;
  }
}
```

- [ ] **Step 5: Run tests to verify pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R ComputePaneLayout
```

Expected: 3 cases PASS (Single, DualV, DualH).

- [ ] **Step 6: Commit**

```powershell
git add src/ui/pane-layout.h src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout for Single / Dual_V / Dual_H"
```

---

### Task 6: computePaneLayout — Tri_A (left-full + right top/bottom)

**Files:**
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`

- [ ] **Step 1: Failing test**

Append:
```cpp
FE_TEST_CASE(ComputePaneLayout_TriA_LeftFull_RightStacked_TwoSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Tri_A,
                                     defaultRatiosFor(LayoutPreset::Tri_A),
                                     1280, 800, 0, 22);
  // Defaults: rV=0.4 -> seam at 512; rH_right=0.5 -> right column seam at midY.
  // totalH = 778; midY = 0 + int(778 * 0.5) = 389.
  FE_ASSERT_EQ(out.slotCount, std::size_t{3});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{2});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0, 512, 778));   // left full
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 512,   0, 1280, 389));  // right top
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 512, 389, 1280, 778));  // right bottom
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
}
```

- [ ] **Step 2: Run, verify fail**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R TriA
```

Expected: FAIL (slotCount==0 since switch default returns empty).

- [ ] **Step 3: Implementation in `src/ui/pane-layout.cpp`**

Add `case P::Tri_A:` to the switch:
```cpp
case P::Tri_A: {
  const int x   = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
  const int ry  = top + static_cast<int>(static_cast<float>(totalH) *
                                          ratios.ratios[1]);
  out.slots[0] = {0, top, x, bot};      // left full
  out.slots[1] = {x, top, W, ry};       // right top
  out.slots[2] = {x, ry,  W, bot};      // right bottom
  out.splitters[0] = makeVerticalSplitter(x, top, bot, 0);
  out.splitters[1] = makeHorizontalSplitter(ry, x, W, 1);
  out.slotCount = 3;
  out.splitterCount = 2;
  return out;
}
```

- [ ] **Step 4: Run tests to verify pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R TriA
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Tri_A"
```

---

### Task 7: computePaneLayout — Tri_B (top-full + bottom left/right)

**Files:**
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`

- [ ] **Step 1: Failing test**

Append:
```cpp
FE_TEST_CASE(ComputePaneLayout_TriB_TopFull_BottomSplit_TwoSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Tri_B,
                                     defaultRatiosFor(LayoutPreset::Tri_B),
                                     1280, 800, 0, 22);
  // Defaults: rH=0.4 -> top half ends at int(778 * 0.4) = 311.
  // rV_bottom=0.5 -> bottom-row seam at 640.
  FE_ASSERT_EQ(out.slotCount, std::size_t{3});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{2});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0, 1280, 311));   // top full
  FE_ASSERT_TRUE(rectEquals(out.slots[1],   0, 311,  640, 778));   // bottom left
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 640, 311, 1280, 778));   // bottom right
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
}
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implementation**

Add `case P::Tri_B:`:
```cpp
case P::Tri_B: {
  const int y  = top + static_cast<int>(static_cast<float>(totalH) *
                                         ratios.ratios[0]);
  const int bx = static_cast<int>(static_cast<float>(W) * ratios.ratios[1]);
  out.slots[0] = {0, top, W, y};         // top full
  out.slots[1] = {0, y,   bx, bot};      // bottom left
  out.slots[2] = {bx, y,  W,  bot};      // bottom right
  out.splitters[0] = makeHorizontalSplitter(y, 0, W, 0);
  out.splitters[1] = makeVerticalSplitter(bx, y, bot, 1);
  out.slotCount = 3;
  out.splitterCount = 2;
  return out;
}
```

- [ ] **Step 4: Run tests, verify pass.**

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Tri_B"
```

---

### Task 8: computePaneLayout — Tri_C (3 vertical columns, cumulative)

**Files:**
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`

- [ ] **Step 1: Failing test**

```cpp
FE_TEST_CASE(ComputePaneLayout_TriC_ThreeColumns_TwoVerticalSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Tri_C,
                                     defaultRatiosFor(LayoutPreset::Tri_C),
                                     1281, 800, 0, 22);
  // 1281 * 0.333 = 426, 1281 * 0.667 = 854 (truncated).
  FE_ASSERT_EQ(out.slotCount, std::size_t{3});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{2});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  426, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 426,   0,  854, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 854,   0, 1281, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
}
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implementation**

```cpp
case P::Tri_C: {
  const int x0 = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
  const int x1 = static_cast<int>(static_cast<float>(W) * ratios.ratios[1]);
  out.slots[0] = {0,  top, x0, bot};
  out.slots[1] = {x0, top, x1, bot};
  out.slots[2] = {x1, top, W,  bot};
  out.splitters[0] = makeVerticalSplitter(x0, top, bot, 0);
  out.splitters[1] = makeVerticalSplitter(x1, top, bot, 1);
  out.slotCount = 3;
  out.splitterCount = 2;
  return out;
}
```

- [ ] **Step 4: Run, verify pass.**

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Tri_C"
```

---

### Task 9: computePaneLayout — Quad_A (2×2 grid)

**Files:**
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`

- [ ] **Step 1: Failing test**

```cpp
FE_TEST_CASE(ComputePaneLayout_QuadA_2x2Grid_ThreeSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_A,
                                     defaultRatiosFor(LayoutPreset::Quad_A),
                                     1280, 800, 0, 22);
  // rV=0.5 (x=640), rH_left=0.5 (y_left=389), rH_right=0.5 (y_right=389).
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  640, 389));   // top-left
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 640,   0, 1280, 389));   // top-right
  FE_ASSERT_TRUE(rectEquals(out.slots[2],   0, 389,  640, 778));   // bot-left
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 640, 389, 1280, 778));   // bot-right
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[2].ratioId, std::uint8_t{2});
}
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implementation**

```cpp
case P::Quad_A: {
  const int x   = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
  const int yL  = top + static_cast<int>(static_cast<float>(totalH) *
                                          ratios.ratios[1]);
  const int yR  = top + static_cast<int>(static_cast<float>(totalH) *
                                          ratios.ratios[2]);
  out.slots[0] = {0, top, x, yL};
  out.slots[1] = {x, top, W, yR};
  out.slots[2] = {0, yL,  x, bot};
  out.slots[3] = {x, yR,  W, bot};
  out.splitters[0] = makeVerticalSplitter(x, top, bot, 0);
  out.splitters[1] = makeHorizontalSplitter(yL, 0, x, 1);
  out.splitters[2] = makeHorizontalSplitter(yR, x, W, 2);
  out.slotCount = 4;
  out.splitterCount = 3;
  return out;
}
```

- [ ] **Step 4: Run, verify pass.**

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Quad_A (2x2)"
```

---

### Task 10: computePaneLayout — Quad_B (4 columns, cumulative)

**Files:**
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`

- [ ] **Step 1: Failing test**

```cpp
FE_TEST_CASE(ComputePaneLayout_QuadB_FourColumns_ThreeVerticalSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_B,
                                     defaultRatiosFor(LayoutPreset::Quad_B),
                                     1280, 800, 0, 22);
  // 1280 * 0.25 = 320, 1280 * 0.5 = 640, 1280 * 0.75 = 960.
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  320, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 320,   0,  640, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 640,   0,  960, 778));
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 960,   0, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[0].ratioId, std::uint8_t{0});
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].ratioId, std::uint8_t{1});
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[2].ratioId, std::uint8_t{2});
}
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implementation**

```cpp
case P::Quad_B: {
  const int x0 = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
  const int x1 = static_cast<int>(static_cast<float>(W) * ratios.ratios[1]);
  const int x2 = static_cast<int>(static_cast<float>(W) * ratios.ratios[2]);
  out.slots[0] = {0,  top, x0, bot};
  out.slots[1] = {x0, top, x1, bot};
  out.slots[2] = {x1, top, x2, bot};
  out.slots[3] = {x2, top, W,  bot};
  out.splitters[0] = makeVerticalSplitter(x0, top, bot, 0);
  out.splitters[1] = makeVerticalSplitter(x1, top, bot, 1);
  out.splitters[2] = makeVerticalSplitter(x2, top, bot, 2);
  out.slotCount = 4;
  out.splitterCount = 3;
  return out;
}
```

- [ ] **Step 4: Run, verify pass.**

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Quad_B (4 columns)"
```

---

### Task 11: computePaneLayout — Quad_C (4 rows, cumulative)

- [ ] **Step 1: Failing test**

```cpp
FE_TEST_CASE(ComputePaneLayout_QuadC_FourRows_ThreeHorizontalSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_C,
                                     defaultRatiosFor(LayoutPreset::Quad_C),
                                     1280, 800, 0, 22);
  // totalH = 778; 0.25 -> y=194, 0.5 -> 389, 0.75 -> 583.
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0], 0,   0, 1280, 194));
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 0, 194, 1280, 389));
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 0, 389, 1280, 583));
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 0, 583, 1280, 778));
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Horizontal);
}
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implementation**

```cpp
case P::Quad_C: {
  const int y0 = top + static_cast<int>(static_cast<float>(totalH) *
                                         ratios.ratios[0]);
  const int y1 = top + static_cast<int>(static_cast<float>(totalH) *
                                         ratios.ratios[1]);
  const int y2 = top + static_cast<int>(static_cast<float>(totalH) *
                                         ratios.ratios[2]);
  out.slots[0] = {0, top, W, y0};
  out.slots[1] = {0, y0,  W, y1};
  out.slots[2] = {0, y1,  W, y2};
  out.slots[3] = {0, y2,  W, bot};
  out.splitters[0] = makeHorizontalSplitter(y0, 0, W, 0);
  out.splitters[1] = makeHorizontalSplitter(y1, 0, W, 1);
  out.splitters[2] = makeHorizontalSplitter(y2, 0, W, 2);
  out.slotCount = 4;
  out.splitterCount = 3;
  return out;
}
```

- [ ] **Step 4: Run, verify pass.**

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Quad_C (4 rows)"
```

---

### Task 12: computePaneLayout — Quad_D (left-full + right 3-stack)

- [ ] **Step 1: Failing test**

```cpp
FE_TEST_CASE(ComputePaneLayout_QuadD_LeftFull_RightThreeStack_ThreeSplitters) {
  const auto out = computePaneLayout(LayoutPreset::Quad_D,
                                     defaultRatiosFor(LayoutPreset::Quad_D),
                                     1280, 800, 0, 22);
  // rV=0.5 -> x=640. rH1=0.333, rH2=0.667 of totalH=778 -> y1=259, y2=518.
  FE_ASSERT_EQ(out.slotCount, std::size_t{4});
  FE_ASSERT_EQ(out.splitterCount, std::size_t{3});
  FE_ASSERT_TRUE(rectEquals(out.slots[0],   0,   0,  640, 778));   // left full
  FE_ASSERT_TRUE(rectEquals(out.slots[1], 640,   0, 1280, 259));   // right top
  FE_ASSERT_TRUE(rectEquals(out.slots[2], 640, 259, 1280, 518));   // right mid
  FE_ASSERT_TRUE(rectEquals(out.slots[3], 640, 518, 1280, 778));   // right bot
  FE_ASSERT_EQ(out.splitters[0].orient, SplitterOrientation::Vertical);
  FE_ASSERT_EQ(out.splitters[1].orient, SplitterOrientation::Horizontal);
  FE_ASSERT_EQ(out.splitters[2].orient, SplitterOrientation::Horizontal);
}
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implementation**

```cpp
case P::Quad_D: {
  const int x  = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
  const int y1 = top + static_cast<int>(static_cast<float>(totalH) *
                                         ratios.ratios[1]);
  const int y2 = top + static_cast<int>(static_cast<float>(totalH) *
                                         ratios.ratios[2]);
  out.slots[0] = {0, top, x, bot};
  out.slots[1] = {x, top, W, y1};
  out.slots[2] = {x, y1,  W, y2};
  out.slots[3] = {x, y2,  W, bot};
  out.splitters[0] = makeVerticalSplitter(x, top, bot, 0);
  out.splitters[1] = makeHorizontalSplitter(y1, x, W, 1);
  out.splitters[2] = makeHorizontalSplitter(y2, x, W, 2);
  out.slotCount = 4;
  out.splitterCount = 3;
  return out;
}
```

- [ ] **Step 4: Run, verify pass.**

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-layout.cpp tests/pane-layout-tests.cpp
git commit -m "feat(ui): computePaneLayout Quad_D (left + right 3-stack)"
```

---

### Task 13: Verify all `computePaneLayout` tests pass + full ctest baseline

- [ ] **Step 1: Run full test suite to confirm no regressions**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release
```

Expected: previously-passing 558 cases + new ~36 (10 slotCount + 7 cycle + 6 ratios + 7 resize + 10 layout cases) PASS.

- [ ] **Step 2: Commit a checkpoint tag (no code change, only marker)**

No commit required — this is a verification gate. Move on to Phase 2.

---

## Phase 2 — SessionState v5 + migration

### Task 14: Grow SessionState struct (compile-only; new fields default-initialised)

**Files:**
- Modify: `src/core/settings-store.h`

- [ ] **Step 1: Modify `src/core/settings-store.h`**

Replace the `SessionState` struct with:
```cpp
#include "core/layout-preset.h"
#include "ui/splitter-ratios.h"
#include <array>

// ... existing using-aliases ...

constexpr std::size_t kMaxPanes = 4;

struct SessionState {
  std::wstring lastPath;
  std::wstring secondPath;
  int windowX = kSettingsUseDefault;
  int windowY = kSettingsUseDefault;
  int windowWidth = kSettingsUseDefault;
  int windowHeight = kSettingsUseDefault;

  // Legacy fields (v4) - retained for migration source. Writer no
  // longer emits layout_mode / orientation in v5; these are derived
  // from preset on read.
  LayoutMode layoutMode = LayoutMode::Single;
  LayoutOrientation orientation = LayoutOrientation::Vertical;

  // v5 fields
  std::array<std::wstring, kMaxPanes> panePaths{};
  std::size_t paneCount = 1;
  std::size_t activePane = 0;
  fast_explorer::core::LayoutPreset preset =
      fast_explorer::core::LayoutPreset::Single;
  std::array<fast_explorer::ui::SplitterRatios,
             fast_explorer::core::kLayoutPresetCount> ratiosPerPreset{};

  bool showHidden = false;
  bool showExtensions = true;
};
```

Note: `ratiosPerPreset` is value-initialised to all-zero; `loadSessionState` populates defaults via `defaultRatiosFor` after parsing.

- [ ] **Step 2: Run build to confirm no syntax errors**

```powershell
cmake --build build --config Release --target FastExplorer core-tests
```

Expected: clean build, no test failures (struct change is additive).

- [ ] **Step 3: Commit**

```powershell
git add src/core/settings-store.h
git commit -m "feat(core): SessionState v5 fields (panePaths, preset, ratiosPerPreset)"
```

---

### Task 15: settings-store v5 writer

**Files:**
- Modify: `src/core/settings-store.cpp`
- Modify: `tests/settings-store-tests.cpp` (write a test first)

- [ ] **Step 1: Failing test — round-trip v5 fields**

Append to `tests/settings-store-tests.cpp`:
```cpp
using fast_explorer::core::LayoutPreset;
using fast_explorer::ui::defaultRatiosFor;

FE_TEST_CASE(SettingsStore_v5_RoundTrip_QuadA) {
  SessionState in{};
  in.windowX = 100; in.windowY = 50; in.windowWidth = 1280; in.windowHeight = 800;
  in.panePaths[0] = L"C:/a";
  in.panePaths[1] = L"D:/b";
  in.panePaths[2] = L"E:/c";
  in.panePaths[3] = L"F:/d";
  in.paneCount = 4;
  in.activePane = 1;
  in.preset = LayoutPreset::Quad_A;
  in.ratiosPerPreset[size_t(LayoutPreset::Quad_A)] = {{0.6f, 0.5f, 0.5f}};

  const std::wstring path = uniqueTempPath(L"fe_v5_qa");      // existing helper
  FE_ASSERT_TRUE(saveSessionState(path, in));

  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.paneCount, std::size_t{4});
  FE_ASSERT_EQ(out.activePane, std::size_t{1});
  FE_ASSERT_EQ(out.preset, LayoutPreset::Quad_A);
  FE_ASSERT_WSTREQ(out.panePaths[0], L"C:/a");
  FE_ASSERT_WSTREQ(out.panePaths[1], L"D:/b");
  FE_ASSERT_WSTREQ(out.panePaths[2], L"E:/c");
  FE_ASSERT_WSTREQ(out.panePaths[3], L"F:/d");
  const auto& r = out.ratiosPerPreset[size_t(LayoutPreset::Quad_A)];
  FE_ASSERT_TRUE(r.ratios[0] > 0.59f && r.ratios[0] < 0.61f);
  DeleteFileW(path.c_str());
}
```

Check the existing `tests/settings-store-tests.cpp` for the actual temp-path helper name and adjust if it differs (e.g., `makeTempSettingsPath`). If no helper exists, inline a 4-line generator using `GetTempPathW` + `GetTickCount64`.

- [ ] **Step 2: Run, verify fail**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R SettingsStore_v5
```

Expected: FAIL on either `paneCount` or `preset` round-trip (v5 not yet implemented).

- [ ] **Step 3: Add v5 key constants + writer logic in `src/core/settings-store.cpp`**

Add near the existing key constants:
```cpp
constexpr std::string_view kKeySchemaVersion {"schema_version"};
constexpr std::string_view kKeyPanePaths     {"pane_paths"};
constexpr std::string_view kKeyPaneCount     {"pane_count"};
constexpr std::string_view kKeyActivePane    {"active_pane"};
constexpr std::string_view kKeyPreset        {"preset"};
constexpr std::string_view kKeyRatios        {"ratios"};

constexpr int kSchemaVersionCurrent = 5;

constexpr std::string_view presetLabel(LayoutPreset p) noexcept {
  switch (p) {
    case LayoutPreset::Single: return "single";
    case LayoutPreset::Dual_V: return "dual_v";
    case LayoutPreset::Dual_H: return "dual_h";
    case LayoutPreset::Tri_A:  return "tri_a";
    case LayoutPreset::Tri_B:  return "tri_b";
    case LayoutPreset::Tri_C:  return "tri_c";
    case LayoutPreset::Quad_A: return "quad_a";
    case LayoutPreset::Quad_B: return "quad_b";
    case LayoutPreset::Quad_C: return "quad_c";
    case LayoutPreset::Quad_D: return "quad_d";
  }
  return "single";
}

constexpr LayoutPreset presetFromLabel(std::string_view s) noexcept {
  if (s == "single") return LayoutPreset::Single;
  if (s == "dual_v") return LayoutPreset::Dual_V;
  if (s == "dual_h") return LayoutPreset::Dual_H;
  if (s == "tri_a")  return LayoutPreset::Tri_A;
  if (s == "tri_b")  return LayoutPreset::Tri_B;
  if (s == "tri_c")  return LayoutPreset::Tri_C;
  if (s == "quad_a") return LayoutPreset::Quad_A;
  if (s == "quad_b") return LayoutPreset::Quad_B;
  if (s == "quad_c") return LayoutPreset::Quad_C;
  if (s == "quad_d") return LayoutPreset::Quad_D;
  return LayoutPreset::Single;  // lenient
}
```

Add a writer helper for the pane paths array:
```cpp
void appendKeyPanePaths(std::string& out,
                       const std::array<std::wstring, kMaxPanes>& paths,
                       bool first) {
  appendKeyHeader(out, kKeyPanePaths, first);
  out.push_back('[');
  for (std::size_t i = 0; i < paths.size(); ++i) {
    if (i > 0) out.append(", ");
    appendJsonEscapedString(out, paths[i]);
  }
  out.push_back(']');
}

void appendKeyRatios(std::string& out,
                     const std::array<SplitterRatios,
                                       kLayoutPresetCount>& ratios,
                     bool first) {
  appendKeyHeader(out, kKeyRatios, first);
  out.append("{\n");
  bool firstEntry = true;
  for (std::size_t i = 0; i < ratios.size(); ++i) {
    const auto p = static_cast<LayoutPreset>(i);
    const auto& r = ratios[i];
    if (r.ratios[0] == 0.0f && r.ratios[1] == 0.0f && r.ratios[2] == 0.0f) {
      continue;  // Skip presets the user never touched (untouched stays default on load).
    }
    if (!firstEntry) out.append(",\n");
    firstEntry = false;
    out.append("    \"");
    out.append(presetLabel(p));
    out.append("\": [");
    const std::size_t n = std::min<std::size_t>(3, slotCountForPreset(p) - 1);
    for (std::size_t j = 0; j < n; ++j) {
      if (j > 0) out.append(", ");
      char buf[32];
      const int len = std::snprintf(buf, sizeof(buf), "%.6f", r.ratios[j]);
      if (len > 0) out.append(buf, static_cast<std::size_t>(len));
    }
    out.push_back(']');
  }
  out.append("\n  }");
}
```

Replace the body of `saveSessionState` so it emits v5 only:
```cpp
bool saveSessionState(const std::wstring& path, const SessionState& state) {
  if (path.empty()) return false;
  if (!ensureParentDir(path)) return false;
  std::string out;
  out.reserve(kSerializedReserveHint);
  appendKeyInt       (out, kKeySchemaVersion, kSchemaVersionCurrent, /*first*/ true);
  appendKeyInt       (out, kKeyWindowX,       state.windowX,         false);
  appendKeyInt       (out, kKeyWindowY,       state.windowY,         false);
  appendKeyInt       (out, kKeyWindowW,       state.windowWidth,     false);
  appendKeyInt       (out, kKeyWindowH,       state.windowHeight,    false);
  appendKeyPanePaths (out, state.panePaths,                          false);
  appendKeyInt       (out, kKeyPaneCount,     static_cast<int>(state.paneCount),  false);
  appendKeyInt       (out, kKeyActivePane,    static_cast<int>(state.activePane), false);
  appendKeyRawString (out, kKeyPreset,        presetLabel(state.preset),          false);
  appendKeyRatios    (out, state.ratiosPerPreset,                                  false);
  appendKeyInt       (out, kKeyShowHidden,    state.showHidden     ? 1 : 0,        false);
  appendKeyInt       (out, kKeyShowExtensions,state.showExtensions ? 1 : 0,        false);
  out.append("\n}\n");

  const std::wstring temp = path + L".tmp";
  if (!writeWholeFile(temp, out)) { DeleteFileW(temp.c_str()); return false; }
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}
```

- [ ] **Step 4: Build (test will still fail until reader supports v5 — Task 16)**

```powershell
cmake --build build --config Release --target core-tests
```

Expected: build OK; tests still fail (round-trip needs the reader).

- [ ] **Step 5: Commit (writer done; reader next)**

```powershell
git add src/core/settings-store.cpp tests/settings-store-tests.cpp
git commit -m "feat(core): settings-store v5 writer (schema_version=5)"
```

---

### Task 16: settings-store v5 reader (no migration yet)

**Files:**
- Modify: `src/core/settings-store.cpp`

- [ ] **Step 1: Extend `JsonReader::parseValueInto` to recognize v5 keys.**

Add new branches in the existing if/else chain. Locate the chain inside `parseValueInto`:

```cpp
if (key == kKeySchemaVersion) {
  int v = 0;
  if (!parseIntInto(v)) return false;
  schemaVersion_ = v;            // new field on JsonReader; see below
  return true;
}
if (key == kKeyPaneCount) {
  int v = 0;
  if (!parseIntInto(v)) return false;
  if (v < 1) v = 1;
  if (v > static_cast<int>(kMaxPanes)) v = static_cast<int>(kMaxPanes);
  state.paneCount = static_cast<std::size_t>(v);
  return true;
}
if (key == kKeyActivePane) {
  int v = 0;
  if (!parseIntInto(v)) return false;
  if (v < 0) v = 0;
  state.activePane = static_cast<std::size_t>(v);
  return true;
}
if (key == kKeyPreset) {
  std::string raw;
  if (!parseStringInto(raw)) return false;
  state.preset = presetFromLabel(raw);
  return true;
}
if (key == kKeyPanePaths) {
  return parsePanePathsArrayInto(state);
}
if (key == kKeyRatios) {
  return parseRatiosObjectInto(state);
}
```

- [ ] **Step 2: Add the `schemaVersion_` member + the two array-parsing helpers**

Inside `class JsonReader`, add a private `int schemaVersion_ = 0;` field and accessor `int schemaVersion() const noexcept { return schemaVersion_; }`. Add helpers:

```cpp
bool parsePanePathsArrayInto(SessionState& state) {
  skipWs();
  if (!consume('[')) return false;
  skipWs();
  std::size_t idx = 0;
  if (peek() == ']') { pos_++; return true; }
  while (true) {
    skipWs();
    std::string raw;
    if (!parseStringInto(raw)) return false;
    if (idx < state.panePaths.size()) {
      state.panePaths[idx] = widenUtf8(raw);
    }
    idx++;
    skipWs();
    if (consume(',')) continue;
    if (consume(']')) return true;
    return false;
  }
}

bool parseRatiosObjectInto(SessionState& state) {
  skipWs();
  if (!consume('{')) return false;
  skipWs();
  if (peek() == '}') { pos_++; return true; }
  while (true) {
    skipWs();
    std::string key;
    if (!parseStringInto(key)) return false;
    skipWs();
    if (!consume(':')) return false;
    skipWs();
    if (!consume('[')) return false;
    const LayoutPreset p = presetFromLabel(key);
    auto& dst = state.ratiosPerPreset[static_cast<std::size_t>(p)];
    std::size_t idx = 0;
    skipWs();
    if (peek() == ']') { pos_++; }
    else {
      while (true) {
        float v = 0.0f;
        if (!parseFloatInto(v)) return false;
        if (idx < dst.ratios.size()) dst.ratios[idx] = v;
        idx++;
        skipWs();
        if (consume(',')) { skipWs(); continue; }
        if (consume(']')) break;
        return false;
      }
    }
    skipWs();
    if (consume(',')) continue;
    if (consume('}')) return true;
    return false;
  }
}

bool parseFloatInto(float& out) {
  skipWs();
  const std::size_t start = pos_;
  if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) pos_++;
  bool sawDot = false, sawDigit = false;
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    if (c >= '0' && c <= '9') { sawDigit = true; pos_++; continue; }
    if (c == '.' && !sawDot)  { sawDot = true; pos_++; continue; }
    break;
  }
  if (!sawDigit) return false;
  const std::string tok(text_.substr(start, pos_ - start));
  try { out = std::stof(tok); } catch (...) { return false; }
  return true;
}
```

- [ ] **Step 3: After parsing in `loadSessionState`, populate untouched preset ratios with defaults**

In `loadSessionState`, after the successful `reader.parseObjectInto(state)`:
```cpp
for (std::size_t i = 0; i < state.ratiosPerPreset.size(); ++i) {
  auto& r = state.ratiosPerPreset[i];
  if (r.ratios[0] == 0.0f && r.ratios[1] == 0.0f && r.ratios[2] == 0.0f) {
    r = defaultRatiosFor(static_cast<LayoutPreset>(i));
  }
}
```

This means a freshly-loaded file has every preset's defaults pre-populated, with user-touched ones overriding.

- [ ] **Step 4: Run tests, verify v5 round-trip passes**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R SettingsStore_v5
```

Expected: `SettingsStore_v5_RoundTrip_QuadA` PASS. Existing tests (v4 files) — see Task 17 below for migration.

- [ ] **Step 5: Commit**

```powershell
git add src/core/settings-store.cpp
git commit -m "feat(core): settings-store v5 reader"
```

---

### Task 17: v4 → v5 migration

**Files:**
- Modify: `src/core/settings-store.cpp`
- Modify: `tests/settings-store-tests.cpp`

- [ ] **Step 1: Failing test for migration**

Append:
```cpp
FE_TEST_CASE(SettingsStore_v4_to_v5_DualV_Migrates) {
  // Hand-craft a v4-style settings file (no schema_version key).
  const std::string v4Json =
    "{\n"
    "  \"last_path\": \"C:/old\",\n"
    "  \"window_x\": 1, \"window_y\": 2, \"window_w\": 800, \"window_h\": 600,\n"
    "  \"layout_mode\": \"dual\",\n"
    "  \"second_path\": \"D:/old\",\n"
    "  \"orientation\": \"vertical\",\n"
    "  \"view_show_hidden\": 0,\n"
    "  \"view_show_extensions\": 1\n"
    "}\n";
  const std::wstring path = uniqueTempPath(L"fe_v4_dv");
  writeRawBytes(path, v4Json);                      // assumed test helper; if absent, write inline using CreateFile/WriteFile

  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.paneCount, std::size_t{2});
  FE_ASSERT_EQ(out.preset, LayoutPreset::Dual_V);
  FE_ASSERT_WSTREQ(out.panePaths[0], L"C:/old");
  FE_ASSERT_WSTREQ(out.panePaths[1], L"D:/old");
  // Migration also retains v4 fields for callers still reading them.
  FE_ASSERT_WSTREQ(out.lastPath,   L"C:/old");
  FE_ASSERT_WSTREQ(out.secondPath, L"D:/old");
  DeleteFileW(path.c_str());
}

FE_TEST_CASE(SettingsStore_v4_to_v5_Single_Migrates) {
  const std::string v4Json =
    "{\n"
    "  \"last_path\": \"X:/proj\",\n"
    "  \"layout_mode\": \"single\"\n"
    "}\n";
  const std::wstring path = uniqueTempPath(L"fe_v4_s");
  writeRawBytes(path, v4Json);
  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.paneCount, std::size_t{1});
  FE_ASSERT_EQ(out.preset, LayoutPreset::Single);
  FE_ASSERT_WSTREQ(out.panePaths[0], L"X:/proj");
  DeleteFileW(path.c_str());
}

FE_TEST_CASE(SettingsStore_v4_to_v5_DualH_Migrates) {
  const std::string v4Json =
    "{\n"
    "  \"last_path\": \"A:/\",\n"
    "  \"second_path\": \"B:/\",\n"
    "  \"layout_mode\": \"dual\",\n"
    "  \"orientation\": \"horizontal\"\n"
    "}\n";
  const std::wstring path = uniqueTempPath(L"fe_v4_dh");
  writeRawBytes(path, v4Json);
  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.preset, LayoutPreset::Dual_H);
  DeleteFileW(path.c_str());
}
```

If the suite has no `writeRawBytes` helper, add one at the top of the file:
```cpp
namespace {
void writeRawBytes(const std::wstring& path, std::string_view bytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wrote = 0;
  WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr);
  CloseHandle(h);
}
}
```

- [ ] **Step 2: Run, expect fail**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R SettingsStore_v4_to_v5
```

Expected: all three FAIL — paneCount, preset, panePaths not populated from v4 fields.

- [ ] **Step 3: Add migration in `loadSessionState`**

After `reader.parseObjectInto(state)` and the ratios-default fill, before `return true`:
```cpp
// v4 -> v5 migration: trigger when schema_version is missing (==0) or older.
if (reader.schemaVersion() < kSchemaVersionCurrent) {
  // Pane paths from legacy lastPath/secondPath.
  if (state.panePaths[0].empty() && !state.lastPath.empty()) {
    state.panePaths[0] = state.lastPath;
  }
  if (state.panePaths[1].empty() && !state.secondPath.empty()) {
    state.panePaths[1] = state.secondPath;
  }
  // Preset + paneCount derived from layoutMode + orientation.
  if (state.layoutMode == LayoutMode::Dual) {
    state.preset = (state.orientation == LayoutOrientation::Horizontal)
                       ? LayoutPreset::Dual_H : LayoutPreset::Dual_V;
    state.paneCount = 2;
  } else {
    state.preset = LayoutPreset::Single;
    state.paneCount = 1;
  }
  state.activePane = 0;
}
```

- [ ] **Step 4: Run, verify migration tests pass**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R SettingsStore_v4_to_v5
```

Expected: 3 PASS.

- [ ] **Step 5: Commit**

```powershell
git add src/core/settings-store.cpp tests/settings-store-tests.cpp
git commit -m "feat(core): v4 -> v5 SessionState migration"
```

---

### Task 18: Settings-store regression sweep

- [ ] **Step 1: Run the full settings-store test set + the full core-tests suite**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R SettingsStore
ctest --test-dir build --output-on-failure -C Release
```

Expected: all SettingsStore_* pass; total failing == 0.

If any older test was asserting v4 JSON-shape directly (e.g., expecting `layout_mode` key on disk), update its expectation to v5 shape — those tests are checking serialization, not behavior, so adjust to the new keys. Recompile + rerun. Commit any adjustments:

```powershell
git add tests/settings-store-tests.cpp
git commit -m "test(settings): update on-disk shape assertions to schema v5"
```

(No commit if no edit was needed.)

---

## Phase 3 — PaneManager API generalisation

### Task 19: PaneManager openPane / closePane (append/pop)

**Files:**
- Modify: `src/ui/pane-manager.h`
- Modify: `src/ui/pane-manager.cpp`

- [ ] **Step 1: Replace `pane-manager.h` body (between `class PaneManager` and end of class)**

```cpp
class PaneManager {
 public:
  PaneManager();
  ~PaneManager();
  PaneManager(const PaneManager&) = delete;
  PaneManager& operator=(const PaneManager&) = delete;
  PaneManager(PaneManager&&) = delete;
  PaneManager& operator=(PaneManager&&) = delete;

  std::size_t openInitial(HWND host);

  // Appends a slot at index count() (so resulting count == old count + 1).
  // initialPath falls back to active().path() when empty.
  // No-op + returns count() when count() >= kMaxPanes.
  std::size_t openPane(HWND host, const std::wstring& initialPath);

  // Pops the last slot. No-op when count() == 1.
  // activeIndex_ clamped to count() - 1 after the pop.
  void closePane() noexcept;

  bool setActive(std::size_t idx) noexcept;
  [[nodiscard]] std::size_t count() const noexcept;
  [[nodiscard]] std::size_t activeIndex() const noexcept;
  [[nodiscard]] PaneController& active();
  [[nodiscard]] const PaneController& active() const;
  [[nodiscard]] PaneController& at(std::size_t i);
  [[nodiscard]] const PaneController& at(std::size_t i) const;

  static constexpr std::size_t kMaxPanes = 4;

 private:
  std::vector<std::unique_ptr<PaneController>> panes_;
  std::size_t activeIndex_ = 0;
};
```

`chooseSecondPaneInitialPath` (the existing free function) stays in the header for now — callers still need it.

- [ ] **Step 2: Update `src/ui/pane-manager.cpp`**

Replace the body:
```cpp
#include "ui/pane-manager.h"

#include "ui/pane-controller.h"

namespace fast_explorer::ui {

PaneManager::PaneManager() = default;
PaneManager::~PaneManager() = default;

std::size_t PaneManager::openInitial(HWND host) {
  panes_.push_back(std::make_unique<PaneController>(host, 0));
  activeIndex_ = 0;
  return 0;
}

std::size_t PaneManager::openPane(HWND host,
                                  const std::wstring& initialPath) {
  if (panes_.size() >= kMaxPanes) {
    return panes_.size();
  }
  const std::size_t newIdx = panes_.size();
  auto pane = std::make_unique<PaneController>(host, newIdx);
  const std::wstring& path = initialPath.empty()
                                 ? panes_[activeIndex_]->currentPath()
                                 : initialPath;
  if (!path.empty()) {
    pane->openFolder(path);
  }
  panes_.push_back(std::move(pane));
  return newIdx;
}

void PaneManager::closePane() noexcept {
  if (panes_.size() <= 1) return;
  panes_.pop_back();
  if (activeIndex_ >= panes_.size()) {
    activeIndex_ = panes_.size() - 1;
  }
}

bool PaneManager::setActive(std::size_t idx) noexcept {
  if (idx >= panes_.size()) return false;
  activeIndex_ = idx;
  return true;
}

std::size_t PaneManager::count() const noexcept {
  return panes_.size();
}
std::size_t PaneManager::activeIndex() const noexcept {
  return activeIndex_;
}
PaneController&       PaneManager::active()       { return *panes_[activeIndex_]; }
const PaneController& PaneManager::active() const { return *panes_[activeIndex_]; }
PaneController&       PaneManager::at(std::size_t i)       { return *panes_[i]; }
const PaneController& PaneManager::at(std::size_t i) const { return *panes_[i]; }

}  // namespace fast_explorer::ui
```

Verify `PaneController` has a `currentPath()` accessor returning `const std::wstring&` — if it's named differently (e.g., `path()` or `folderPath()`), use the actual name and update the fallback in `openPane`.

- [ ] **Step 3: Build — main-window.cpp will fail to compile**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: compile errors in `main-window.cpp` — `openSecond`, `closeSecond`, `isDual` no longer exist.

- [ ] **Step 4: Adapt `main-window.cpp` call sites — preserve current behavior with the new API**

Search/replace these call patterns in `src/ui/main-window.cpp`:

- `paneManager_->openSecond(hwnd_)` → `paneManager_->openPane(hwnd_, secondPath)` (`secondPath` is the existing local already used in the dual-mode entry path)
- `paneManager_->closeSecond()` → `paneManager_->closePane()`
- `paneManager_->isDual()` → `(paneManager_->count() > 1)`

Build until clean:
```powershell
cmake --build build --config Release --target FastExplorer
```

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-manager.h src/ui/pane-manager.cpp src/ui/main-window.cpp
git commit -m "refactor(ui): PaneManager append/pop API (openPane/closePane)"
```

---

### Task 20: PaneManager tests for 4-slot operations

**Files:**
- Modify: `tests/pane-manager-tests.cpp`

- [ ] **Step 1: Look up existing test patterns**

Open `tests/pane-manager-tests.cpp` and observe the existing test setup pattern (how it creates a host HWND and constructs `PaneManager`). The test harness already wires a window class registration helper — reuse the same.

- [ ] **Step 2: Add failing tests**

Append:
```cpp
FE_TEST_CASE(PaneManager_OpenPane_AppendsUpToFour) {
  HWND host = createHiddenTestHost();   // helper from existing tests
  PaneManager mgr;
  mgr.openInitial(host);
  FE_ASSERT_EQ(mgr.count(), std::size_t{1});

  FE_ASSERT_EQ(mgr.openPane(host, L""), std::size_t{1});
  FE_ASSERT_EQ(mgr.count(), std::size_t{2});

  FE_ASSERT_EQ(mgr.openPane(host, L""), std::size_t{2});
  FE_ASSERT_EQ(mgr.count(), std::size_t{3});

  FE_ASSERT_EQ(mgr.openPane(host, L""), std::size_t{3});
  FE_ASSERT_EQ(mgr.count(), std::size_t{4});

  // 5th openPane is a no-op + returns count().
  FE_ASSERT_EQ(mgr.openPane(host, L""), std::size_t{4});
  FE_ASSERT_EQ(mgr.count(), std::size_t{4});

  DestroyWindow(host);
}

FE_TEST_CASE(PaneManager_ClosePane_PopsLast_NeverDropsBelowOne) {
  HWND host = createHiddenTestHost();
  PaneManager mgr;
  mgr.openInitial(host);
  mgr.openPane(host, L"");
  mgr.openPane(host, L"");
  FE_ASSERT_EQ(mgr.count(), std::size_t{3});

  mgr.closePane();
  FE_ASSERT_EQ(mgr.count(), std::size_t{2});

  mgr.closePane();
  FE_ASSERT_EQ(mgr.count(), std::size_t{1});

  mgr.closePane();  // no-op
  FE_ASSERT_EQ(mgr.count(), std::size_t{1});

  DestroyWindow(host);
}

FE_TEST_CASE(PaneManager_ClosePane_ClampsActiveIndex) {
  HWND host = createHiddenTestHost();
  PaneManager mgr;
  mgr.openInitial(host);
  mgr.openPane(host, L"");
  mgr.openPane(host, L"");
  mgr.openPane(host, L"");
  FE_ASSERT_TRUE(mgr.setActive(3));
  FE_ASSERT_EQ(mgr.activeIndex(), std::size_t{3});

  mgr.closePane();   // drops slot 3, activeIndex_ -> 2
  FE_ASSERT_EQ(mgr.activeIndex(), std::size_t{2});
  mgr.closePane();   // drops slot 2, activeIndex_ -> 1
  FE_ASSERT_EQ(mgr.activeIndex(), std::size_t{1});

  DestroyWindow(host);
}
```

If no `createHiddenTestHost()` helper exists, inline a minimal `CreateWindowExW(0, L"STATIC", ...)` call before each test — reuse the pattern from the existing tests in this file.

- [ ] **Step 3: Run, verify pass (these test new behaviors that should already work after Task 19's implementation)**

```powershell
cmake --build build --config Release --target core-tests
ctest --test-dir build --output-on-failure -C Release -R PaneManager
```

Expected: all PASS. If `OpenSecond`/`CloseSecond` tests existed and now reference removed methods, delete those obsolete tests (their replacements are above).

- [ ] **Step 4: Commit**

```powershell
git add tests/pane-manager-tests.cpp
git commit -m "test(ui): PaneManager 4-slot append/pop coverage"
```

---

## Phase 4 — Grow MainWindow per-slot arrays

### Task 21: Per-slot arrays size 2 → 4

**Files:**
- Modify: `src/ui/main-window.h`
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: In `src/ui/main-window.h`, change every `std::array<T, 2>` to `std::array<T, 4>`**

Specifically these members (search the file for `, 2>`):
```cpp
std::array<HWND, 4> listViews_{nullptr, nullptr, nullptr, nullptr};
std::array<HWND, 4> addressBars_{};
std::array<HWND, 4> addressDropdownBtns_{};
std::array<std::unique_ptr<PaneToolbarRow>, 4> paneToolbarRows_;
std::array<IDropTarget*, 4> dropTargets_{};
std::array<bool, 4> firstBatchSeen_{};
std::array<std::unique_ptr<class IconCacheCoordinator>, 4> iconCoords_;
std::array<std::unique_ptr<SelectionSync>, 4> selectionSyncs_;
std::array<std::unique_ptr<LabelEditController>, 4> labelEdits_;
```

Initializers: zero-value-init the trailing slots as shown.

- [ ] **Step 2: Build**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: clean build. All `for (i = 0; i < listViews_.size(); ++i)` loops now iterate 0..3; slots 2 and 3 stay null/empty because `paneManager_->count() <= 2` at runtime — existing guards `if (listViews_[i] == nullptr) continue;` already gate the body.

- [ ] **Step 3: Smoke-run the existing 2-pane case to confirm zero behavior change**

```powershell
ctest --test-dir build --output-on-failure -C Release
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```powershell
git add src/ui/main-window.h
git commit -m "refactor(ui): per-pane HWND arrays grow to size 4 (capacity only)"
```

---

## Phase 5 — PaneSplitter HWND class

### Task 22: SplitterContext + PaneSplitter class registration + WM_PAINT (hairline) + WM_SETCURSOR + WM_ERASEBKGND

**Files:**
- Create: `src/ui/pane-splitter.h`
- Create: `src/ui/pane-splitter.cpp`
- Modify: `CMakeLists.txt` (add both files to `FastExplorer` target source list)

- [ ] **Step 1: Add the header**

`src/ui/pane-splitter.h`:
```cpp
#pragma once

#include <windows.h>

#include <functional>

#include "ui/pane-layout.h"
#include "ui/splitter-ratios.h"

namespace fast_explorer::ui {

struct SplitterContext {
  SplitterOrientation orient = SplitterOrientation::Vertical;
  std::uint8_t ratioId = 0;
  SplitterRatios* ratios = nullptr;        // borrowed, MainWindow owns
  std::function<void()> onCommit;          // -> MainWindow::relayout
};

class PaneSplitter {
 public:
  // Register the window class. Idempotent. Returns false only on
  // unexpected RegisterClassEx failure.
  static bool registerClass(HINSTANCE instance) noexcept;

  // Create one splitter HWND as a child of `parent`. `ctx` is moved
  // into a heap allocation owned by the HWND; freed at WM_NCDESTROY.
  static HWND create(HINSTANCE instance, HWND parent, SplitterContext ctx);

  static constexpr const wchar_t* kClassName = L"FastExplorer.PaneSplitter";
};

}  // namespace fast_explorer::ui
```

- [ ] **Step 2: Add `src/ui/pane-splitter.{h,cpp}` to `CMakeLists.txt` `FastExplorer` source list.**

- [ ] **Step 3: Implement the class registration + paint + cursor + erase**

`src/ui/pane-splitter.cpp`:
```cpp
#include "ui/pane-splitter.h"

#include <windowsx.h>

namespace fast_explorer::ui {

namespace {

LRESULT CALLBACK splitterWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  auto* ctx = reinterpret_cast<SplitterContext*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
      auto* incoming = static_cast<SplitterContext*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(incoming));
      return TRUE;
    }
    case WM_NCDESTROY: {
      delete ctx;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SETCURSOR: {
      const HCURSOR c = LoadCursorW(nullptr,
          (ctx && ctx->orient == SplitterOrientation::Vertical)
              ? IDC_SIZEWE : IDC_SIZENS);
      SetCursor(c);
      return TRUE;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      // Theme-aware hairline color. The dark-mode pref is read via
      // the same helper MainWindow uses for listview grid lines; for
      // simplicity here we use COLOR_3DSHADOW so the splitter follows
      // the system shadow tone. Dark mode is wired in a later task
      // via custom paint if visual review demands it.
      const HBRUSH brush = GetSysColorBrush(COLOR_3DSHADOW);
      if (ctx && ctx->orient == SplitterOrientation::Vertical) {
        // Hairline is the visualRect-equivalent inside the HWND: a
        // 1-px column centered horizontally. HWND width is the grab
        // padding * 2 + 1 px.
        const int midX = (rc.right - rc.left) / 2;
        RECT line = {midX, rc.top, midX + 1, rc.bottom};
        FillRect(hdc, &line, brush);
      } else {
        const int midY = (rc.bottom - rc.top) / 2;
        RECT line = {rc.left, midY, rc.right, midY + 1};
        FillRect(hdc, &line, brush);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

bool PaneSplitter::registerClass(HINSTANCE instance) noexcept {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = splitterWndProc;
  wc.hInstance = instance;
  wc.hCursor = nullptr;        // owned by WM_SETCURSOR
  wc.hbrBackground = nullptr;  // owned by WM_ERASEBKGND/WM_PAINT
  wc.lpszClassName = kClassName;
  // Idempotent: ignore "already registered" on subsequent calls.
  const ATOM atom = RegisterClassExW(&wc);
  if (atom != 0) return true;
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND PaneSplitter::create(HINSTANCE instance, HWND parent,
                          SplitterContext ctx) {
  auto* heapCtx = new SplitterContext(std::move(ctx));
  HWND hwnd = CreateWindowExW(
      0, kClassName, L"",
      WS_CHILD | WS_CLIPSIBLINGS,
      0, 0, 0, 0,                // positioned by relayout()
      parent, nullptr, instance, heapCtx);
  if (hwnd == nullptr) {
    delete heapCtx;
  }
  return hwnd;
}

}  // namespace fast_explorer::ui
```

- [ ] **Step 4: Build, verify no errors**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: clean build.

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-splitter.h src/ui/pane-splitter.cpp CMakeLists.txt
git commit -m "feat(ui): PaneSplitter class skeleton (paint + cursor)"
```

---

### Task 23: PaneSplitter drag — WM_LBUTTONDOWN / WM_MOUSEMOVE / WM_LBUTTONUP with XOR ghost line

**Files:**
- Modify: `src/ui/pane-splitter.cpp`

- [ ] **Step 1: Extend `splitterWndProc`**

Add per-HWND drag state alongside the heap `SplitterContext`. Simplest path: add fields to the struct.

In `pane-splitter.h`, extend `SplitterContext`:
```cpp
struct SplitterContext {
  SplitterOrientation orient = SplitterOrientation::Vertical;
  std::uint8_t ratioId = 0;
  SplitterRatios* ratios = nullptr;
  std::function<void()> onCommit;

  // Drag state (populated by WM_LBUTTONDOWN, cleared on WM_LBUTTONUP).
  bool dragging = false;
  POINT dragStartScreen{0, 0};
  int   axisLengthAtStart = 0;       // parent client extent along drag axis
  float ratioAtStart = 0.0f;
  int   lastGhostPos = -1;           // last drawn ghost position (parent client coords)
};
```

- [ ] **Step 2: Add the message handlers in `splitterWndProc`**

Insert after `WM_PAINT`:
```cpp
    case WM_LBUTTONDOWN: {
      if (!ctx || !ctx->ratios) return 0;
      SetCapture(hwnd);
      ctx->dragging = true;
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ClientToScreen(hwnd, &pt);
      ctx->dragStartScreen = pt;
      ctx->ratioAtStart = ctx->ratios->ratios[ctx->ratioId];

      HWND parent = GetParent(hwnd);
      RECT prc; GetClientRect(parent, &prc);
      ctx->axisLengthAtStart =
          (ctx->orient == SplitterOrientation::Vertical)
              ? (prc.right - prc.left)
              : (prc.bottom - prc.top);

      // Initial ghost at current position (parent client coords)
      POINT pcli = pt; ScreenToClient(parent, &pcli);
      ctx->lastGhostPos =
          (ctx->orient == SplitterOrientation::Vertical) ? pcli.x : pcli.y;
      drawGhost(parent, ctx->orient, ctx->lastGhostPos);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!ctx || !ctx->dragging) return 0;
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ClientToScreen(hwnd, &pt);
      HWND parent = GetParent(hwnd);
      POINT pcli = pt; ScreenToClient(parent, &pcli);
      const int newPos =
          (ctx->orient == SplitterOrientation::Vertical) ? pcli.x : pcli.y;

      // XOR-erase previous, draw new.
      drawGhost(parent, ctx->orient, ctx->lastGhostPos);
      drawGhost(parent, ctx->orient, newPos);
      ctx->lastGhostPos = newPos;
      return 0;
    }
    case WM_LBUTTONUP: {
      if (!ctx || !ctx->dragging) return 0;
      // Erase final ghost.
      HWND parent = GetParent(hwnd);
      drawGhost(parent, ctx->orient, ctx->lastGhostPos);
      ReleaseCapture();
      ctx->dragging = false;

      // Commit the new ratio.
      const float newRatio =
          static_cast<float>(ctx->lastGhostPos) /
          static_cast<float>(std::max(1, ctx->axisLengthAtStart));
      // Clamp identical to computeNewRatio's [0.1, 0.9] band; the
      // cumulative-clamp variant is applied by the relayout consumer
      // when the preset is Quad_B/C/D.
      const float clamped =
          newRatio < 0.1f ? 0.1f : (newRatio > 0.9f ? 0.9f : newRatio);
      ctx->ratios->ratios[ctx->ratioId] = clamped;
      if (ctx->onCommit) ctx->onCommit();
      ctx->lastGhostPos = -1;
      return 0;
    }
    case WM_CAPTURECHANGED: {
      if (ctx && ctx->dragging) {
        // Capture stolen (e.g., Alt-Tab); discard the in-flight ghost.
        drawGhost(GetParent(hwnd), ctx->orient, ctx->lastGhostPos);
        ctx->dragging = false;
        ctx->lastGhostPos = -1;
      }
      return 0;
    }
```

- [ ] **Step 3: Add the `drawGhost` helper above `splitterWndProc`**

```cpp
namespace {

// XOR-draw a 2-px ghost line on the parent's client DC. Drawing
// twice at the same position erases (XOR is self-inverse). When
// pos is negative the call is a no-op so we can pair WM_MOUSEMOVE's
// erase+draw symmetrically even on the first frame.
void drawGhost(HWND parent, SplitterOrientation orient, int pos) {
  if (pos < 0) return;
  HDC dc = GetDC(parent);
  if (!dc) return;
  const int prevRop = SetROP2(dc, R2_NOTXORPEN);
  HPEN pen = CreatePen(PS_SOLID, 2, RGB(160, 160, 160));
  HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
  RECT prc; GetClientRect(parent, &prc);
  if (orient == SplitterOrientation::Vertical) {
    MoveToEx(dc, pos, prc.top, nullptr);
    LineTo(dc, pos, prc.bottom);
  } else {
    MoveToEx(dc, prc.left, pos, nullptr);
    LineTo(dc, prc.right, pos);
  }
  SelectObject(dc, oldPen);
  DeleteObject(pen);
  SetROP2(dc, prevRop);
  ReleaseDC(parent, dc);
}

}  // namespace
```

Place the namespace block above `splitterWndProc` so `drawGhost` is visible to it.

- [ ] **Step 4: Build, verify no errors**

```powershell
cmake --build build --config Release --target FastExplorer
```

- [ ] **Step 5: Commit**

```powershell
git add src/ui/pane-splitter.h src/ui/pane-splitter.cpp
git commit -m "feat(ui): PaneSplitter drag with XOR ghost line"
```

---

## Phase 6 — MainWindow integration

### Task 24: Add `lastDualPreset_`, `preset_`, `ratiosPerPreset_`, splitter HWND array, and `enterLayout` declaration

**Files:**
- Modify: `src/ui/main-window.h`

- [ ] **Step 1: Add member declarations**

Add `#include "core/layout-preset.h"` and `#include "ui/splitter-ratios.h"` near the top.

Add these members at the bottom of the private section (next to `orientation_`):
```cpp
fast_explorer::core::LayoutPreset preset_ =
    fast_explorer::core::LayoutPreset::Single;
fast_explorer::core::LayoutPreset lastDualPreset_ =
    fast_explorer::core::LayoutPreset::Dual_V;
std::array<fast_explorer::ui::SplitterRatios,
           fast_explorer::core::kLayoutPresetCount> ratiosPerPreset_{};
std::array<HWND, 3> splitterHwnds_{nullptr, nullptr, nullptr};
```

Add a public method declaration alongside `enterDualMode/enterSingleMode`:
```cpp
// Switches to the given preset, opening or closing slots as needed.
// Idempotent when `target == preset_` (still triggers relayout to
// pick up any external ratio changes).
void enterLayout(fast_explorer::core::LayoutPreset target);
```

Add a private helper:
```cpp
void initRatiosToDefaults() noexcept;
```

- [ ] **Step 2: Build (still no implementation; method bodies in next task)**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: unresolved `enterLayout` / `initRatiosToDefaults` linker errors. Continue — fixed in Task 25/26.

- [ ] **Step 3: Skip commit until method bodies exist (Task 25/26).**

---

### Task 25: Pre-create 3 splitter HWNDs in onCreate; init ratios to defaults

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Add `#include "ui/pane-splitter.h"` to `src/ui/main-window.cpp`.**

- [ ] **Step 2: Implement `initRatiosToDefaults`**

```cpp
void MainWindow::initRatiosToDefaults() noexcept {
  using fast_explorer::core::LayoutPreset;
  for (std::size_t i = 0; i < ratiosPerPreset_.size(); ++i) {
    ratiosPerPreset_[i] = defaultRatiosFor(static_cast<LayoutPreset>(i));
  }
}
```

- [ ] **Step 3: Call it + register class + create the three splitter HWNDs at the bottom of `onCreate(hwnd)`**

In `MainWindow::onCreate(HWND hwnd)`, after the existing pane-0 setup and before the return:
```cpp
  initRatiosToDefaults();

  // Register the splitter class once (idempotent).
  PaneSplitter::registerClass(instance_);

  // Pre-create 3 splitter HWNDs in a hidden state. Visible/visible-and-
  // positioned by relayout() based on the active preset's splitterCount.
  for (std::size_t i = 0; i < splitterHwnds_.size(); ++i) {
    SplitterContext ctx;
    ctx.orient = SplitterOrientation::Vertical;     // updated by relayout
    ctx.ratioId = 0;                                // updated by relayout
    ctx.ratios = &ratiosPerPreset_[static_cast<std::size_t>(preset_)];
    ctx.onCommit = [this]() { this->relayout(); };
    splitterHwnds_[i] = PaneSplitter::create(instance_, hwnd, std::move(ctx));
    if (splitterHwnds_[i]) ShowWindow(splitterHwnds_[i], SW_HIDE);
  }
```

- [ ] **Step 4: Build**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: clean build (still no `enterLayout` body — linker error). Continue.

- [ ] **Step 5: Skip commit until next task.**

---

### Task 26: Implement `relayout()` to use `computePaneLayout`; reposition slots + splitters

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate the existing `relayout()` body (or the `onSize` body that does the layout work).**

The current code calls `computePaneRects(...)` inside `onSize`. Replace that block with `computePaneLayout(preset_, ratiosPerPreset_[...], ...)` and add per-splitter reposition.

- [ ] **Step 2: Replace the layout block**

```cpp
void MainWindow::relayout() {
  if (!hwnd_) return;
  RECT client;
  GetClientRect(hwnd_, &client);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;

  RECT statusRect{};
  if (statusBar_) GetWindowRect(statusBar_, &statusRect);
  const int statusH = statusBar_
                          ? (statusRect.bottom - statusRect.top)
                          : 0;
  applyStatusParts(clientW);

  using fast_explorer::ui::computePaneLayout;
  const auto& ratios =
      ratiosPerPreset_[static_cast<std::size_t>(preset_)];
  const auto result = computePaneLayout(preset_, ratios,
                                         clientW, clientH,
                                         /*reservedTop*/ 0, statusH);

  // Position slot HWNDs (each slot internally hosts its toolbar row +
  // address bar + listview — current per-slot internal layout is
  // unchanged from the dual-mode path).
  for (std::size_t i = 0; i < listViews_.size(); ++i) {
    const RECT& r = result.slots[i];
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    const bool active = i < result.slotCount;
    HWND rowHwnd = paneToolbarRows_[i] ? paneToolbarRows_[i]->handle()
                                       : addressBars_[i];

    if (!active || w <= 0 || h <= 0) {
      if (rowHwnd) ShowWindow(rowHwnd, SW_HIDE);
      if (listViews_[i]) ShowWindow(listViews_[i], SW_HIDE);
      continue;
    }
    // The internal layout (toolbar row at top of slot rect, listview
    // below) is the same as the existing dual-mode placement, just
    // applied per-slot. Reuse the existing helper if extracted, or
    // inline as below.
    const int rowH = scaleForDpi(42, GetDpiForWindow(hwnd_));
    if (rowHwnd) {
      SetWindowPos(rowHwnd, nullptr, r.left, r.top, w, rowH,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      ShowWindow(rowHwnd, SW_SHOWNA);
    }
    if (listViews_[i]) {
      SetWindowPos(listViews_[i], nullptr, r.left, r.top + rowH,
                   w, h - rowH, SWP_NOZORDER | SWP_NOACTIVATE);
      ShowWindow(listViews_[i], SW_SHOWNA);
    }
  }

  // Position splitter HWNDs; hide unused ones.
  for (std::size_t i = 0; i < splitterHwnds_.size(); ++i) {
    HWND s = splitterHwnds_[i];
    if (!s) continue;
    if (i >= result.splitterCount) {
      ShowWindow(s, SW_HIDE);
      continue;
    }
    const auto& sp = result.splitters[i];
    // Update per-splitter context (orient + ratioId + ratios pointer
    // for the CURRENT preset).
    auto* ctx = reinterpret_cast<SplitterContext*>(
        GetWindowLongPtrW(s, GWLP_USERDATA));
    if (ctx) {
      ctx->orient = sp.orient;
      ctx->ratioId = sp.ratioId;
      ctx->ratios = &ratiosPerPreset_[static_cast<std::size_t>(preset_)];
    }
    SetWindowPos(s, HWND_TOP,
                 sp.hitRect.left, sp.hitRect.top,
                 sp.hitRect.right - sp.hitRect.left,
                 sp.hitRect.bottom - sp.hitRect.top,
                 SWP_NOACTIVATE);
    ShowWindow(s, SW_SHOWNA);
    InvalidateRect(s, nullptr, FALSE);
  }
}
```

- [ ] **Step 3: Replace the call in `onSize` with a single `relayout();`** (if the existing body inlines the layout, lift it into `relayout()` and have `onSize` just delegate).

- [ ] **Step 4: Build, verify clean**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: clean (still unresolved `enterLayout` — addressed in Task 27).

- [ ] **Step 5: Skip commit until enterLayout lands.**

---

### Task 27: Implement `enterLayout(preset)`

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Write the method**

```cpp
void MainWindow::enterLayout(fast_explorer::core::LayoutPreset target) {
  using fast_explorer::core::LayoutPreset;
  using fast_explorer::core::slotCountForPreset;
  if (!paneManager_) return;

  const std::size_t targetCount = slotCountForPreset(target);

  // Grow.
  while (paneManager_->count() < targetCount &&
         paneManager_->count() < PaneManager::kMaxPanes) {
    const std::size_t newIdx = paneManager_->openPane(hwnd_, L"");
    if (newIdx <= paneManager_->count() - 1) {
      // Mirror the existing dual-entry side effects for the new slot:
      // toolbar row, address bar, listview, drop target, coordinators.
      // Reuse the existing per-pane install path that today runs for
      // slot 1 in enterDualMode — extracted into a private
      // installPaneAt(idx) helper. If it does not yet exist, factor
      // out the dual-mode body before this task; see Task 21 notes.
      installPaneAt(newIdx);
    }
  }

  // Shrink (also tears down per-slot UI before releasing the pane).
  while (paneManager_->count() > targetCount) {
    const std::size_t idx = paneManager_->count() - 1;
    uninstallPaneAt(idx);
    paneManager_->closePane();
  }

  preset_ = target;
  if (target == LayoutPreset::Dual_V || target == LayoutPreset::Dual_H) {
    lastDualPreset_ = target;
  }
  if (paneManager_->activeIndex() >= targetCount) {
    paneManager_->setActive(targetCount - 1);
  }
  applyActivePaneAppearance();
  relayout();
}
```

`installPaneAt(idx)` / `uninstallPaneAt(idx)` are private helpers that bundle the existing per-pane HWND creation / teardown found inside the current `enterDualMode` and `enterSingleMode` for slot 1. If those helpers do not yet exist, extract them before writing the body of `enterLayout`:

1. Move the slot-1 toolbar-row creation, address-bar creation, drop-target registration, and coordinator installation into `installPaneAt(std::size_t idx)`.
2. Move the slot-1 teardown body into `uninstallPaneAt(std::size_t idx)`.

Both helpers take an index and use `listViews_[idx]`, `addressBars_[idx]`, etc. — same indexing as the existing slot-1 paths. (If creating list-view children isn't in `enterDualMode` today and lives in `onCreate` only, then `installPaneAt` must also create the list-view HWND for `idx`; copy that block from the slot-0 onCreate path with the index parameterised.)

- [ ] **Step 2: Build**

```powershell
cmake --build build --config Release --target FastExplorer
```

Expected: clean build. (If `installPaneAt`/`uninstallPaneAt` don't exist, declare + implement them first.)

- [ ] **Step 3: Commit Phase 6 batch**

```powershell
git add src/ui/main-window.h src/ui/main-window.cpp
git commit -m "feat(ui): enterLayout(preset) + splitter HWND pool + relayout via computePaneLayout"
```

---

### Task 28: Accel IDs + accel table + WM_COMMAND handlers (Ctrl+3, Ctrl+4)

**Files:**
- Modify: `src/ui/messages.h`
- Modify: `src/app/main.cpp`
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Add accel IDs**

In `src/ui/messages.h` near `kAccelLayoutDual`:
```cpp
inline constexpr WORD kAccelLayoutTri              = 116;  // Ctrl+3
inline constexpr WORD kAccelLayoutQuad             = 117;  // Ctrl+4
```

- [ ] **Step 2: Add accel table entries**

In `src/app/main.cpp`, in the `ACCEL` array near the existing layout entries:
```cpp
{FVIRTKEY | FCONTROL, '3', fast_explorer::ui::kAccelLayoutTri},
{FVIRTKEY | FCONTROL, '4', fast_explorer::ui::kAccelLayoutQuad},
```

- [ ] **Step 3: Handle the new commands in `MainWindow::onCommand`**

Find the existing `case kAccelLayoutSingle:` / `case kAccelLayoutDual:` block; add:
```cpp
case kAccelLayoutTri: {
  using fast_explorer::core::nextPresetInCycle;
  enterLayout(nextPresetInCycle(preset_, 3));
  return 0;
}
case kAccelLayoutQuad: {
  using fast_explorer::core::nextPresetInCycle;
  enterLayout(nextPresetInCycle(preset_, 4));
  return 0;
}
```

Update the existing `case kAccelLayoutSingle:` body to call `enterLayout(LayoutPreset::Single)` (drop direct calls to `enterSingleMode` if present).

Update the existing `case kAccelLayoutDual:` body:
```cpp
case kAccelLayoutDual: {
  using fast_explorer::core::LayoutPreset;
  if (preset_ == LayoutPreset::Dual_V || preset_ == LayoutPreset::Dual_H) {
    // Existing resolveLayoutToggle path — preserved.
    const bool dual = true;
    const auto t = resolveLayoutToggle(dual, orientation_,
                                        orientation_);  // same seam = exit
    if (t.action == LayoutAction::ExitToSingle) {
      enterLayout(LayoutPreset::Single);
    }
    return 0;
  }
  enterLayout(lastDualPreset_);
  return 0;
}
```

The Alt+V / Alt+H handlers stay unchanged; they only apply when `preset_` is `Dual_V` or `Dual_H` (gate with an `if`):
```cpp
case kAccelLayoutVerticalToggle:
case kAccelLayoutHorizontalToggle: {
  using fast_explorer::core::LayoutPreset;
  if (preset_ != LayoutPreset::Dual_V && preset_ != LayoutPreset::Dual_H) {
    return 0;
  }
  const auto pressed =
      LOWORD(wParam) == kAccelLayoutHorizontalToggle
          ? LayoutOrientation::Horizontal
          : LayoutOrientation::Vertical;
  const auto t = resolveLayoutToggle(true, orientation_, pressed);
  if (t.action == LayoutAction::ExitToSingle) {
    enterLayout(LayoutPreset::Single);
  } else if (t.action == LayoutAction::SwitchOrientation) {
    orientation_ = pressed;
    enterLayout(pressed == LayoutOrientation::Horizontal
                    ? LayoutPreset::Dual_H : LayoutPreset::Dual_V);
  }
  return 0;
}
```

- [ ] **Step 4: Build + run a smoke test**

```powershell
cmake --build build --config Release --target FastExplorer
Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue
Start-Process build\FastExplorer.exe
```

Manual check:
- App opens at single pane.
- Ctrl+3 → enters Tri_A (left full + right stack). Press Ctrl+3 again → Tri_B. Again → Tri_C. Again → Tri_A.
- Ctrl+4 → enters Quad_A (2×2). Press Ctrl+4 again → Quad_B. Again → C, D, A.
- Ctrl+1 → back to Single.
- Ctrl+2 → dual (last orientation).

`Stop-Process` the app between iterations to start clean if something gets stuck.

- [ ] **Step 5: Commit**

```powershell
git add src/ui/messages.h src/app/main.cpp src/ui/main-window.cpp
git commit -m "feat(ui): Ctrl+3 / Ctrl+4 cycle Tri_A..C / Quad_A..D"
```

---

### Task 29: SessionState restore on startup

**Files:**
- Modify: `src/ui/main-window.cpp` (the existing `restoreLayoutFromSession` body)

- [ ] **Step 1: Replace the body**

```cpp
void MainWindow::restoreLayoutFromSession(
    const fast_explorer::core::SessionState& state) {
  using fast_explorer::core::LayoutPreset;
  using fast_explorer::core::slotCountForPreset;
  if (!paneManager_) return;

  // Pull persisted ratios into the live store.
  ratiosPerPreset_ = state.ratiosPerPreset;

  const std::size_t targetCount = slotCountForPreset(state.preset);

  // Slot 0 was opened by onCreate on state.panePaths[0] (or lastPath
  // fallback). Open the rest with their persisted paths.
  for (std::size_t i = 1; i < targetCount; ++i) {
    const std::wstring& path = state.panePaths[i];
    paneManager_->openPane(hwnd_, path);
    installPaneAt(i);
  }

  preset_ = state.preset;
  if (preset_ == LayoutPreset::Dual_V || preset_ == LayoutPreset::Dual_H) {
    lastDualPreset_ = preset_;
  }
  if (state.activePane < targetCount) {
    paneManager_->setActive(state.activePane);
  }
  orientation_ = (preset_ == LayoutPreset::Dual_H)
                     ? LayoutOrientation::Horizontal
                     : LayoutOrientation::Vertical;
  applyActivePaneAppearance();
  relayout();
}
```

The slot-0 path comes from the existing `openFolder(...)` call right after `paneManager_->openInitial(hwnd)`. Make sure that call now picks `state.panePaths[0]` if non-empty (and falls back to `lastPath` for v4-migrated files — the migrator already populated `panePaths[0]` so a single check is enough).

- [ ] **Step 2: Build, run, verify**

```powershell
cmake --build build --config Release --target FastExplorer
Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue
Start-Process build\FastExplorer.exe
```

Manual: open Tri_A, navigate each pane to a distinct path, close the app, relaunch — Tri_A should restore with all three paths and the splitter positions preserved.

- [ ] **Step 3: Commit**

```powershell
git add src/ui/main-window.cpp
git commit -m "feat(ui): restore multi-pane layout + ratios from session"
```

---

### Task 30: SessionState capture on shutdown

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate where `capturedState_` is populated at `WM_DESTROY` (search for `capturedState_`).**

- [ ] **Step 2: Extend the population block**

```cpp
  capturedState_->panePaths.fill(std::wstring{});
  if (paneManager_) {
    for (std::size_t i = 0; i < paneManager_->count() && i < kMaxPanes; ++i) {
      capturedState_->panePaths[i] = paneManager_->at(i).currentPath();
    }
    capturedState_->paneCount = paneManager_->count();
    capturedState_->activePane = paneManager_->activeIndex();
  } else {
    capturedState_->paneCount = 1;
    capturedState_->activePane = 0;
  }
  capturedState_->preset = preset_;
  capturedState_->ratiosPerPreset = ratiosPerPreset_;
```

Keep legacy v4 fields populated too (for forward compat with code that might still read them — `MainWindow` capture is internal but the SessionState header still exposes the fields):
```cpp
  capturedState_->lastPath   = capturedState_->panePaths[0];
  capturedState_->secondPath = capturedState_->panePaths[1];
  capturedState_->layoutMode = capturedState_->paneCount > 1
                                   ? LayoutMode::Dual : LayoutMode::Single;
  capturedState_->orientation = orientation_;
```

- [ ] **Step 3: Build, run smoke**

```powershell
cmake --build build --config Release --target FastExplorer
Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue
Start-Process build\FastExplorer.exe
```

Manual: open Quad_A, drag splitters to non-default positions, close. Inspect `%LOCALAPPDATA%\FastExplorer\settings.json` — should now have `schema_version: 5`, `pane_count: 4`, `preset: "quad_a"`, and `ratios.quad_a` with the drag-adjusted values.

- [ ] **Step 4: Commit**

```powershell
git add src/ui/main-window.cpp
git commit -m "feat(ui): capture multi-pane state at WM_DESTROY"
```

---

## Phase 7 — Cleanup + verification

### Task 31: Remove legacy `computePaneRects` and `enterDualMode/enterSingleMode`

**Files:**
- Modify: `src/ui/pane-layout.h`
- Modify: `src/ui/pane-layout.cpp`
- Modify: `tests/pane-layout-tests.cpp`
- Modify: `src/ui/main-window.h`
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Grep for remaining callers**

```powershell
# from repo root
Select-String -Path "src","tests" -Pattern "computePaneRects|enterDualMode|enterSingleMode" -SimpleMatch
```

Expected: only the definitions themselves + this plan document.

- [ ] **Step 2: Delete `PaneLayoutRects` and `computePaneRects` from `src/ui/pane-layout.h` and `src/ui/pane-layout.cpp`.**

- [ ] **Step 3: Delete `enterDualMode` / `enterSingleMode` declarations + definitions in `main-window.{h,cpp}`** if they are still present after Task 27/28 left them as wrappers. If they were already removed, skip.

- [ ] **Step 4: Delete the now-orphaned tests from `tests/pane-layout-tests.cpp` that reference `computePaneRects` / `PaneLayoutRects`.** (Their replacements are the `computePaneLayout` tests added in Tasks 5–12.)

- [ ] **Step 5: Build + full ctest**

```powershell
cmake --build build --config Release --target FastExplorer core-tests
ctest --test-dir build --output-on-failure -C Release
```

Expected: clean build, all tests pass.

- [ ] **Step 6: Commit**

```powershell
git add src/ui/pane-layout.h src/ui/pane-layout.cpp tests/pane-layout-tests.cpp src/ui/main-window.h src/ui/main-window.cpp
git commit -m "refactor(ui): remove legacy computePaneRects + dual-only mode helpers"
```

---

### Task 32: Full verification — ctest + manual integration

- [ ] **Step 1: Run the full test suite**

```powershell
cmake --build build --config Release --target FastExplorer core-tests
ctest --test-dir build --output-on-failure -C Release
```

Expected: 558 previously-passing + new ~50 cases = ~608+ all PASS.

- [ ] **Step 2: Manual integration checklist (per `feedback_local_test_first` memory — UI changes get a local run before push)**

```powershell
Stop-Process -Name FastExplorer -ErrorAction SilentlyContinue
Start-Process build\FastExplorer.exe
```

Verify each item:

- [ ] Ctrl+1 from any state → Single.
- [ ] Ctrl+2 from Single → Dual_V; press Ctrl+2 again → Single.
- [ ] Alt+V / Alt+H from Dual_V/H → flips seam (existing behavior preserved).
- [ ] Ctrl+3 from Single → Tri_A; press Ctrl+3 → Tri_B → Tri_C → Tri_A (cycle).
- [ ] Ctrl+4 from any state → Quad_A; cycle → B → C → D → A.
- [ ] In each preset, every splitter:
  - cursor changes to `IDC_SIZEWE` / `IDC_SIZENS` on hover.
  - mouse-down + drag draws a ghost line that tracks the cursor.
  - mouse-up commits the new position; pane rects update once.
  - dragging beyond a neighbor (Quad_B/C/D) clamps to the neighbor band.
- [ ] Restart app — last preset + paths + splitter positions all restore.
- [ ] In Quad_A with 4 distinct deep folders open, scrolling each listview stays snappy (perf-tracker dump or just eye test — no visible frame drops).
- [ ] No leaked splitter HWNDs: cycle Ctrl+3/4 ~20 times — Task Manager → handle count stable.
- [ ] Old v4 settings file in `%LOCALAPPDATA%\FastExplorer\settings.json`: temporarily rename current `settings.json`, drop in a v4-shape file (or use a copy from before this branch), relaunch — pane 0 and pane 1 restore correctly, no crash.

- [ ] **Step 3: If any manual item fails, file the issue back to the relevant Task and fix.** Re-run Step 1 + Step 2 until clean.

- [ ] **Step 4: Commit any fixes from the manual sweep with focused messages (one bug = one commit).**

---

### Task 33: Final sweep — formatting, lints, docs

- [ ] **Step 1: `git status` should show no stray edits.**

```powershell
git status
```

- [ ] **Step 2: Tag-readiness check**

```powershell
# Confirm version-bump file (if any) — search for "v0.3.0"
Select-String -Path "src","docs" -Pattern "v0\.3\.0" -SimpleMatch
```

If a version constant lives in source (e.g., `src/app/version.h` or similar), bump to `v0.4.0` and commit:
```powershell
git add <version-file>
git commit -m "chore: bump version to v0.4.0"
```

- [ ] **Step 3: Push branch, open PR — do NOT tag yet.** User performs the tag manually per the release-flow memory (`git tag v0.4.0 && git push origin v0.4.0` only after PR merge).

---

## Risks / things to watch

- **Splitter hairline color in dark mode.** Task 22 uses `COLOR_3DSHADOW` which is theme-aware on Windows 10/11 but may render too dark on the Mica-backed window. If review flags it, add a custom-paint branch keyed to the same `applySystemTheme` dark-mode flag MainWindow already tracks. Pure UI tweak, no architecture impact.
- **`computeNewRatio` vs `computeNewCumulativeRatio` selection.** Task 23 commits via the non-cumulative `[0.1, 0.9]` clamp universally. For `Quad_B/C/D`, this is wrong (a middle splitter could pass a neighbor and produce inverted columns on next relayout). Before Task 32's manual cumulative-clamp test, extend `WM_LBUTTONUP` to pick the cumulative variant when `preset_ == Quad_B/C/D` and pass `prevRatio = ratios[ratioId-1]` (or 0.0f) / `nextRatio = ratios[ratioId+1]` (or 1.0f).
- **Per-pane state migration.** `showHidden` / `showExtensions` stay window-global in v0.4.0. When tabs ship, both move into `PaneController`. The settings JSON already keys them at the top level — no schema change to plan for here.
- **Drop targets for slots 2/3.** Existing `dropTargets_[i]` registration follows the same pattern as slot 1. `installPaneAt(idx)` must call the same `RegisterDragDrop` block; `uninstallPaneAt(idx)` must call `RevokeDragDrop`. Easy to forget — explicit in Task 27.

## Out of scope (per spec)

- Tab UI inside each pane.
- Splitter snap behavior.
- Drag-to-swap-panes.
- Network folder tree expansion (Shell API limitation; pre-existing).

---

## Self-review notes (post-write checklist)

- ✅ Each spec section maps to at least one task. The "Architecture", "PaneManager API", "Layout engine", "PaneSplitter", "Persistence — SessionState v5", "Keybindings", "MainWindow integration", "Testing strategy", "Files touched", and "Out of scope" sections all have explicit task coverage (Tasks 1-32, with Out of scope captured in the trailing section here).
- ✅ No "TBD" / "TODO" / "fill in later" / "similar to Task N" in any step.
- ✅ Types consistent across tasks: `PaneLayoutResult`, `SplitterRect`, `SplitterOrientation`, `SplitterRatios`, `SplitterContext`, `LayoutPreset`, `kMaxPanes`, `kLayoutPresetCount` all referenced by their final names from first use.
- ✅ Every code-touching step includes the actual code.
- ✅ Every test step shows the actual assertion bodies.
- ✅ Risks the engineer can't infer from the spec are called out (`computeNewCumulativeRatio` selection, dark-mode color, drop-target wiring).
