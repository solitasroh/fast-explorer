#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "test-harness.h"
#include "explorer/folder-name.h"

using fast_explorer::ui::uniqueFolderLeaf;

FE_TEST_CASE(FolderName_NoCollisions_ReturnsBase) {
  const std::vector<std::wstring_view> existing{};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"New folder"), L"New folder");
}

FE_TEST_CASE(FolderName_BaseCollides_ReturnsBaseParen2) {
  const std::array<std::wstring_view, 1> existing{L"New folder"};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"New folder"),
                   L"New folder (2)");
}

FE_TEST_CASE(FolderName_BaseAndParen2Collide_ReturnsParen3) {
  const std::array<std::wstring_view, 2> existing{L"New folder",
                                                  L"New folder (2)"};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"New folder"),
                   L"New folder (3)");
}

FE_TEST_CASE(FolderName_SkipsToFirstFreeSuffix) {
  const std::array<std::wstring_view, 4> existing{
      L"New folder", L"New folder (2)", L"New folder (3)", L"New folder (5)"};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"New folder"),
                   L"New folder (4)");
}

FE_TEST_CASE(FolderName_CaseInsensitiveCollision) {
  // Win32 paths are case-insensitive by default, so "new FOLDER" and
  // "New folder" must be treated as the same leaf.
  const std::array<std::wstring_view, 1> existing{L"new FOLDER"};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"New folder"),
                   L"New folder (2)");
}

FE_TEST_CASE(FolderName_UnrelatedNamesDoNotCollide) {
  const std::array<std::wstring_view, 3> existing{L"alpha", L"beta", L"gamma"};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"New folder"), L"New folder");
}

FE_TEST_CASE(FolderName_DifferentBaseStillTriesParen2First) {
  const std::array<std::wstring_view, 1> existing{L"project"};
  FE_ASSERT_WSTREQ(uniqueFolderLeaf(existing, L"project"), L"project (2)");
}
