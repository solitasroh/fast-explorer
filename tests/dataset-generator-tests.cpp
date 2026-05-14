#include <windows.h>

#include "bench-fs-helper.h"
#include "bench/dataset-generator.h"
#include "test-harness.h"

using fast_explorer::bench::generateDataset;
using fast_explorer::bench::GenerateError;
using fast_explorer::bench::GenerateResult;
using fast_explorer::bench::PresetKind;
using fast_explorer::tests::makeFreshTempDirPath;
using fast_explorer::tests::removeDirectoryRecursive;

namespace {

uint64_t countDirEntries(const std::wstring& dir) {
  std::wstring pattern = dir + L"\\*";
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                              FindExSearchNameMatch, nullptr,
                              FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) {
    return 0;
  }
  uint64_t count = 0;
  do {
    if (wcscmp(fd.cFileName, L".") == 0 ||
        wcscmp(fd.cFileName, L"..") == 0) {
      continue;
    }
    ++count;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return count;
}

}  // namespace

FE_TEST_CASE(DatasetGenerator_None_ReturnsInvalidPreset) {
  GenerateResult r = generateDataset(PresetKind::None, L"C:\\tmp\\x", 1);
  FE_ASSERT_EQ(r.error, GenerateError::InvalidPreset);
}

FE_TEST_CASE(DatasetGenerator_EmptyOut_ReturnsOutPathInvalid) {
  GenerateResult r = generateDataset(PresetKind::Small, L"", 1);
  FE_ASSERT_EQ(r.error, GenerateError::OutPathInvalid);
}

FE_TEST_CASE(DatasetGenerator_RelativeOut_ReturnsOutPathInvalid) {
  GenerateResult r = generateDataset(PresetKind::Small, L"relative-no-drive", 1);
  FE_ASSERT_EQ(r.error, GenerateError::OutPathInvalid);
}

FE_TEST_CASE(DatasetGenerator_Small_Creates200Files) {
  const std::wstring dir = makeFreshTempDirPath(L"small");
  GenerateResult r = generateDataset(PresetKind::Small, dir, 1);
  const bool genOk = (r.error == GenerateError::None);
  uint64_t entries = 0;
  if (genOk) {
    entries = countDirEntries(dir);
  }
  removeDirectoryRecursive(dir);
  FE_ASSERT_TRUE(genOk);
  FE_ASSERT_EQ(r.filesCreated, 200ULL);
  FE_ASSERT_EQ(r.dirsCreated, 0ULL);
  FE_ASSERT_EQ(entries, 200ULL);
}

FE_TEST_CASE(DatasetGenerator_DeepTree_CreatesDepthPlusLeaf) {
  const std::wstring dir = makeFreshTempDirPath(L"deep");
  GenerateResult r = generateDataset(PresetKind::DeepTree, dir, 1);
  const bool genOk = (r.error == GenerateError::None);
  removeDirectoryRecursive(dir);
  FE_ASSERT_TRUE(genOk);
  FE_ASSERT_EQ(r.dirsCreated, 25ULL);
  FE_ASSERT_EQ(r.filesCreated, 1ULL);
}

FE_TEST_CASE(DatasetGenerator_NonEmptyOut_ReturnsOutNotEmpty) {
  const std::wstring dir = makeFreshTempDirPath(L"nonempty");
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring sentinel = dir + L"\\sentinel.txt";
  HANDLE h = CreateFileW(sentinel.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
  GenerateResult r = generateDataset(PresetKind::Small, dir, 1);
  removeDirectoryRecursive(dir);
  FE_ASSERT_EQ(r.error, GenerateError::OutNotEmpty);
}
