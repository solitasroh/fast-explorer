#pragma once

#include <cstdint>
#include <cstddef>

namespace fast_explorer::core {

// Named layout presets for the multi-pane view. The integer values
// are stable: they are serialized into settings.json (schema v5,
// preset string mapping lives in settings-store.cpp), so reordering
// or removing entries is a breaking change. Add new entries at the
// end of the enum and grow kLayoutPresetCount accordingly.
enum class LayoutPreset : std::uint8_t {
  Single  = 0,   // 1 pane: full window
  Dual_V  = 1,   // 2 panes: left | right
  Dual_H  = 2,   // 2 panes: top / bottom
  Tri_A   = 3,   // 3 panes: left-full + right top/bottom
  Tri_B   = 4,   // 3 panes: top-full + bottom left/right
  Tri_C   = 5,   // 3 panes: 3 vertical columns
  Quad_A  = 6,   // 4 panes: 2x2 grid
  Quad_B  = 7,   // 4 panes: 4 vertical columns
  Quad_C  = 8,   // 4 panes: 4 horizontal rows
  Quad_D  = 9,   // 4 panes: left-full + right 3-stack
};

inline constexpr std::size_t kLayoutPresetCount = 10;

static_assert(static_cast<std::size_t>(LayoutPreset::Quad_D) + 1 ==
                  kLayoutPresetCount,
              "kLayoutPresetCount must equal the number of LayoutPreset values");

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

}  // namespace fast_explorer::core
