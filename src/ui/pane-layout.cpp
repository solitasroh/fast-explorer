#include "ui/pane-layout.h"

namespace {

constexpr int kSplitterThicknessDip = 1;
constexpr int kSplitterGrabHalfDip  = 2;

fast_explorer::ui::SplitterRect makeVerticalSplitter(int x, int top, int bottom,
                                                     std::uint8_t ratioId) noexcept {
  fast_explorer::ui::SplitterRect s;
  s.orient = fast_explorer::ui::SplitterOrientation::Vertical;
  s.ratioId = ratioId;
  s.hitRect = {x - kSplitterGrabHalfDip, top,
               x + kSplitterGrabHalfDip + kSplitterThicknessDip, bottom};
  s.visualRect = {x, top, x + kSplitterThicknessDip, bottom};
  return s;
}

fast_explorer::ui::SplitterRect makeHorizontalSplitter(int y, int left, int right,
                                                       std::uint8_t ratioId) noexcept {
  fast_explorer::ui::SplitterRect s;
  s.orient = fast_explorer::ui::SplitterOrientation::Horizontal;
  s.ratioId = ratioId;
  s.hitRect = {left, y - kSplitterGrabHalfDip,
               right, y + kSplitterGrabHalfDip + kSplitterThicknessDip};
  s.visualRect = {left, y, right, y + kSplitterThicknessDip};
  return s;
}

}  // namespace

namespace fast_explorer::ui {

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
    case P::Tri_A: {
      const int x   = static_cast<int>(static_cast<float>(W) * ratios.ratios[0]);
      const int ry  = top + static_cast<int>(static_cast<float>(totalH) *
                                              ratios.ratios[1]);
      out.slots[0] = {0, top, x, bot};
      out.slots[1] = {x, top, W, ry};
      out.slots[2] = {x, ry,  W, bot};
      out.splitters[0] = makeVerticalSplitter(x, top, bot, 0);
      out.splitters[1] = makeHorizontalSplitter(ry, x, W, 1);
      out.slotCount = 3;
      out.splitterCount = 2;
      return out;
    }
    case P::Tri_B: {
      const int y  = top + static_cast<int>(static_cast<float>(totalH) *
                                             ratios.ratios[0]);
      const int bx = static_cast<int>(static_cast<float>(W) * ratios.ratios[1]);
      out.slots[0] = {0, top, W, y};
      out.slots[1] = {0, y,   bx, bot};
      out.slots[2] = {bx, y,  W,  bot};
      out.splitters[0] = makeHorizontalSplitter(y, 0, W, 0);
      out.splitters[1] = makeVerticalSplitter(bx, y, bot, 1);
      out.slotCount = 3;
      out.splitterCount = 2;
      return out;
    }
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
    default:
      // Defensive fallthrough — unreachable for any valid LayoutPreset.
      return out;
  }
}

}  // namespace fast_explorer::ui
