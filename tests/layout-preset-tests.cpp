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
