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
