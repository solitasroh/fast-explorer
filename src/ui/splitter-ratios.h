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
