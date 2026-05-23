#include "test-harness.h"
#include "winui_lite/chrome/dpi-scale.h"

using fast_explorer::ui::scaleForDpi;

FE_TEST_CASE(DpiScale_DefaultDpi_Identity) {
  FE_ASSERT_EQ(scaleForDpi(100, 96), 100);
  FE_ASSERT_EQ(scaleForDpi(0, 96), 0);
  FE_ASSERT_EQ(scaleForDpi(1280, 96), 1280);
}

FE_TEST_CASE(DpiScale_125Percent_Scales) {
  // 120 / 96 = 1.25
  FE_ASSERT_EQ(scaleForDpi(100, 120), 125);
  FE_ASSERT_EQ(scaleForDpi(160, 120), 200);
}

FE_TEST_CASE(DpiScale_150Percent_Scales) {
  FE_ASSERT_EQ(scaleForDpi(100, 144), 150);
  FE_ASSERT_EQ(scaleForDpi(300, 144), 450);
}

FE_TEST_CASE(DpiScale_200Percent_Scales) {
  FE_ASSERT_EQ(scaleForDpi(100, 192), 200);
  FE_ASSERT_EQ(scaleForDpi(160, 192), 320);
}

FE_TEST_CASE(DpiScale_NearestRounding) {
  // 100 * 120 / 96 = 125, exact
  FE_ASSERT_EQ(scaleForDpi(100, 120), 125);
  // 99 * 120 / 96 = 123.75 -> 124
  FE_ASSERT_EQ(scaleForDpi(99, 120), 124);
}

FE_TEST_CASE(DpiScale_ZeroDpi_DefensiveIdentity) {
  FE_ASSERT_EQ(scaleForDpi(100, 0), 100);
}

FE_TEST_CASE(DpiScale_ZeroValue_AnyDpi_Zero) {
  FE_ASSERT_EQ(scaleForDpi(0, 144), 0);
  FE_ASSERT_EQ(scaleForDpi(0, 192), 0);
}
