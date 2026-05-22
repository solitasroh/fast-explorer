#include "core/file-grouping.h"

#include <windows.h>

#include <algorithm>
#include <unordered_set>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/file-sort.h"
#include "ui/format-cache.h"

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

// Compatibility-jamo glyphs for the 19 choseong group IDs (0..18) used as
// header labels. Index matches `groupIdForName`'s choseong indexing.
constexpr const wchar_t* kChoseongGlyph[19] = {
  L"ㄱ", L"ㄲ", L"ㄴ", L"ㄷ", L"ㄸ", L"ㄹ", L"ㅁ",
  L"ㅂ", L"ㅃ", L"ㅅ", L"ㅆ", L"ㅇ", L"ㅈ", L"ㅉ",
  L"ㅊ", L"ㅋ", L"ㅌ", L"ㅍ", L"ㅎ",
};

// Convert a UTC FILETIME (100ns ticks) to a local SYSTEMTIME.
SYSTEMTIME toLocalSystemTime(uint64_t utc100ns) noexcept {
  FILETIME utc{};
  utc.dwLowDateTime  = static_cast<DWORD>(utc100ns);
  utc.dwHighDateTime = static_cast<DWORD>(utc100ns >> 32);
  FILETIME local{};
  FileTimeToLocalFileTime(&utc, &local);
  SYSTEMTIME st{};
  FileTimeToSystemTime(&local, &st);
  return st;
}

// Convert a local SYSTEMTIME back to UTC FILETIME 100ns ticks.
uint64_t toUtcFiletime(const SYSTEMTIME& localSt) noexcept {
  FILETIME local{};
  SystemTimeToFileTime(&localSt, &local);
  FILETIME utc{};
  LocalFileTimeToFileTime(&local, &utc);
  ULARGE_INTEGER ui{};
  ui.LowPart  = utc.dwLowDateTime;
  ui.HighPart = utc.dwHighDateTime;
  return ui.QuadPart;
}

// Returns the UTC FILETIME corresponding to local-midnight at the start
// of the day that `nowLocal` falls in.
uint64_t localMidnightOfDay(const SYSTEMTIME& nowLocal) noexcept {
  SYSTEMTIME st = nowLocal;
  st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
  return toUtcFiletime(st);
}

// SYSTEMTIME.wDayOfWeek: Sunday=0, Monday=1, ..., Saturday=6.
// Returns 0..6 days to subtract from `dt` to reach the Monday of that week.
int daysFromMonday(WORD dow) noexcept {
  // Sunday is treated as the previous Monday + 6 days (week-of-Mon convention).
  return (dow == 0) ? 6 : (dow - 1);
}

// Subtracts `days` from a SYSTEMTIME's date (handles month/year rollover by
// going through FILETIME). Time fields are zeroed by caller.
uint64_t subtractDays(const SYSTEMTIME& localMidnight, int days) noexcept {
  uint64_t ft = toUtcFiletime(localMidnight);
  // 100ns ticks per day = 86400 seconds * 1e7
  constexpr uint64_t kTicksPerDay = 86400ULL * 10000000ULL;
  ft -= static_cast<uint64_t>(days) * kTicksPerDay;
  return ft;
}

int32_t groupIdForModified(const FileEntry& entry, uint64_t nowUtc) noexcept {
  const uint64_t mod = entry.modifiedTime100ns;
  const SYSTEMTIME nowLocal = toLocalSystemTime(nowUtc);
  const uint64_t todayStart = localMidnightOfDay(nowLocal);
  // Future-dated → clamp to "today"
  if (mod >= todayStart) return 0;
  SYSTEMTIME todayMidnight = nowLocal;
  todayMidnight.wHour = todayMidnight.wMinute =
      todayMidnight.wSecond = todayMidnight.wMilliseconds = 0;
  const uint64_t yesterdayStart = subtractDays(todayMidnight, 1);
  if (mod >= yesterdayStart) return 1;
  const uint64_t weekStart =
      subtractDays(todayMidnight, daysFromMonday(nowLocal.wDayOfWeek));
  if (mod >= weekStart) return 2;
  SYSTEMTIME monthStart = todayMidnight;
  monthStart.wDay = 1;
  monthStart.wDayOfWeek = 0;
  const uint64_t monthStartFt = toUtcFiletime(monthStart);
  if (mod >= monthStartFt) return 3;
  SYSTEMTIME yearStart = todayMidnight;
  yearStart.wMonth = 1;
  yearStart.wDay = 1;
  yearStart.wDayOfWeek = 0;
  const uint64_t yearStartFt = toUtcFiletime(yearStart);
  if (mod >= yearStartFt) return 4;
  return 5;
}

