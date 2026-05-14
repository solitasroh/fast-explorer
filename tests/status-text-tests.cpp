#include "test-harness.h"
#include "ui/status-text.h"

using fast_explorer::core::EnumerationError;
using fast_explorer::ui::errorStatusText;
using fast_explorer::ui::loadingProgressStatusText;
using fast_explorer::ui::loadingStatusText;
using fast_explorer::ui::readyStatusText;

FE_TEST_CASE(StatusText_Loading_IncludesPath) {
  FE_ASSERT_WSTREQ(loadingStatusText(L"C:\\tmp\\foo"),
                   L"Loading: C:\\tmp\\foo");
}

FE_TEST_CASE(StatusText_LoadingProgress_FormatsCount) {
  FE_ASSERT_WSTREQ(loadingProgressStatusText(0), L"Loading: 0 items");
  FE_ASSERT_WSTREQ(loadingProgressStatusText(100), L"Loading: 100 items");
}

FE_TEST_CASE(StatusText_Ready_FormatsCount) {
  FE_ASSERT_WSTREQ(readyStatusText(0), L"0 items");
  FE_ASSERT_WSTREQ(readyStatusText(200), L"200 items");
  FE_ASSERT_WSTREQ(readyStatusText(100000), L"100000 items");
}

FE_TEST_CASE(StatusText_Error_Known) {
  FE_ASSERT_WSTREQ(errorStatusText(EnumerationError::PathNotFound),
                   L"Error: PathNotFound");
  FE_ASSERT_WSTREQ(errorStatusText(EnumerationError::AccessDenied),
                   L"Error: AccessDenied");
  FE_ASSERT_WSTREQ(errorStatusText(EnumerationError::Canceled),
                   L"Error: Canceled");
}

FE_TEST_CASE(StatusText_Error_NoneIsValidLabel) {
  FE_ASSERT_WSTREQ(errorStatusText(EnumerationError::None),
                   L"Error: None");
}
