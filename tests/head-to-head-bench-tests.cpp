#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "bench/head-to-head-bench.h"
#include "core/path-utils.h"
#include "test-harness.h"

using fast_explorer::bench::enumerateFindFirstRaw;
using fast_explorer::bench::enumerateGfibheRaw;
using fast_explorer::bench::EnumerationBenchError;
using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::HeadToHeadResult;
using fast_explorer::bench::PresetKind;
using fast_explorer::bench::runHeadToHeadBench;
using fast_explorer::core::PathConvertError;
using fast_explorer::core::toInternal;
using fast_explorer::tests::makeFreshTempDirPath;
using fast_explorer::tests::removeDirectoryRecursive;

FE_TEST_CASE(HeadToHead_FindFirstRaw_SmallDataset_Counts200) {
  const std::wstring dir = makeFreshTempDirPath(L"hth-find");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  std::wstring internal;
  FE_ASSERT_EQ(toInternal(dir, internal), PathConvertError::None);
  bool ok = false;
  const uint64_t count = enumerateFindFirstRaw(internal, &ok);
  removeDirectoryRecursive(dir);
  FE_ASSERT_TRUE(ok);
  FE_ASSERT_EQ(count, 200ULL);
}

FE_TEST_CASE(HeadToHead_GfibheRaw_SmallDataset_Counts200) {
  const std::wstring dir = makeFreshTempDirPath(L"hth-gfibhe");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  std::wstring internal;
  FE_ASSERT_EQ(toInternal(dir, internal), PathConvertError::None);
  bool ok = false;
  const uint64_t count = enumerateGfibheRaw(internal, &ok);
  removeDirectoryRecursive(dir);
  FE_ASSERT_TRUE(ok);
  FE_ASSERT_EQ(count, 200ULL);
}

FE_TEST_CASE(HeadToHead_RawMethods_AgreeOnCount) {
  const std::wstring dir = makeFreshTempDirPath(L"hth-agree");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  std::wstring internal;
  FE_ASSERT_EQ(toInternal(dir, internal), PathConvertError::None);
  bool okFind = false;
  bool okGfibhe = false;
  const uint64_t findCount = enumerateFindFirstRaw(internal, &okFind);
  const uint64_t gfibheCount = enumerateGfibheRaw(internal, &okGfibhe);
  removeDirectoryRecursive(dir);
  FE_ASSERT_TRUE(okFind);
  FE_ASSERT_TRUE(okGfibhe);
  FE_ASSERT_EQ(findCount, gfibheCount);
}

FE_TEST_CASE(HeadToHead_FindFirstRaw_NonexistentPath_FailsCleanly) {
  std::wstring internal;
  FE_ASSERT_EQ(toInternal(L"C:\\__fe_no_such_path__99999", internal),
               PathConvertError::None);
  bool ok = true;
  enumerateFindFirstRaw(internal, &ok);
  FE_ASSERT_FALSE(ok);
}

FE_TEST_CASE(HeadToHead_GfibheRaw_NonexistentPath_FailsCleanly) {
  std::wstring internal;
  FE_ASSERT_EQ(toInternal(L"C:\\__fe_no_such_path__99999", internal),
               PathConvertError::None);
  bool ok = true;
  enumerateGfibheRaw(internal, &ok);
  FE_ASSERT_FALSE(ok);
}

FE_TEST_CASE(HeadToHeadBench_EmptyPath_PathInvalid) {
  HeadToHeadResult r = runHeadToHeadBench(L"", 3);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::PathInvalid);
}

FE_TEST_CASE(HeadToHeadBench_ZeroRuns_PathInvalid) {
  HeadToHeadResult r = runHeadToHeadBench(L"C:\\tmp", 0);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::PathInvalid);
}

FE_TEST_CASE(HeadToHeadBench_NonexistentPath_OpenFailed) {
  HeadToHeadResult r =
      runHeadToHeadBench(L"C:\\__fe_no_such_path__99999", 2);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::OpenFailed);
}

FE_TEST_CASE(HeadToHeadBench_SmallDataset_BothMethodsAgree) {
  const std::wstring dir = makeFreshTempDirPath(L"hth-bench");
  const auto gen = generateDataset(PresetKind::Small, dir, 1);
  if (gen.error != GenerateError::None) {
    removeDirectoryRecursive(dir);
    FE_ASSERT_TRUE(false);
  }
  HeadToHeadResult r = runHeadToHeadBench(dir, 3);
  removeDirectoryRecursive(dir);
  FE_ASSERT_EQ(r.error, EnumerationBenchError::None);
  FE_ASSERT_EQ(r.findRuns.size(), 3ULL);
  FE_ASSERT_EQ(r.gfibheRuns.size(), 3ULL);
  for (const auto& run : r.findRuns) {
    FE_ASSERT_EQ(run.entriesObserved, 200ULL);
  }
  for (const auto& run : r.gfibheRuns) {
    FE_ASSERT_EQ(run.entriesObserved, 200ULL);
  }
  FE_ASSERT_TRUE(r.findMedianUs > 0);
  FE_ASSERT_TRUE(r.gfibheMedianUs > 0);
}
