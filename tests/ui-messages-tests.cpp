#include "test-harness.h"
#include "ui/messages.h"

using fast_explorer::ui::generationFromWParam;
using fast_explorer::ui::kWmFeAddressCommit;
using fast_explorer::ui::kWmFeAddressPopupClick;
using fast_explorer::ui::kWmFeAddressPopupHide;
using fast_explorer::ui::kWmFeAddressPopupPick;
using fast_explorer::ui::kWmFeBase;
using fast_explorer::ui::kWmFeEnumBatch;
using fast_explorer::ui::kWmFeEnumComplete;
using fast_explorer::ui::kWmFeEnumError;
using fast_explorer::ui::kWmFeFsChange;
using fast_explorer::ui::kWmFeIconBatch;
using fast_explorer::ui::kWmFeOperationResult;
using fast_explorer::ui::kWmFePerfEvent;
using fast_explorer::ui::kWmFeSortComplete;
using fast_explorer::ui::makePaneWParam;
using fast_explorer::ui::paneIndexFromWParam;

FE_TEST_CASE(UiMessages_AllDistinct) {
  const UINT ids[] = {
      kWmFeBase,         kWmFeEnumBatch,    kWmFeEnumComplete,
      kWmFeEnumError,    kWmFeSortComplete, kWmFeIconBatch,
      kWmFeOperationResult, kWmFeFsChange,  kWmFePerfEvent,
      kWmFeAddressCommit, kWmFeAddressPopupPick,
      kWmFeAddressPopupHide, kWmFeAddressPopupClick,
  };
  for (size_t i = 0; i < std::size(ids); ++i) {
    for (size_t j = i + 1; j < std::size(ids); ++j) {
      FE_ASSERT_NE(ids[i], ids[j]);
    }
  }
}

FE_TEST_CASE(UiMessages_WithinWmAppRange) {
  FE_ASSERT_TRUE(kWmFeBase >= WM_APP);
  FE_ASSERT_TRUE(kWmFeAddressPopupClick <= 0xBFFFu);
}

FE_TEST_CASE(UiMessages_ConsecutiveOffsets) {
  FE_ASSERT_EQ(kWmFeEnumBatch, kWmFeBase + 0x01u);
  FE_ASSERT_EQ(kWmFeEnumComplete, kWmFeBase + 0x02u);
  FE_ASSERT_EQ(kWmFePerfEvent, kWmFeBase + 0x08u);
  FE_ASSERT_EQ(kWmFeAddressCommit, kWmFeBase + 0x09u);
  FE_ASSERT_EQ(kWmFeAddressPopupPick, kWmFeBase + 0x0Bu);
  FE_ASSERT_EQ(kWmFeAddressPopupHide, kWmFeBase + 0x0Cu);
  FE_ASSERT_EQ(kWmFeAddressPopupClick, kWmFeBase + 0x0Du);
}

FE_TEST_CASE(UiMessages_MakePaneWParam_ZeroPaneZeroGen_IsZero) {
  FE_ASSERT_EQ(makePaneWParam(0, 0), static_cast<UINT_PTR>(0));
}

FE_TEST_CASE(UiMessages_MakePaneWParam_GenInLow32Bits) {
  const UINT_PTR wp = makePaneWParam(0, 0xDEADBEEFu);
  FE_ASSERT_EQ(generationFromWParam(wp), 0xDEADBEEFu);
  FE_ASSERT_EQ(paneIndexFromWParam(wp), static_cast<std::size_t>(0));
}

FE_TEST_CASE(UiMessages_MakePaneWParam_PaneInBitsAbove32) {
  const UINT_PTR wp = makePaneWParam(1, 0x12345678u);
  FE_ASSERT_EQ(generationFromWParam(wp), 0x12345678u);
  FE_ASSERT_EQ(paneIndexFromWParam(wp), static_cast<std::size_t>(1));
}

FE_TEST_CASE(UiMessages_MakePaneWParam_GenZeroDecodesPaneCorrectly) {
  const UINT_PTR wp = makePaneWParam(2, 0);
  FE_ASSERT_EQ(generationFromWParam(wp), 0u);
  FE_ASSERT_EQ(paneIndexFromWParam(wp), static_cast<std::size_t>(2));
}

FE_TEST_CASE(UiMessages_MakePaneWParam_LegacyZeroWParamDecodesAsPaneZero) {
  // Pre-M9 senders that use WPARAM=0 must still decode as pane 0 +
  // generation 0 so the dispatcher routes them to the first pane.
  FE_ASSERT_EQ(paneIndexFromWParam(0), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(generationFromWParam(0), 0u);
}

FE_TEST_CASE(UiMessages_MakePaneWParam_LegacyGenOnlyWParamDecodesAsPaneZero) {
  // Pre-M9 senders that put generation directly into WPARAM (no
  // packing) must decode as pane 0 + the literal generation value.
  const UINT_PTR wp = static_cast<UINT_PTR>(0xCAFEBABEu);
  FE_ASSERT_EQ(paneIndexFromWParam(wp), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(generationFromWParam(wp), 0xCAFEBABEu);
}
