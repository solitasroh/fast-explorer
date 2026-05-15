#include "test-harness.h"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string_view>
#include <vector>

#include "core/file-entry.h"
#include "core/file-sort.h"

using fast_explorer::core::compareEntries;
using fast_explorer::core::FileEntry;
using fast_explorer::core::kNoExtension;
using fast_explorer::core::lessEntries;
using fast_explorer::core::SortDirection;
using fast_explorer::core::SortKey;
using fast_explorer::core::SortSpec;
namespace flags = fast_explorer::core::file_entry_flags;

namespace {

FileEntry makeEntry(std::wstring_view name,
                    uint64_t size = 0,
                    uint64_t modified = 0,
                    uint16_t extOffset = kNoExtension,
                    uint8_t entryFlags = 0) {
  FileEntry e{};
  e.namePtr = name.data();
  e.nameLength = static_cast<uint16_t>(name.size());
  e.extensionOffset = extOffset;
  e.size = size;
  e.modifiedTime100ns = modified;
  e.flags = entryFlags;
  return e;
}

uint16_t findExtensionOffset(std::wstring_view name) {
  const auto pos = name.rfind(L'.');
  if (pos == std::wstring_view::npos) return kNoExtension;
  return static_cast<uint16_t>(pos);
}

constexpr SortSpec kNameAsc{SortKey::Name, SortDirection::Ascending};
constexpr SortSpec kNameDesc{SortKey::Name, SortDirection::Descending};
constexpr SortSpec kSizeAsc{SortKey::Size, SortDirection::Ascending};
constexpr SortSpec kSizeDesc{SortKey::Size, SortDirection::Descending};
constexpr SortSpec kModAsc{SortKey::Modified, SortDirection::Ascending};
constexpr SortSpec kModDesc{SortKey::Modified, SortDirection::Descending};
constexpr SortSpec kTypeAsc{SortKey::Type, SortDirection::Ascending};
constexpr SortSpec kTypeDesc{SortKey::Type, SortDirection::Descending};

}  // namespace

FE_TEST_CASE(FileSort_Name_Ascending_OrdersAlphabetically) {
  const auto a = makeEntry(L"apple.txt", 0, 0, findExtensionOffset(L"apple.txt"));
  const auto b = makeEntry(L"banana.txt", 0, 0, findExtensionOffset(L"banana.txt"));
  FE_ASSERT_TRUE(compareEntries(a, b, kNameAsc) < 0);
  FE_ASSERT_TRUE(compareEntries(b, a, kNameAsc) > 0);
}

FE_TEST_CASE(FileSort_Name_Descending_ReversesOrder) {
  const auto a = makeEntry(L"apple.txt", 0, 0, findExtensionOffset(L"apple.txt"));
  const auto b = makeEntry(L"banana.txt", 0, 0, findExtensionOffset(L"banana.txt"));
  FE_ASSERT_TRUE(compareEntries(a, b, kNameDesc) > 0);
  FE_ASSERT_TRUE(compareEntries(b, a, kNameDesc) < 0);
}

FE_TEST_CASE(FileSort_Name_IgnoresCase) {
  const auto a = makeEntry(L"Apple", 0);
  const auto b = makeEntry(L"banana", 0);
  FE_ASSERT_TRUE(compareEntries(a, b, kNameAsc) < 0);
}

FE_TEST_CASE(FileSort_Name_IdenticalReturnsZero) {
  const auto a = makeEntry(L"same.txt", 0, 0, findExtensionOffset(L"same.txt"));
  const auto b = makeEntry(L"SAME.TXT", 0, 0, findExtensionOffset(L"SAME.TXT"));
  FE_ASSERT_EQ(compareEntries(a, b, kNameAsc), 0);
}

