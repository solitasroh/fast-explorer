#include "test-harness.h"
#include "explorer/pane-tab-host-state.h"

using fast_explorer::ui::detail::adjustActiveAfterErase;
using fast_explorer::ui::detail::adjustActiveAfterMove;

FE_TEST_CASE(PaneTabHostState_Erase_BeforeActive_Decrements) {
  FE_ASSERT_EQ(adjustActiveAfterErase(2, 0, 3),
               static_cast<std::size_t>(1));
}
FE_TEST_CASE(PaneTabHostState_Erase_AfterActive_Unchanged) {
  FE_ASSERT_EQ(adjustActiveAfterErase(0, 2, 3),
               static_cast<std::size_t>(0));
}
FE_TEST_CASE(PaneTabHostState_Erase_AtActive_LastBecomesNewLast) {
  FE_ASSERT_EQ(adjustActiveAfterErase(2, 2, 3),
               static_cast<std::size_t>(1));
}
FE_TEST_CASE(PaneTabHostState_Erase_AtActive_MiddleStays) {
  FE_ASSERT_EQ(adjustActiveAfterErase(1, 1, 3),
               static_cast<std::size_t>(1));
}
FE_TEST_CASE(PaneTabHostState_Move_ActiveItself_FollowsTo) {
  FE_ASSERT_EQ(adjustActiveAfterMove(2, 2, 0),
               static_cast<std::size_t>(0));
}
FE_TEST_CASE(PaneTabHostState_Move_FromBeforeActiveToAfter_Decrements) {
  FE_ASSERT_EQ(adjustActiveAfterMove(2, 0, 3),
               static_cast<std::size_t>(1));
}
FE_TEST_CASE(PaneTabHostState_Move_FromAfterActiveToBefore_Increments) {
  FE_ASSERT_EQ(adjustActiveAfterMove(1, 3, 0),
               static_cast<std::size_t>(2));
}
FE_TEST_CASE(PaneTabHostState_Move_Outside_Unchanged) {
  FE_ASSERT_EQ(adjustActiveAfterMove(2, 4, 5),
               static_cast<std::size_t>(2));
}
