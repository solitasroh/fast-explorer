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
using fast_explorer::ui::formatSelectionSummary;
using fast_explorer::ui::humanReadableSize;
using fast_explorer::ui::statusBarPartLayout;
using fast_explorer::ui::StatusPartLayout;

// Compile-time policy lock: a single part for single mode, a 50/50
// split for dual mode, and the -1 sentinel always at the far edge
// so the last part covers the trailing slack on odd-width windows.
static_assert(statusBarPartLayout(1280, 1).count == 1);
static_assert(statusBarPartLayout(1280, 1).edges[0] == -1);
static_assert(statusBarPartLayout(1280, 2).count == 2);
static_assert(statusBarPartLayout(1280, 2).edges[0] == 640);
static_assert(statusBarPartLayout(1280, 2).edges[1] == -1);
static_assert(statusBarPartLayout(1281, 2).edges[0] == 640);  // round down
static_assert(statusBarPartLayout(0, 2).count == 1);  // degenerate width
static_assert(statusBarPartLayout(1280, 0).count == 1);  // fallback
static_assert(statusBarPartLayout(1280, 3).count == 1);  // fallback

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

FE_TEST_CASE(StatusPartLayout_Single_SinglePartFullWidth) {
  const auto out = statusBarPartLayout(1280, 1);
  FE_ASSERT_EQ(out.count, static_cast<std::size_t>(1));
  FE_ASSERT_EQ(out.edges[0], -1);
}

FE_TEST_CASE(StatusPartLayout_Dual_FiftyFiftySplit) {
  const auto out = statusBarPartLayout(1280, 2);
  FE_ASSERT_EQ(out.count, static_cast<std::size_t>(2));
  FE_ASSERT_EQ(out.edges[0], 640);
  FE_ASSERT_EQ(out.edges[1], -1);
}

FE_TEST_CASE(StatusPartLayout_Dual_OddWidth_LeftAbsorbsLossOnRoundDown) {
  const auto out = statusBarPartLayout(1281, 2);
  FE_ASSERT_EQ(out.edges[0], 640);
  FE_ASSERT_EQ(out.edges[1], -1);
}

FE_TEST_CASE(StatusPartLayout_OutOfRangeOrZeroWidth_FallsBackToSingle) {
  FE_ASSERT_EQ(statusBarPartLayout(0, 2).count, static_cast<std::size_t>(1));
  FE_ASSERT_EQ(statusBarPartLayout(1280, 0).count, static_cast<std::size_t>(1));
  FE_ASSERT_EQ(statusBarPartLayout(1280, 3).count, static_cast<std::size_t>(1));
}

FE_TEST_CASE(HumanReadableSize_BytesUnderKiB_RawWithBSuffix) {
  FE_ASSERT_WSTREQ(humanReadableSize(0),    L"0 B");
  FE_ASSERT_WSTREQ(humanReadableSize(1),    L"1 B");
  FE_ASSERT_WSTREQ(humanReadableSize(512),  L"512 B");
  FE_ASSERT_WSTREQ(humanReadableSize(1023), L"1023 B");
}

FE_TEST_CASE(HumanReadableSize_KiloMegaGigaTera_OneDecimal) {
  FE_ASSERT_WSTREQ(humanReadableSize(1024),                       L"1.0 KB");
  FE_ASSERT_WSTREQ(humanReadableSize(1536),                       L"1.5 KB");
  FE_ASSERT_WSTREQ(humanReadableSize(1024ull * 1024ull),          L"1.0 MB");
  FE_ASSERT_WSTREQ(humanReadableSize(45ull * 1024ull * 1024ull +
                                     200ull * 1024ull),           L"45.2 MB");
  FE_ASSERT_WSTREQ(humanReadableSize(1024ull * 1024ull * 1024ull),L"1.0 GB");
  FE_ASSERT_WSTREQ(humanReadableSize(1024ull * 1024ull * 1024ull *
                                     1024ull),                    L"1.0 TB");
}

FE_TEST_CASE(HumanReadableSize_PetaExa_ExtendsBeyondTeraToEB) {
  // PB tier (above TB, below EB)
  const std::uint64_t pb = 1024ull * 1024ull * 1024ull * 1024ull * 1024ull;
  FE_ASSERT_WSTREQ(humanReadableSize(pb),       L"1.0 PB");
  FE_ASSERT_WSTREQ(humanReadableSize(2ull * pb),L"2.0 PB");
  // EB tier (1024 * PB). uint64_t caps at ~16 EB so the table top
  // never overflows; any byte count near the uint64_t ceiling
  // renders inside the EB unit.
  const std::uint64_t eb = 1024ull * pb;
  FE_ASSERT_WSTREQ(humanReadableSize(eb), L"1.0 EB");
}

FE_TEST_CASE(FormatSelectionSummary_NoSelection_FallsBackToReadyText) {
  // With selectedCount == 0 the formatter must produce exactly the
  // bare "N items" line, identical to the legacy readyStatusText
  // path so a non-selecting user sees no rendering change.
  FE_ASSERT_WSTREQ(formatSelectionSummary(1234, 0, 0), L"1234 items");
  FE_ASSERT_WSTREQ(formatSelectionSummary(0,    0, 0), L"0 items");
}

FE_TEST_CASE(FormatSelectionSummary_WithSelection_AppendsCountAndSize) {
  FE_ASSERT_WSTREQ(
      formatSelectionSummary(1234, 5, 45ull * 1024ull * 1024ull + 200ull * 1024ull),
      L"1234 items | 5 selected (45.2 MB)");
}

FE_TEST_CASE(FormatSelectionSummary_SelectionWithZeroBytes_FoldersOnly) {
  // The byte-aggregator at the controller layer already strips
  // folders, so a folder-only selection arrives as
  // selectedBytes == 0. The formatter must still show the count
  // and render "0 B" for the size.
  FE_ASSERT_WSTREQ(formatSelectionSummary(100, 3, 0),
                   L"100 items | 3 selected (0 B)");
}
