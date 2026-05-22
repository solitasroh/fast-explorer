#include "test-harness.h"

#include "core/file-entry.h"
#include "core/file-grouping.h"

using fast_explorer::core::FileEntry;
using fast_explorer::core::GroupKey;
using fast_explorer::core::groupIdForEntry;
using fast_explorer::core::kNoExtension;

namespace {

FileEntry makeEntry(std::wstring_view name,
                    uint16_t extOffset = kNoExtension,
                    uint8_t entryFlags = 0,
                    uint64_t modified100ns = 0) {
  FileEntry e{};
  e.namePtr = name.data();
  e.nameLength = static_cast<uint16_t>(name.size());
  e.extensionOffset = extOffset;
  e.flags = entryFlags;
  e.modifiedTime100ns = modified100ns;
  return e;
}

}  // namespace

FE_TEST_CASE(group_none_returns_zero_for_every_entry) {
  auto e1 = makeEntry(L"foo.txt");
  auto e2 = makeEntry(L"bar");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::None, e1, 0), 0);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::None, e2, 0), 0);
}

FE_TEST_CASE(group_name_korean_choseong_first_syllable) {
  // U+AC00 = '가' → choseong index 0 (ㄱ)
  auto e = makeEntry(L"가나다.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 0);
}

FE_TEST_CASE(group_name_korean_choseong_middle_range) {
  // U+B9C8 = '마' → choseong index 6 (ㅁ)
  auto e = makeEntry(L"마라톤.exe");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 6);
}

FE_TEST_CASE(group_name_korean_choseong_last) {
  // '하' → choseong index 18 (ㅎ)
  auto e = makeEntry(L"하늘.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 18);
}
