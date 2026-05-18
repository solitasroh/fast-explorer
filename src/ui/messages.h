#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace fast_explorer::ui {

// Window-message IDs marshalled from background workers to the UI
// thread.  +0x100 above WM_APP to clear messages that other libraries
// commonly stake at WM_APP + small offsets.
inline constexpr UINT kWmFeBase            = WM_APP + 0x100;
inline constexpr UINT kWmFeEnumBatch       = kWmFeBase + 0x01;
inline constexpr UINT kWmFeEnumComplete    = kWmFeBase + 0x02;
inline constexpr UINT kWmFeEnumError       = kWmFeBase + 0x03;
inline constexpr UINT kWmFeSortComplete    = kWmFeBase + 0x04;
inline constexpr UINT kWmFeIconBatch       = kWmFeBase + 0x05;
inline constexpr UINT kWmFeOperationResult = kWmFeBase + 0x06;
inline constexpr UINT kWmFeFsChange        = kWmFeBase + 0x07;
inline constexpr UINT kWmFePerfEvent       = kWmFeBase + 0x08;
inline constexpr UINT kWmFeAddressCommit   = kWmFeBase + 0x09;
inline constexpr UINT kWmFeLowMemory       = kWmFeBase + 0x0A;
inline constexpr UINT kWmFeAddressPopupPick = kWmFeBase + 0x0B;
inline constexpr UINT kWmFeAddressPopupHide = kWmFeBase + 0x0C;
inline constexpr UINT kWmFeAddressPopupClick = kWmFeBase + 0x0D;

inline constexpr WORD kAccelFocusAddress = 100;
inline constexpr WORD kAccelNavBack       = 101;
inline constexpr WORD kAccelNavForward    = 102;
inline constexpr WORD kAccelNavUp         = 103;
inline constexpr WORD kAccelRefresh       = 104;
inline constexpr WORD kAccelDelete        = 105;
inline constexpr WORD kAccelRename        = 106;
inline constexpr WORD kAccelCreateFolder  = 107;
inline constexpr WORD kAccelLayoutSingle  = 108;  // Ctrl+1
inline constexpr WORD kAccelLayoutDual    = 109;  // Ctrl+2
inline constexpr WORD kAccelLayoutToggle  = 110;  // Ctrl+H
inline constexpr WORD kAccelPaneSwitch    = 111;  // Tab
inline constexpr WORD kAccelCopy          = 112;  // Ctrl+C
inline constexpr WORD kAccelCut           = 113;  // Ctrl+X
inline constexpr WORD kAccelPaste         = 114;  // Ctrl+V

static_assert(kWmFeBase >= WM_APP,
              "WM_FE_* must live in the WM_APP user range");
static_assert(kWmFeAddressPopupClick <= 0xBFFFu,
              "WM_FE_* must not spill past the WM_APP user range");

// WPARAM packing for multi-pane message routing. The low 32 bits hold
// the pane's generation (existing contract, unchanged); the next 8
// bits hold the pane index (0..255, more than enough for the M9
// dual-horizontal layout that caps at 2). A WPARAM constructed
// without these helpers (paneIndex 0 implicit) decodes safely as
// pane 0, so legacy and pre-M9 senders interoperate.
inline UINT_PTR makePaneWParam(std::size_t paneIndex,
                               std::uint32_t generation) noexcept {
  return (static_cast<UINT_PTR>(paneIndex & 0xFFu) << 32) |
         static_cast<UINT_PTR>(generation);
}

inline std::size_t paneIndexFromWParam(WPARAM wp) noexcept {
  return static_cast<std::size_t>(
      (static_cast<UINT_PTR>(wp) >> 32) & 0xFFu);
}

inline std::uint32_t generationFromWParam(WPARAM wp) noexcept {
  return static_cast<std::uint32_t>(static_cast<UINT_PTR>(wp));
}

}  // namespace fast_explorer::ui
