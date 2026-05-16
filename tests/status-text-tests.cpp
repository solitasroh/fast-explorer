#include "test-harness.h"
#include "ui/status-text.h"

using fast_explorer::core::EnumerationError;
using fast_explorer::ui::errorStatusText;
using fast_explorer::ui::loadingProgressStatusText;
using fast_explorer::ui::loadingStatusText;
using fast_explorer::ui::OperationResult;
using fast_explorer::ui::opResultStatusText;
using fast_explorer::ui::readyStatusText;
using fast_explorer::ui::ShellCommandKind;

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

FE_TEST_CASE(StatusText_OpResult_DeleteSuccess) {
  OperationResult r;
  r.kind = ShellCommandKind::Delete;
  r.sourcePath = L"C:\\tmp\\victim.txt";
  r.success = true;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Moved 'victim.txt' to Recycle Bin");
}

FE_TEST_CASE(StatusText_OpResult_DeleteFailure) {
  OperationResult r;
  r.kind = ShellCommandKind::Delete;
  r.sourcePath = L"C:\\tmp\\victim.txt";
  r.success = false;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Failed to delete 'victim.txt'");
}

FE_TEST_CASE(StatusText_OpResult_RenameSuccess) {
  OperationResult r;
  r.kind = ShellCommandKind::Rename;
  r.sourcePath = L"C:\\tmp\\before.txt";
  r.newName = L"after.txt";
  r.success = true;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Renamed 'before.txt' to 'after.txt'");
}

FE_TEST_CASE(StatusText_OpResult_RenameFailure) {
  OperationResult r;
  r.kind = ShellCommandKind::Rename;
  r.sourcePath = L"C:\\tmp\\before.txt";
  r.newName = L"after.txt";
  r.success = false;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Failed to rename 'before.txt'");
}

FE_TEST_CASE(StatusText_OpResult_CreateFolderSuccess) {
  OperationResult r;
  r.kind = ShellCommandKind::CreateFolder;
  r.sourcePath = L"C:\\tmp";
  r.newName = L"NewSub";
  r.success = true;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Created folder 'NewSub'");
}

FE_TEST_CASE(StatusText_OpResult_CreateFolderFailure) {
  OperationResult r;
  r.kind = ShellCommandKind::CreateFolder;
  r.sourcePath = L"C:\\tmp";
  r.newName = L"NewSub";
  r.success = false;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Failed to create folder 'NewSub'");
}

FE_TEST_CASE(StatusText_OpResult_LeafExtraction_NoSeparator) {
  // Defensive case: sourcePath without a separator should be used
  // as-is for the quoted leaf, not produce an empty quote.
  OperationResult r;
  r.kind = ShellCommandKind::Delete;
  r.sourcePath = L"barefile";
  r.success = true;
  FE_ASSERT_WSTREQ(opResultStatusText(r),
                   L"Moved 'barefile' to Recycle Bin");
}
