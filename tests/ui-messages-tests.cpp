#include "test-harness.h"
#include "ui/messages.h"

using fast_explorer::ui::kWmFeAddressCommit;
using fast_explorer::ui::kWmFeBase;
using fast_explorer::ui::kWmFeEnumBatch;
using fast_explorer::ui::kWmFeEnumComplete;
using fast_explorer::ui::kWmFeEnumError;
using fast_explorer::ui::kWmFeFsChange;
using fast_explorer::ui::kWmFeIconBatch;
using fast_explorer::ui::kWmFeOperationResult;
using fast_explorer::ui::kWmFePerfEvent;
using fast_explorer::ui::kWmFeSortComplete;

FE_TEST_CASE(UiMessages_AllDistinct) {
  const UINT ids[] = {
      kWmFeBase,         kWmFeEnumBatch,    kWmFeEnumComplete,
      kWmFeEnumError,    kWmFeSortComplete, kWmFeIconBatch,
      kWmFeOperationResult, kWmFeFsChange,  kWmFePerfEvent,
      kWmFeAddressCommit,
  };
  for (size_t i = 0; i < std::size(ids); ++i) {
    for (size_t j = i + 1; j < std::size(ids); ++j) {
      FE_ASSERT_NE(ids[i], ids[j]);
    }
  }
}

FE_TEST_CASE(UiMessages_WithinWmAppRange) {
  FE_ASSERT_TRUE(kWmFeBase >= WM_APP);
  FE_ASSERT_TRUE(kWmFeAddressCommit <= 0xBFFFu);
}

FE_TEST_CASE(UiMessages_ConsecutiveOffsets) {
  FE_ASSERT_EQ(kWmFeEnumBatch, kWmFeBase + 0x01u);
  FE_ASSERT_EQ(kWmFeEnumComplete, kWmFeBase + 0x02u);
  FE_ASSERT_EQ(kWmFePerfEvent, kWmFeBase + 0x08u);
  FE_ASSERT_EQ(kWmFeAddressCommit, kWmFeBase + 0x09u);
}