// Stable FNV-1a hash of a case-folded ASCII extension string.
// Folded into the [2, 0x7FFFFFFE] range so it never collides with the
// reserved 0 (folder) or 1 (no-extension) IDs and stays positive.
int32_t hashExtension(const wchar_t* ext, size_t len) noexcept {
  constexpr uint32_t kFnvOffset = 0x811C9DC5u;
  constexpr uint32_t kFnvPrime  = 0x01000193u;
  uint32_t h = kFnvOffset;
  for (size_t i = 0; i < len; ++i) {
    wchar_t c = ext[i];
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
    h ^= static_cast<uint32_t>(c);
    h *= kFnvPrime;
  }
  int32_t id = static_cast<int32_t>(h & 0x7FFFFFFFu);
  if (id < 2) id += 2;
  return id;
}

int32_t groupIdForType(const FileEntry& entry) noexcept {
  if (entry.flags & file_entry_flags::kIsDirectory) return 0;
  if (entry.extensionOffset == kNoExtension ||
      entry.extensionOffset >= entry.nameLength) {
    return 1;
  }
  // Extension text starts AFTER the dot at extensionOffset.
  const wchar_t* ext = entry.namePtr + entry.extensionOffset + 1;
  const size_t   len = entry.nameLength - entry.extensionOffset - 1;
  return hashExtension(ext, len);
}

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
                        uint64_t nowFiletime) noexcept {
  switch (key) {
    case GroupKey::None:     return 0;
    case GroupKey::Name:     return groupIdForName(entry);
    case GroupKey::Modified: return groupIdForModified(entry, nowFiletime);
    case GroupKey::Type:     return groupIdForType(entry);
  }
  return 0;
}

std::vector<int32_t> enumerateGroups(GroupKey key,
                                     const FileModelStore& store,
                                     uint64_t nowFiletime) {
  std::vector<int32_t> result;
  if (key == GroupKey::None) return result;
  // displayedCount() == subset size when a filter is active; falls
  // back to publishedCount() otherwise. publishedCount() would OOB
  // visibleAt under an active subset.
  const std::size_t count = store.displayedCount();
  if (count == 0) return result;
  std::unordered_set<int32_t> seen;
  seen.reserve(count);
  result.reserve(64);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& entry = store.visibleAt(i);
    const int32_t id = groupIdForEntry(key, entry, nowFiletime);
    if (seen.insert(id).second) {
      result.push_back(id);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::wstring groupTitleForId(GroupKey key, int32_t id,
                             const FileModelStore* store,
                             const fast_explorer::ui::FormatCache* cache) {
  switch (key) {
    case GroupKey::None:
      return std::wstring{};
    case GroupKey::Name: {
      if (id >= 0 && id <= 18) return std::wstring{kChoseongGlyph[id]};
      if (id >= 19 && id <= 44) {
        const wchar_t letter = static_cast<wchar_t>(L'A' + (id - 19));
        return std::wstring(1, letter);
      }
      if (id == 45) return L"0 - 9";
      return L"기타";
    }
    case GroupKey::Modified: {
      switch (id) {
        case 0:  return L"오늘";
        case 1:  return L"어제";
        case 2:  return L"이번 주";
        case 3:  return L"이번 달";
        case 4:  return L"올해";
        default: return L"더 오래전";
      }
    }
    case GroupKey::Type: {
      if (id == 0) return L"폴더";
      if (id == 1) return L"파일";
      // id >= 2: find an exemplar entry with this hashed id, format via cache.
      // Tests pass nullptr — fall back to a safe label rather than crashing.
      if (store == nullptr || cache == nullptr) {
        return L"파일";
      }
      // FormatCache::typeForEntry is non-const (it mutates the LRU on miss),
      // so we have to drop the const to call it. The cache itself is a
      // UI-thread-only mutable cache; the const here is only for declaration
      // hygiene.
      auto* mutableCache =
          const_cast<fast_explorer::ui::FormatCache*>(cache);
      const std::size_t count = store->displayedCount();
      for (std::size_t i = 0; i < count; ++i) {
        const auto& entry = store->visibleAt(i);
        if (groupIdForEntry(GroupKey::Type, entry, 0) == id) {
          return mutableCache->typeForEntry(entry);
        }
      }
      return L"파일";
    }
  }
  return std::wstring{};
}

int compareWithGroup(const FileEntry& a,
                     const FileEntry& b,
                     SortSpec spec,
                     GroupKey gk,
                     uint64_t nowFiletime) noexcept {
  if (gk != GroupKey::None) {
    const int32_t ga = groupIdForEntry(gk, a, nowFiletime);
    const int32_t gb = groupIdForEntry(gk, b, nowFiletime);
    if (ga != gb) return ga - gb;
  }
  return compareEntries(a, b, spec);
}

}  // namespace fast_explorer::core
