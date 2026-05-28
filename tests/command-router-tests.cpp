#include "test-harness.h"
#include "winui_lite/chrome/cmd-packing.h"
#include "winui_lite/chrome/command-router.h"

using fast_explorer::ui::CommandRouter;
using fast_explorer::ui::packCmd;

FE_TEST_CASE(CommandRouter_EmptyDispatch_ReturnsFalse) {
  CommandRouter r;
  FE_ASSERT_TRUE(!r.dispatch(42));
}

FE_TEST_CASE(CommandRouter_RegisteredDispatch_InvokesHandlerAndReturnsTrue) {
  CommandRouter r;
  int hits = 0;
  r.registerCommand(100, [&] { ++hits; });
  FE_ASSERT_TRUE(r.dispatch(100));
  FE_ASSERT_EQ(hits, 1);
}

FE_TEST_CASE(CommandRouter_UnregisteredId_ReturnsFalseNoCall) {
  CommandRouter r;
  int hits = 0;
  r.registerCommand(100, [&] { ++hits; });
  FE_ASSERT_TRUE(!r.dispatch(101));
  FE_ASSERT_EQ(hits, 0);
}

FE_TEST_CASE(CommandRouter_ReregisterOverwrites) {
  CommandRouter r;
  int a = 0, b = 0;
  r.registerCommand(7, [&] { ++a; });
  r.registerCommand(7, [&] { ++b; });
  r.dispatch(7);
  FE_ASSERT_EQ(a, 0);
  FE_ASSERT_EQ(b, 1);
  FE_ASSERT_EQ(r.size(), static_cast<size_t>(1));
}

FE_TEST_CASE(CommandRouter_Unregister_Removes) {
  CommandRouter r;
  r.registerCommand(5, [] {});
  r.unregister(5);
  FE_ASSERT_TRUE(!r.dispatch(5));
  FE_ASSERT_EQ(r.size(), static_cast<size_t>(0));
}

FE_TEST_CASE(CommandRouter_MultipleHandlers_Independent) {
  CommandRouter r;
  int x = 0, y = 0;
  r.registerCommand(1, [&] { ++x; });
  r.registerCommand(2, [&] { ++y; });
  r.dispatch(1);
  r.dispatch(1);
  r.dispatch(2);
  FE_ASSERT_EQ(x, 2);
  FE_ASSERT_EQ(y, 1);
}

FE_TEST_CASE(CommandRouter_PackedDispatch_DeliversPaneIdx) {
  CommandRouter r;
  std::size_t seenPane = 999;
  r.registerPackedCommand(200, [&](std::size_t p) { seenPane = p; });
  // packCmd(200, 3) packs to (200 << 4) | 3 = 3203.
  FE_ASSERT_TRUE(r.dispatch(packCmd(200, 3)));
  FE_ASSERT_EQ(seenPane, static_cast<std::size_t>(3));
}

FE_TEST_CASE(CommandRouter_PlainBeatsPackedOnConflict) {
  CommandRouter r;
  int plain = 0, packed = 0;
  // Concoct a value where packCmd produces id 7 (button=0, pane=7).
  // by_id_ registration must win even though unpackButton(7)=0 also
  // matches the packed handler registered at 0.
  r.registerCommand(7, [&] { ++plain; });
  r.registerPackedCommand(0, [&](std::size_t) { ++packed; });
  FE_ASSERT_TRUE(r.dispatch(7));
  FE_ASSERT_EQ(plain, 1);
  FE_ASSERT_EQ(packed, 0);
}

FE_TEST_CASE(CommandRouter_PackedUnregister_Removes) {
  CommandRouter r;
  int hits = 0;
  r.registerPackedCommand(300, [&](std::size_t) { ++hits; });
  r.unregisterPacked(300);
  FE_ASSERT_TRUE(!r.dispatch(packCmd(300, 0)));
  FE_ASSERT_EQ(hits, 0);
}