FE_TEST_CASE(FileSort_Size_Ascending_OrdersBySize) {
  const auto a = makeEntry(L"small", 10);
  const auto b = makeEntry(L"large", 1000);
  FE_ASSERT_TRUE(compareEntries(a, b, kSizeAsc) < 0);
  FE_ASSERT_TRUE(compareEntries(b, a, kSizeAsc) > 0);
}

FE_TEST_CASE(FileSort_Size_Descending_ReversesOrder) {
  const auto a = makeEntry(L"small", 10);
  const auto b = makeEntry(L"large", 1000);
  FE_ASSERT_TRUE(compareEntries(a, b, kSizeDesc) > 0);
}

FE_TEST_CASE(FileSort_Size_EqualFallsBackToNameAscending) {
  const auto a = makeEntry(L"banana", 100);
  const auto b = makeEntry(L"apple", 100);
  FE_ASSERT_TRUE(compareEntries(a, b, kSizeAsc) > 0);
  FE_ASSERT_TRUE(compareEntries(b, a, kSizeAsc) < 0);
}

FE_TEST_CASE(FileSort_Size_DescendingTieKeepsNameAscending) {
  const auto a = makeEntry(L"banana", 100);
  const auto b = makeEntry(L"apple", 100);
  FE_ASSERT_TRUE(compareEntries(a, b, kSizeDesc) > 0);
  FE_ASSERT_TRUE(compareEntries(b, a, kSizeDesc) < 0);
}

FE_TEST_CASE(FileSort_Modified_Ascending_OldestFirst) {
  const auto older = makeEntry(L"a", 0, 1000);
  const auto newer = makeEntry(L"b", 0, 2000);
  FE_ASSERT_TRUE(compareEntries(older, newer, kModAsc) < 0);
  FE_ASSERT_TRUE(compareEntries(newer, older, kModAsc) > 0);
}

FE_TEST_CASE(FileSort_Modified_Descending_NewestFirst) {
  const auto older = makeEntry(L"a", 0, 1000);
  const auto newer = makeEntry(L"b", 0, 2000);
  FE_ASSERT_TRUE(compareEntries(older, newer, kModDesc) > 0);
}

FE_TEST_CASE(FileSort_Modified_EqualFallsBackToNameAscending) {
  const auto a = makeEntry(L"zeta", 0, 5000);
  const auto b = makeEntry(L"alpha", 0, 5000);
  FE_ASSERT_TRUE(compareEntries(a, b, kModAsc) > 0);
}

FE_TEST_CASE(FileSort_Type_Ascending_OrdersByExtension) {
  const auto a = makeEntry(L"doc.doc", 0, 0, findExtensionOffset(L"doc.doc"));
  const auto b = makeEntry(L"txt.txt", 0, 0, findExtensionOffset(L"txt.txt"));
  FE_ASSERT_TRUE(compareEntries(a, b, kTypeAsc) < 0);
}

FE_TEST_CASE(FileSort_Type_Descending_ReversesOrder) {
  const auto a = makeEntry(L"doc.doc", 0, 0, findExtensionOffset(L"doc.doc"));
  const auto b = makeEntry(L"txt.txt", 0, 0, findExtensionOffset(L"txt.txt"));
  FE_ASSERT_TRUE(compareEntries(a, b, kTypeDesc) > 0);
}

FE_TEST_CASE(FileSort_Type_NoExtensionBeforeWithExtension) {
  const auto bare = makeEntry(L"readme", 0, 0, kNoExtension);
  const auto withExt = makeEntry(L"file.txt", 0, 0, findExtensionOffset(L"file.txt"));
  FE_ASSERT_TRUE(compareEntries(bare, withExt, kTypeAsc) < 0);
  FE_ASSERT_TRUE(compareEntries(withExt, bare, kTypeAsc) > 0);
}

