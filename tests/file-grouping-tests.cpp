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

FE_TEST_CASE(group_name_latin_uppercase_A_is_19) {
  auto e = makeEntry(L"Apple.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 19);
}

FE_TEST_CASE(group_name_latin_lowercase_normalized) {
  auto e1 = makeEntry(L"apple.txt");
  auto e2 = makeEntry(L"APPLE.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0),
               groupIdForEntry(GroupKey::Name, e2, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0), 19);
}

FE_TEST_CASE(group_name_latin_Z_is_44) {
  auto e = makeEntry(L"zebra.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 44);
}

FE_TEST_CASE(group_name_digit_is_45) {
  auto e1 = makeEntry(L"9-readme.txt");
  auto e2 = makeEntry(L"0.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0), 45);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e2, 0), 45);
}

FE_TEST_CASE(group_name_symbol_is_other_46) {
  auto e1 = makeEntry(L"_foo.txt");
  auto e2 = makeEntry(L"!bar.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e1, 0), 46);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e2, 0), 46);
}

FE_TEST_CASE(group_name_compat_jamo_normalized_to_choseong) {
  // U+3131 (ㄱ, Hangul Compatibility Jamo) maps to choseong ㄱ (id 0)
  auto e = makeEntry(L"ㄱ-file.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 0);
  // U+314E (ㅎ) maps to choseong ㅎ (id 18)
  auto e2 = makeEntry(L"ㅎ-file.txt");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e2, 0), 18);
}

FE_TEST_CASE(group_name_empty_name_is_other) {
  auto e = makeEntry(L"");
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Name, e, 0), 46);
}
