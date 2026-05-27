#include "test-harness.h"
#include "winui_lite/chrome/command-router.h"

using fast_explorer::ui::CommandRouter;

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