FE_TEST_CASE(FileSort_Type_EqualFallsBackToNameAscending) {
  const auto a = makeEntry(L"zeta.txt", 0, 0, findExtensionOffset(L"zeta.txt"));
  const auto b = makeEntry(L"alpha.txt", 0, 0, findExtensionOffset(L"alpha.txt"));
  FE_ASSERT_TRUE(compareEntries(a, b, kTypeAsc) > 0);
  FE_ASSERT_TRUE(compareEntries(b, a, kTypeAsc) < 0);
}

FE_TEST_CASE(FileSort_Type_IgnoresExtensionCase) {
  const auto a = makeEntry(L"a.TXT", 0, 0, findExtensionOffset(L"a.TXT"));
  const auto b = makeEntry(L"b.txt", 0, 0, findExtensionOffset(L"b.txt"));
  FE_ASSERT_EQ(compareEntries(a, b, kTypeAsc), -1);
}

FE_TEST_CASE(FileSort_LessEntries_MatchesCompareSign) {
  const auto a = makeEntry(L"apple", 10);
  const auto b = makeEntry(L"banana", 20);
  FE_ASSERT_TRUE(lessEntries(a, b, kNameAsc));
  FE_ASSERT_FALSE(lessEntries(b, a, kNameAsc));
  FE_ASSERT_FALSE(lessEntries(a, a, kNameAsc));
}

FE_TEST_CASE(FileSort_StdSort_NameAscending_ProducesAlphabetical) {
  std::vector<FileEntry> v{
      makeEntry(L"charlie"),
      makeEntry(L"alpha"),
      makeEntry(L"bravo"),
  };
  std::sort(v.begin(), v.end(),
            [](const FileEntry& a, const FileEntry& b) {
              return lessEntries(a, b, kNameAsc);
            });
  FE_ASSERT_EQ(std::wstring_view(v[0].namePtr, v[0].nameLength),
               std::wstring_view(L"alpha"));
  FE_ASSERT_EQ(std::wstring_view(v[1].namePtr, v[1].nameLength),
               std::wstring_view(L"bravo"));
  FE_ASSERT_EQ(std::wstring_view(v[2].namePtr, v[2].nameLength),
               std::wstring_view(L"charlie"));
}

FE_TEST_CASE(FileSort_StdSort_SizeDescTiebreakDeterministic) {
  std::vector<FileEntry> v{
      makeEntry(L"zeta", 100),
      makeEntry(L"alpha", 100),
      makeEntry(L"mike", 100),
  };
  std::sort(v.begin(), v.end(),
            [](const FileEntry& a, const FileEntry& b) {
              return lessEntries(a, b, kSizeDesc);
            });
  // All sizes equal -> primary == 0 for every pair -> tiebreak by name asc.
  FE_ASSERT_EQ(std::wstring_view(v[0].namePtr, v[0].nameLength),
               std::wstring_view(L"alpha"));
  FE_ASSERT_EQ(std::wstring_view(v[1].namePtr, v[1].nameLength),
               std::wstring_view(L"mike"));
  FE_ASSERT_EQ(std::wstring_view(v[2].namePtr, v[2].nameLength),
               std::wstring_view(L"zeta"));
}

FE_TEST_CASE(FileSort_EmptyNameHandledGracefully) {
  const auto empty = makeEntry(L"");
  const auto named = makeEntry(L"a");
  FE_ASSERT_TRUE(compareEntries(empty, named, kNameAsc) < 0);
  FE_ASSERT_EQ(compareEntries(empty, empty, kNameAsc), 0);
}

FE_TEST_CASE(FileSort_StrictWeakOrdering_Irreflexive) {
  const auto e = makeEntry(L"file.txt", 100, 5000, findExtensionOffset(L"file.txt"));
  // a < a must be false under strict weak ordering.
  FE_ASSERT_FALSE(lessEntries(e, e, kNameAsc));
  FE_ASSERT_FALSE(lessEntries(e, e, kSizeAsc));
  FE_ASSERT_FALSE(lessEntries(e, e, kModAsc));
  FE_ASSERT_FALSE(lessEntries(e, e, kTypeAsc));
}
