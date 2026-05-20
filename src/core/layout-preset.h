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
