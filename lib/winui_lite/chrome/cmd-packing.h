#pragma once

#include <windows.h>

#include <cstddef>

namespace fast_explorer::ui {

// Packs (buttonId, paneIdx) into a single WORD for WM_COMMAND
// routing. WM_COMMAND only carries 16 bits of identifier in
// LOWORD(wParam), so the split is 12 bits for the original button /
// menu id and 4 bits for the pane index — up to 16 panes (well past
// the multi-pane cap) and ids 0..4095 fit cleanly. A previous 8/8
// split silently truncated any id >= 256, which broke menu items
// whose ids lived above the toolbar range.
inline constexpr WORD kPaneIdxBits = 4;
inline constexpr WORD kPaneIdxMask = 0x000F;

constexpr WORD packCmd(WORD buttonId, std::size_t paneIdx) noexcept {
  return static_cast<WORD>((buttonId << kPaneIdxBits) |
                           (paneIdx & kPaneIdxMask));
}
constexpr WORD unpackButton(WORD packed) noexcept {
  return static_cast<WORD>(packed >> kPaneIdxBits);
}
constexpr std::size_t unpackPane(WORD packed) noexcept {
  return static_cast<std::size_t>(packed & kPaneIdxMask);
}

}  // namespace fast_explorer::ui
