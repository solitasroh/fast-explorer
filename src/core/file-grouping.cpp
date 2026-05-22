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

int32_t groupIdForName(const FileEntry& entry) noexcept {
  if (entry.nameLength == 0 || entry.namePtr == nullptr) {
    return 46;  // "other" bucket (placeholder until Task 3 expands)
  }
  const wchar_t c = entry.namePtr[0];
  if (c >= kHangulSyllableBase && c <= kHangulSyllableMax) {
    return (c - kHangulSyllableBase) / kHangulChoseongStride;
  }
  return 46;  // every non-Hangul falls into other until later tasks expand
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
