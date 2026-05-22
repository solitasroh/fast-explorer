#include "core/file-grouping.h"

#include "core/file-entry.h"

namespace fast_explorer::core {

namespace {

// Hangul syllable block U+AC00..U+D7A3.
// Each syllable encodes (cho * 588 + jung * 28 + jong) offset from U+AC00.
// There are 19 choseong (leading consonants).
constexpr int32_t kHangulSyllableBase   = 0xAC00;
constexpr int32_t kHangulSyllableMax    = 0xD7A3;
constexpr int32_t kHangulChoseongStride = 588;

// Hangul Compatibility Jamo (U+3131..U+314E) → choseong index.
// 30 codepoints in the block; only 19 of them are leading consonants used
// by syllables. The rest (vowels, jong-only) fall through to "other".
// Table order matches Unicode order; -1 means "not a choseong".
constexpr int8_t kCompatJamoToChoseong[] = {
  // U+3131 ㄱ, U+3132 ㄲ, U+3133 ㄳ, U+3134 ㄴ, U+3135 ㄵ, U+3136 ㄶ,
       0,           1,          -1,          2,          -1,         -1,
  // U+3137 ㄷ, U+3138 ㄸ, U+3139 ㄹ, U+313A ㄺ, U+313B ㄻ, U+313C ㄼ,
       3,           4,           5,          -1,         -1,         -1,
  // U+313D ㄽ, U+313E ㄾ, U+313F ㄿ, U+3140 ㅀ, U+3141 ㅁ, U+3142 ㅂ,
      -1,          -1,          -1,         -1,           6,          7,
  // U+3143 ㅃ, U+3144 ㅄ, U+3145 ㅅ, U+3146 ㅆ, U+3147 ㅇ, U+3148 ㅈ,
       8,          -1,          9,          10,          11,         12,
  // U+3149 ㅉ, U+314A ㅊ, U+314B ㅋ, U+314C ㅌ, U+314D ㅍ, U+314E ㅎ,
      13,          14,         15,          16,          17,         18,
};

int32_t groupIdForName(const FileEntry& entry) noexcept {
  if (entry.nameLength == 0 || entry.namePtr == nullptr) {
    return 46;
  }
  const wchar_t c = entry.namePtr[0];
  // Hangul syllables → choseong by arithmetic.
  if (c >= kHangulSyllableBase && c <= kHangulSyllableMax) {
    return (c - kHangulSyllableBase) / kHangulChoseongStride;
  }
  // Hangul compatibility jamo → table lookup.
  if (c >= 0x3131 && c <= 0x314E) {
    const int8_t cho = kCompatJamoToChoseong[c - 0x3131];
    if (cho >= 0) return cho;
    return 46;
  }
  // ASCII digit.
  if (c >= L'0' && c <= L'9') return 45;
  // ASCII letter (case folded).
  if (c >= L'A' && c <= L'Z') return 19 + (c - L'A');
  if (c >= L'a' && c <= L'z') return 19 + (c - L'a');
  return 46;
}

}  // namespace

int32_t groupIdForEntry(GroupKey key,
                        const FileEntry& entry,
                        uint64_t /*nowFiletime*/) noexcept {
  switch (key) {
    case GroupKey::None:     return 0;
    case GroupKey::Name:     return groupIdForName(entry);
    case GroupKey::Modified: return 0;
    case GroupKey::Type:     return 0;
  }
  return 0;
}

}  // namespace fast_explorer::core
