#pragma once

#include <windows.h>

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

inline constexpr WORD kAccelFocusAddress = 100;

static_assert(kWmFeBase >= WM_APP,
              "WM_FE_* must live in the WM_APP user range");
static_assert(kWmFeAddressCommit <= 0xBFFFu,
              "WM_FE_* must not spill past the WM_APP user range");

}  // namespace fast_explorer::ui
