# File Grouping ("분류 방법") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Win11 Explorer "분류 방법" (Group by) — empty-area right-click menu groups list-view items under collapsible headers by Name first-letter, Modified date bucket, or Type. Per-pane session-only state; native LVS_OWNERDATA grouping via `LVM_ENABLEGROUPVIEW` + `LVN_GETDISPINFO` callback.

**Architecture:** New `core/file-grouping.{h,cpp}` pure module owns the bucketization (no Win32). `PaneController` adds `groupBy_` + `groupNow_` state; `PaneSortCoordinator::requestSort` accepts group params and uses a group-aware comparator wrapping the existing `compareEntries`. `MainWindow` handles the right-click menu, calls `LVM_INSERTGROUPW` after sort completes, and fills `iGroupId` per row in `LVN_GETDISPINFOW`.

**Tech Stack:** C++20, Win32 common-controls (ListView), CMake/Ninja, MSVC, in-repo test harness (`tests/test-harness.h` + ctest).

**Spec:** [docs/superpowers/specs/2026-05-22-file-grouping-design.md](../specs/2026-05-22-file-grouping-design.md)

**Build / verify cycle for every task:**

```powershell
$dll = (Get-ChildItem "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools" -Filter "Microsoft.VisualStudio.DevShell.dll" -Recurse | Select-Object -First 1).FullName
Import-Module $dll
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\18\Professional" -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
cmake --build build           # builds everything including core-tests
ctest --test-dir build --output-on-failure
```

Test name pattern: each `FE_TEST_CASE(name)` registers globally; `ctest` runs them via `core-tests.exe`. Filter to one test by running `build/core-tests.exe <substring>` if needed.

---

## Phase A — Pure-function core (no Win32)

### Task 1: Create `file-grouping` module + first failing test

**Files:**
- Create: `src/core/file-grouping.h`
- Create: `src/core/file-grouping.cpp`
- Create: `tests/file-grouping-tests.cpp`
- Modify: `CMakeLists.txt` (add new sources to FastExplorer target list AND core-tests sources)

- [ ] **Step 1: Write the failing test**

Create `tests/file-grouping-tests.cpp`:

```cpp
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
```

- [ ] **Step 2: Create header stub**

Create `src/core/file-grouping.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fast_explorer::core {

struct FileEntry;
class FileModelStore;
class FormatCache;

enum class GroupKey : uint8_t {
  None     = 0,
  Name     = 1,
  Modified = 2,
  Type     = 3,
};

// Returns the group ID this entry belongs to under `key`.
// `nowFiletime` is the 100ns-since-1601 timestamp used only for
// GroupKey::Modified; safe to pass 0 for the other keys.
[[nodiscard]] int32_t groupIdForEntry(GroupKey key,
                                      const FileEntry& entry,
                                      uint64_t nowFiletime) noexcept;

}  // namespace fast_explorer::core
```

- [ ] **Step 3: Create implementation stub**

Create `src/core/file-grouping.cpp`:

```cpp
#include "core/file-grouping.h"

#include "core/file-entry.h"

namespace fast_explorer::core {

int32_t groupIdForEntry(GroupKey key,
                        const FileEntry& /*entry*/,
                        uint64_t /*nowFiletime*/) noexcept {
  if (key == GroupKey::None) {
    return 0;
  }
  return 0;  // other keys filled in later tasks
}

}  // namespace fast_explorer::core
```

- [ ] **Step 4: Add to CMake**

Open `CMakeLists.txt` and find the `add_executable(FastExplorer ...)` source list (line ~95-133). Add these two lines next to `src/core/file-sort.cpp` / `file-sort.h`:

```cmake
  src/core/file-grouping.cpp
  src/core/file-grouping.h
```

Then find the `add_executable(core-tests ...)` source list (line ~224-260) and add (next to `tests/file-sort-tests.cpp`):

```cmake
  tests/file-grouping-tests.cpp
```

Also confirm `src/core/file-grouping.cpp` is in the core-tests sources list — search for `tests/file-sort-tests.cpp` and add `src/core/file-grouping.cpp` near `src/core/file-sort.cpp` in that target's source block. (The pattern is: each `tests/X-tests.cpp` typically pairs with adding `src/.../X.cpp` to the same target.)

- [ ] **Step 5: Build + run test**

Run:
```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: build succeeds, `core-tests` passes including the new `group_none_returns_zero_for_every_entry`.

- [ ] **Step 6: Commit**

```bash
git add src/core/file-grouping.h src/core/file-grouping.cpp tests/file-grouping-tests.cpp CMakeLists.txt
git commit -m "feat(core): file-grouping module skeleton with GroupKey::None"
```

---

### Task 2: Implement Name grouping — Hangul choseong (ID 0..18)

**Files:**
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/file-grouping-tests.cpp`:

```cpp
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
```

- [ ] **Step 2: Run tests to verify failure**

Run: `cmake --build build && build/core-tests.exe group_name_korean`

Expected: 3 FAILs ("returned 0, expected 6" etc).

- [ ] **Step 3: Implement Hangul choseong extraction**

Replace `src/core/file-grouping.cpp` with:

```cpp
#include "core/file-grouping.h"

#include "core/file-entry.h"

namespace fast_explorer::core {

namespace {

// Hangul syllable block U+AC000..U+D7A3.
// Each syllable encodes (cho * 588 + jung * 28 + jong) offset from U+AC00.
// There are 19 choseong (leading consonants).
constexpr int32_t kHangulSyllableBase   = 0xAC00;
constexpr int32_t kHangulSyllableMax    = 0xD7A3;
constexpr int32_t kHangulChoseongStride = 588;
constexpr int32_t kHangulChoseongCount  = 19;

int32_t groupIdForName(const FileEntry& entry) noexcept {
  if (entry.nameLength == 0 || entry.namePtr == nullptr) {
    return 46;  // "other" bucket
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
```

- [ ] **Step 4: Run tests to verify pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: PASS for all three new tests, plus all previous tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/file-grouping.cpp tests/file-grouping-tests.cpp
git commit -m "feat(core): GroupKey::Name maps Hangul syllables to choseong"
```

---

### Task 3: Name grouping — Latin (19..44), digit (45), other (46), compat-jamo normalization

**Files:**
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/file-grouping-tests.cpp`:

```cpp
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
```

- [ ] **Step 2: Implement Latin/digit/other + compat-jamo lookup**

Replace `groupIdForName` in `src/core/file-grouping.cpp`:

```cpp
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
```

- [ ] **Step 3: Run tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: all new tests pass; previous Hangul tests still pass.

- [ ] **Step 4: Commit**

```bash
git add src/core/file-grouping.cpp tests/file-grouping-tests.cpp
git commit -m "feat(core): GroupKey::Name buckets Latin/digit/other + compat-jamo"
```

---

### Task 4: Modified grouping — 6 date buckets, local-TZ aware

**Files:**
- Modify: `src/core/file-grouping.h`
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

The `now` parameter is a Windows FILETIME (100ns ticks since 1601-01-01 UTC). Bucket boundaries are computed in *local* time so "오늘" = today in user's TZ.

- [ ] **Step 1: Write failing tests**

Append to `tests/file-grouping-tests.cpp`:

```cpp
#include <windows.h>  // SystemTimeToFileTime, FileTimeToLocalFileTime

namespace {

// Build a FILETIME (UTC ticks since 1601) representing the given local
// wall-clock time. Mirrors the same conversion the production code uses.
uint64_t localFiletime(WORD year, WORD month, WORD day,
                       WORD hour, WORD minute) {
  SYSTEMTIME st{};
  st.wYear = year; st.wMonth = month; st.wDay = day;
  st.wHour = hour; st.wMinute = minute;
  FILETIME local{}, utc{};
  SystemTimeToFileTime(&st, &local);
  LocalFileTimeToFileTime(&local, &utc);
  ULARGE_INTEGER ui{};
  ui.LowPart = utc.dwLowDateTime;
  ui.HighPart = utc.dwHighDateTime;
  return ui.QuadPart;
}

}  // namespace

FE_TEST_CASE(group_modified_today) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);  // Mon noon
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 15, 0, 1));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 0);
}

FE_TEST_CASE(group_modified_yesterday) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 14, 23, 59));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 1);
}

FE_TEST_CASE(group_modified_this_week) {
  // now = Mon 2026-06-15 noon. This-week starts Mon 2026-06-15 00:00.
  // A Sat-before-yesterday (2026-06-13) is older than yesterday (Sun
  // 2026-06-14), which means it's NOT in this-week bucket per spec —
  // the spec defines this-week as "week-start ≤ t < yesterday_start".
  // Pick a value just before yesterday's start: Sun 2026-06-14 just
  // before 00:00 would be in this-week.
  // Simpler case: now = Wed 2026-06-17 12:00; this-week = Mon 2026-06-15
  // 00:00..Tue 2026-06-16 23:59 (excluding yesterday Tue and today Wed
  // is the gray zone). Re-read spec: this-week = ≥ Mon 00:00, excluding
  // today and yesterday. So a Mon noon item with now=Wed noon is in
  // bucket 2 (this week).
  const uint64_t now = localFiletime(2026, 6, 17, 12, 0);  // Wed
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 15, 9, 0));  // Mon
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 2);
}

FE_TEST_CASE(group_modified_this_month) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  // A Sunday from 2 weeks ago (2026-06-01) is before this-week-start but
  // in the same month.
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 6, 1, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 3);
}

FE_TEST_CASE(group_modified_this_year) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  // February of same year — before this-month, in this-year.
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2026, 2, 1, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 4);
}

FE_TEST_CASE(group_modified_older) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2024, 1, 1, 9, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 5);
}

FE_TEST_CASE(group_modified_future_clamps_to_today) {
  const uint64_t now = localFiletime(2026, 6, 15, 12, 0);
  auto e = makeEntry(L"x.txt", kNoExtension, 0, localFiletime(2027, 1, 1, 0, 0));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Modified, e, now), 0);
}
```

- [ ] **Step 2: Implement Modified bucketization**

Add to `src/core/file-grouping.cpp`. Place above `groupIdForName`:

```cpp
#include <windows.h>

namespace {

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
// going through FILETIME). Time fields are zeroed first by caller.
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
  const SYSTEMTIME todayMidnight = [&] {
    SYSTEMTIME s = nowLocal;
    s.wHour = s.wMinute = s.wSecond = s.wMilliseconds = 0;
    return s;
  }();
  const uint64_t yesterdayStart = subtractDays(todayMidnight, 1);
  if (mod >= yesterdayStart) return 1;
  const uint64_t weekStart =
      subtractDays(todayMidnight, daysFromMonday(nowLocal.wDayOfWeek));
  if (mod >= weekStart) return 2;
  const SYSTEMTIME monthStart = [&] {
    SYSTEMTIME s = todayMidnight;
    s.wDay = 1;
    s.wDayOfWeek = 0;  // ignored on input
    return s;
  }();
  const uint64_t monthStartFt = toUtcFiletime(monthStart);
  if (mod >= monthStartFt) return 3;
  const SYSTEMTIME yearStart = [&] {
    SYSTEMTIME s = todayMidnight;
    s.wMonth = 1;
    s.wDay   = 1;
    s.wDayOfWeek = 0;
    return s;
  }();
  const uint64_t yearStartFt = toUtcFiletime(yearStart);
  if (mod >= yearStartFt) return 4;
  return 5;
}

}  // namespace
```

Update the `groupIdForEntry` switch:

```cpp
case GroupKey::Modified: return groupIdForModified(entry, nowFiletime);
```

- [ ] **Step 3: Run tests**

Run: `cmake --build build && build/core-tests.exe group_modified`

Expected: all 7 new Modified tests PASS.

- [ ] **Step 4: Commit**

```bash
git add src/core/file-grouping.cpp tests/file-grouping-tests.cpp
git commit -m "feat(core): GroupKey::Modified buckets relative to local-time now"
```

---

### Task 5: Type grouping — folder (0), no-extension (1), extension dynamic (2+)

**Files:**
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

Type grouping needs a stable extension→ID mapping. The mapping is built per-call: walk the store once, assign IDs 2,3,4,... in encounter order. For `groupIdForEntry` alone (without store context), we can't assign — so this single-entry function returns a *hash-derived* ID for files with extensions. The per-store mapping is built later in `enumerateGroups`.

For Task 5 we test only the simple cases: folder, no-extension, and that two entries with the same extension produce the same id (whatever hash they get).

- [ ] **Step 1: Write failing tests**

Append to `tests/file-grouping-tests.cpp`:

```cpp
FE_TEST_CASE(group_type_folder_is_zero) {
  auto e = makeEntry(L"My Folder", kNoExtension,
                     fast_explorer::core::file_entry_flags::isDirectory);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e, 0), 0);
}

FE_TEST_CASE(group_type_no_extension_is_one) {
  auto e = makeEntry(L"README", kNoExtension);
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e, 0), 1);
}

FE_TEST_CASE(group_type_same_extension_same_id) {
  auto e1 = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  auto e2 = makeEntry(L"different.txt", findExtensionOffset(L"different.txt"));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e1, 0),
               groupIdForEntry(GroupKey::Type, e2, 0));
}

FE_TEST_CASE(group_type_different_extension_different_id) {
  auto e1 = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  auto e2 = makeEntry(L"a.pdf", findExtensionOffset(L"a.pdf"));
  FE_ASSERT_NE(groupIdForEntry(GroupKey::Type, e1, 0),
               groupIdForEntry(GroupKey::Type, e2, 0));
}

FE_TEST_CASE(group_type_extension_is_case_insensitive) {
  auto e1 = makeEntry(L"a.TXT", findExtensionOffset(L"a.TXT"));
  auto e2 = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  FE_ASSERT_EQ(groupIdForEntry(GroupKey::Type, e1, 0),
               groupIdForEntry(GroupKey::Type, e2, 0));
}

FE_TEST_CASE(group_type_extension_is_above_one) {
  auto e = makeEntry(L"a.txt", findExtensionOffset(L"a.txt"));
  FE_ASSERT_TRUE(groupIdForEntry(GroupKey::Type, e, 0) > 1);
}
```

You'll need `findExtensionOffset` — it already exists in `file-sort-tests.cpp`. Define it in the anonymous namespace of `file-grouping-tests.cpp` (don't share — keep test files self-contained):

```cpp
uint16_t findExtensionOffset(std::wstring_view name) {
  const auto pos = name.rfind(L'.');
  if (pos == std::wstring_view::npos) return kNoExtension;
  return static_cast<uint16_t>(pos);
}
```

- [ ] **Step 2: Implement type bucketing**

Add to `src/core/file-grouping.cpp`. Place above `groupIdForName`:

```cpp
namespace {

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
  // Force result into [2, 0x7FFFFFFE].
  int32_t id = static_cast<int32_t>(h & 0x7FFFFFFFu);
  if (id < 2) id += 2;
  return id;
}

int32_t groupIdForType(const FileEntry& entry) noexcept {
  if (entry.flags & file_entry_flags::isDirectory) return 0;
  if (entry.extensionOffset == kNoExtension ||
      entry.extensionOffset >= entry.nameLength) {
    return 1;
  }
  // Extension text starts AT extensionOffset (which points at the '.').
  // We want the chars AFTER the dot.
  const wchar_t* ext = entry.namePtr + entry.extensionOffset + 1;
  const size_t   len = entry.nameLength - entry.extensionOffset - 1;
  return hashExtension(ext, len);
}

}  // namespace
```

Update the `groupIdForEntry` switch:

```cpp
case GroupKey::Type: return groupIdForType(entry);
```

- [ ] **Step 3: Run tests**

Run: `cmake --build build && build/core-tests.exe group_type`

Expected: all 6 new Type tests PASS.

- [ ] **Step 4: Commit**

```bash
git add src/core/file-grouping.cpp tests/file-grouping-tests.cpp
git commit -m "feat(core): GroupKey::Type buckets folder/no-ext/per-extension"
```

---

### Task 6: `enumerateGroups` — walk store, return ordered group IDs present

**Files:**
- Modify: `src/core/file-grouping.h`
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

- [ ] **Step 1: Add header declaration**

Add to `src/core/file-grouping.h`:

```cpp
// Walks the store's displayed items (filter-aware via store.visibleAt)
// and returns the group IDs present, in render order. Empty groups
// are not included. Caller must hold the store stable for the call
// (e.g., enumeration worker not active).
[[nodiscard]] std::vector<int32_t> enumerateGroups(
    GroupKey key,
    const FileModelStore& store,
    uint64_t nowFiletime);
```

- [ ] **Step 2: Write failing test**

Append to `tests/file-grouping-tests.cpp`:

```cpp
#include "core/file-model-store.h"
#include "core/name-arena.h"
using fast_explorer::core::FileModelStore;
using fast_explorer::core::NameArena;
using fast_explorer::core::enumerateGroups;

namespace {

struct StoreFixture {
  FileModelStore store{L"X:\\d"};
  NameArena arena;
};

// Append one entry, then publish so publishedCount reflects all rows so far.
void addEntry(StoreFixture& fx, std::wstring_view name,
              uint64_t modified100ns = 0,
              bool isDir = false) {
  const auto interned = fx.arena.intern(name);
  FileEntry e{};
  e.namePtr = interned.data();
  e.nameLength = static_cast<uint16_t>(interned.size());
  e.extensionOffset = findExtensionOffset(name);
  e.modifiedTime100ns = modified100ns;
  if (isDir) {
    e.flags |= fast_explorer::core::file_entry_flags::isDirectory;
  }
  fx.store.appendEntry(e);
  fx.store.publish(static_cast<std::uint32_t>(fx.store.itemCount()));
}

}  // namespace

FE_TEST_CASE(enumerate_name_returns_only_present_buckets_sorted) {
  StoreFixture fx;
  addEntry(fx, L"가나");        // 0
  addEntry(fx, L"하늘");        // 18
  addEntry(fx, L"Apple");      // 19
  addEntry(fx, L"9-readme");   // 45
  const auto ids = enumerateGroups(GroupKey::Name, fx.store, 0);
  FE_ASSERT_EQ(ids.size(), 4u);
  FE_ASSERT_EQ(ids[0], 0);
  FE_ASSERT_EQ(ids[1], 18);
  FE_ASSERT_EQ(ids[2], 19);
  FE_ASSERT_EQ(ids[3], 45);
}

FE_TEST_CASE(enumerate_returns_empty_when_store_empty) {
  StoreFixture fx;
  const auto ids = enumerateGroups(GroupKey::Name, fx.store, 0);
  FE_ASSERT_EQ(ids.size(), 0u);
}

FE_TEST_CASE(enumerate_type_folders_first_then_files_sorted_by_id) {
  StoreFixture fx;
  addEntry(fx, L"folder1", 0, /*isDir=*/true);
  addEntry(fx, L"a.txt");
  addEntry(fx, L"b.pdf");
  addEntry(fx, L"folder2", 0, /*isDir=*/true);
  const auto ids = enumerateGroups(GroupKey::Type, fx.store, 0);
  // 0 (folders) must appear first since two folders are present.
  FE_ASSERT_TRUE(ids.size() >= 3u);
  FE_ASSERT_EQ(ids[0], 0);
}
```

- [ ] **Step 3: (No store API change needed)**

`FileModelStore::appendEntry` + `publish` already exist (used by `pane-sort-coordinator-tests.cpp`'s `fillStore`). No header/impl change required for the store.

- [ ] **Step 4: Implement enumerateGroups**

Add to `src/core/file-grouping.cpp`:

```cpp
#include "core/file-model-store.h"

#include <algorithm>
#include <unordered_set>

namespace fast_explorer::core {

std::vector<int32_t> enumerateGroups(GroupKey key,
                                     const FileModelStore& store,
                                     uint64_t nowFiletime) {
  std::vector<int32_t> result;
  if (key == GroupKey::None) return result;
  const std::size_t count = store.publishedCount();
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

}  // namespace fast_explorer::core
```

Sort policy: ascending by ID. For Name and Modified, ascending ID is the visual order we want. For Type, ID 0 (folders) is smallest so it lands first; ID 1 (no-extension) is next; hashed extensions sort by hash, which is arbitrary but stable across runs (acceptable for v0.6.0 — a future pass can sort by header text instead).

- [ ] **Step 5: Run tests**

Run: `cmake --build build && build/core-tests.exe enumerate_`

Expected: all 3 enumerate tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/file-grouping.h src/core/file-grouping.cpp tests/file-grouping-tests.cpp src/core/file-model-store.h src/core/file-model-store.cpp
git commit -m "feat(core): enumerateGroups walks store, returns ordered group IDs"
```

---

### Task 7: `groupTitleForId` — Korean header strings

**Files:**
- Modify: `src/core/file-grouping.h`
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

Type group headers depend on the per-extension mapping that lives outside this pure function. We solve it by passing in a `FormatCache&` (already exists, gives "TXT 파일" style strings). For Name and Modified, no cache is needed — header strings are constants.

- [ ] **Step 1: Add header declaration**

Add to `src/core/file-grouping.h`:

```cpp
// Returns the Korean header string for a given (key, id). For Type
// groups (id >= 2), reads the extension's display name via `cache`.
// Caller owns the returned wstring. Never returns empty for valid IDs.
[[nodiscard]] std::wstring groupTitleForId(
    GroupKey key, int32_t id,
    const FileModelStore* store,
    const FormatCache* cache);
```

The `store` + `cache` pointers are only consulted for Type IDs ≥ 2 (look up an exemplar entry from the store, format its type via cache). Other key/id combinations can pass nullptr safely.

- [ ] **Step 2: Write failing tests**

Append to `tests/file-grouping-tests.cpp`:

```cpp
using fast_explorer::core::groupTitleForId;

FE_TEST_CASE(title_name_choseong_uses_jamo_glyph) {
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 0, nullptr, nullptr), L"ㄱ");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 6, nullptr, nullptr), L"ㅁ");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 18, nullptr, nullptr), L"ㅎ");
}

FE_TEST_CASE(title_name_latin_uses_letter) {
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 19, nullptr, nullptr), L"A");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 44, nullptr, nullptr), L"Z");
}

FE_TEST_CASE(title_name_digit_and_other) {
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 45, nullptr, nullptr), L"0 - 9");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Name, 46, nullptr, nullptr), L"기타");
}

FE_TEST_CASE(title_modified_korean_labels) {
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Modified, 0, nullptr, nullptr), L"오늘");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Modified, 1, nullptr, nullptr), L"어제");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Modified, 2, nullptr, nullptr), L"이번 주");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Modified, 3, nullptr, nullptr), L"이번 달");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Modified, 4, nullptr, nullptr), L"올해");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Modified, 5, nullptr, nullptr), L"더 오래전");
}

FE_TEST_CASE(title_type_folder_and_unknown) {
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Type, 0, nullptr, nullptr), L"폴더");
  FE_ASSERT_WSTREQ(groupTitleForId(GroupKey::Type, 1, nullptr, nullptr), L"파일");
}
```

Note: Type id ≥ 2 needs store+cache integration; that's tested via the Win32 path in Phase C.

- [ ] **Step 3: Implement groupTitleForId**

Append to `src/core/file-grouping.cpp`:

```cpp
#include "core/format-cache.h"  // for the Type id >= 2 path

namespace fast_explorer::core {

namespace {

constexpr const wchar_t* kChoseongGlyph[19] = {
  L"ㄱ", L"ㄲ", L"ㄴ", L"ㄷ", L"ㄸ", L"ㄹ", L"ㅁ",
  L"ㅂ", L"ㅃ", L"ㅅ", L"ㅆ", L"ㅇ", L"ㅈ", L"ㅉ",
  L"ㅊ", L"ㅋ", L"ㅌ", L"ㅍ", L"ㅎ",
};

}  // namespace

std::wstring groupTitleForId(GroupKey key, int32_t id,
                             const FileModelStore* store,
                             const FormatCache* cache) {
  switch (key) {
    case GroupKey::None:
      return std::wstring{};
    case GroupKey::Name: {
      if (id >= 0 && id <= 18)  return std::wstring{kChoseongGlyph[id]};
      if (id >= 19 && id <= 44) return std::wstring{static_cast<wchar_t>(L'A' + (id - 19))};
      if (id == 45)             return L"0 - 9";
      return L"기타";
    }
    case GroupKey::Modified: {
      switch (id) {
        case 0: return L"오늘";
        case 1: return L"어제";
        case 2: return L"이번 주";
        case 3: return L"이번 달";
        case 4: return L"올해";
        default: return L"더 오래전";
      }
    }
    case GroupKey::Type: {
      if (id == 0) return L"폴더";
      if (id == 1) return L"파일";
      // Find an exemplar entry with this id, format via cache.
      if (store == nullptr || cache == nullptr) {
        return L"파일";  // graceful fallback for tests / unwired callers
      }
      const std::size_t count = store->publishedCount();
      for (std::size_t i = 0; i < count; ++i) {
        const auto& entry = store->visibleAt(i);
        if (groupIdForEntry(GroupKey::Type, entry, 0) == id) {
          return cache->typeForEntry(entry);
        }
      }
      return L"파일";
    }
  }
  return std::wstring{};
}

}  // namespace fast_explorer::core
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build && build/core-tests.exe title_`

Expected: all 5 new title tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/file-grouping.h src/core/file-grouping.cpp tests/file-grouping-tests.cpp
git commit -m "feat(core): groupTitleForId returns Korean headers for Name/Modified/Type"
```

---

### Task 8: `compareWithGroup` — group-aware comparator wrapper

**Files:**
- Modify: `src/core/file-grouping.h`
- Modify: `src/core/file-grouping.cpp`
- Modify: `tests/file-grouping-tests.cpp`

- [ ] **Step 1: Add header declaration**

Add to `src/core/file-grouping.h`:

```cpp
// Tri-state comparator that uses groupId as the primary key, falling
// through to the existing compareEntries comparator on group ties.
// When `gk == GroupKey::None`, behaviour is exactly compareEntries.
[[nodiscard]] int compareWithGroup(const FileEntry& a,
                                   const FileEntry& b,
                                   struct SortSpec spec,
                                   GroupKey gk,
                                   uint64_t nowFiletime) noexcept;
```

- [ ] **Step 2: Write failing test**

Append to `tests/file-grouping-tests.cpp`:

```cpp
#include "core/file-sort.h"
using fast_explorer::core::compareWithGroup;
using fast_explorer::core::SortDirection;
using fast_explorer::core::SortKey;
using fast_explorer::core::SortSpec;

FE_TEST_CASE(compare_with_group_none_matches_compareEntries) {
  auto a = makeEntry(L"가.txt");
  auto b = makeEntry(L"Apple.txt");
  const SortSpec spec{SortKey::Name, SortDirection::Ascending};
  const int classic = compareEntries(a, b, spec);
  const int wrapped = compareWithGroup(a, b, spec, GroupKey::None, 0);
  FE_ASSERT_EQ(classic, wrapped);
}

FE_TEST_CASE(compare_with_group_cross_group_uses_group_id) {
  auto a = makeEntry(L"하늘.txt");  // group 18
  auto b = makeEntry(L"가나.txt");  // group 0
  const SortSpec spec{SortKey::Name, SortDirection::Ascending};
  const int r = compareWithGroup(a, b, spec, GroupKey::Name, 0);
  // a's group (18) > b's group (0) → positive
  FE_ASSERT_TRUE(r > 0);
}

FE_TEST_CASE(compare_with_group_same_group_falls_through_to_name) {
  auto a = makeEntry(L"가나.txt");  // group 0
  auto b = makeEntry(L"강물.txt");  // group 0
  const SortSpec spec{SortKey::Name, SortDirection::Ascending};
  const int r = compareWithGroup(a, b, spec, GroupKey::Name, 0);
  // Both in group 0; sort by name ordering — 가나 < 강물 (compareEntries).
  FE_ASSERT_TRUE(r < 0);
}
```

- [ ] **Step 3: Implement compareWithGroup**

Append to `src/core/file-grouping.cpp`:

```cpp
#include "core/file-sort.h"

namespace fast_explorer::core {

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
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build && build/core-tests.exe compare_with_group`

Expected: all 3 new comparator tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/file-grouping.h src/core/file-grouping.cpp tests/file-grouping-tests.cpp
git commit -m "feat(core): compareWithGroup wraps compareEntries with groupId-first tiebreak"
```

---

## Phase B — Sort integration

### Task 9: Extend `PaneSortCoordinator::requestSort` to accept (groupBy, nowFiletime)

**Files:**
- Modify: `src/ui/pane-sort-coordinator.h`
- Modify: `src/ui/pane-sort-coordinator.cpp`
- Modify: `tests/pane-sort-coordinator-tests.cpp`

The coordinator stores `groupBy_` and `nowFiletime_` as new members; `requestSort` accepts both as defaulted args. The internal comparator switches from `lessEntries` to `lessThan` lambda using `compareWithGroup`.

- [ ] **Step 1: Write failing test**

Append to `tests/pane-sort-coordinator-tests.cpp` (reuses the existing `SortFixture` defined in that file):

```cpp
#include "core/file-grouping.h"
using fast_explorer::core::GroupKey;

FE_TEST_CASE(sort_coord_group_by_name_clusters_by_choseong) {
  SortFixture fx(0);  // empty store; fill manually
  // Append 6 entries spanning 3 Name groups: 가/강 (id 0), 하 (id 18),
  // Apple/Banana (id 19/20). Order of insertion is shuffled so the
  // sort actually has work to do.
  for (auto name : {L"Banana", L"가나", L"하늘", L"Apple", L"강물", L"Apricot"}) {
    fx.store.appendEntry(makeEntry(name, fx.backing));
  }
  fx.store.publish(static_cast<std::uint32_t>(fx.store.itemCount()));

  // Sync sort with GroupKey::Name. enumerationActive=false. now=0 (unused
  // for Name grouping).
  FE_ASSERT_EQ(
      static_cast<int>(fx.coord.requestSort(
          SortKey::Name, false, GroupKey::Name, 0)),
      static_cast<int>(SortDispatch::AppliedSync));

  // After sort, visibleOrder should cluster by group id.
  // Build the resulting name sequence by walking visibleAt.
  std::vector<std::wstring> names;
  for (std::size_t i = 0; i < fx.store.publishedCount(); ++i) {
    const auto& e = fx.store.visibleAt(i);
    names.emplace_back(e.namePtr, e.nameLength);
  }
  // First two should both start with 가/강 (group 0), next 하 (18),
  // then 3 Latin names alphabetically (Apple/Apricot/Banana = ids 19, 19, 20).
  FE_ASSERT_TRUE(names[0] == L"가나" || names[0] == L"강물");
  FE_ASSERT_TRUE(names[1] == L"가나" || names[1] == L"강물");
  FE_ASSERT_WSTREQ(names[2], L"하늘");
  FE_ASSERT_WSTREQ(names[3], L"Apple");
  FE_ASSERT_WSTREQ(names[4], L"Apricot");
  FE_ASSERT_WSTREQ(names[5], L"Banana");
}
```

- [ ] **Step 2: Update header**

In `src/ui/pane-sort-coordinator.h`, change `requestSort` signature:

```cpp
SortDispatch requestSort(fast_explorer::core::SortKey key,
                         bool enumerationActive,
                         fast_explorer::core::GroupKey groupBy =
                             fast_explorer::core::GroupKey::None,
                         uint64_t nowFiletime = 0);
```

Add to private section:

```cpp
fast_explorer::core::GroupKey groupBy_ =
    fast_explorer::core::GroupKey::None;
uint64_t nowFiletime_ = 0;
```

Add public getters:

```cpp
fast_explorer::core::GroupKey currentGroupBy() const noexcept {
  return groupBy_;
}
uint64_t currentNowFiletime() const noexcept { return nowFiletime_; }
```

Add include:

```cpp
#include "core/file-grouping.h"
```

- [ ] **Step 3: Update implementation**

In `src/ui/pane-sort-coordinator.cpp`, update `requestSort`:

```cpp
SortDispatch PaneSortCoordinator::requestSort(
    fast_explorer::core::SortKey key,
    bool enumerationActive,
    fast_explorer::core::GroupKey groupBy,
    uint64_t nowFiletime) {
  // ... existing enumerationActive guard ...
  sortSpec_.key  = key;
  groupBy_       = groupBy;
  nowFiletime_   = nowFiletime;
  // ... rest unchanged except the comparator used inside std::sort / std::stable_sort ...
}
```

Find the std::sort call (or wherever the comparator is built) and replace `lessEntries(a, b, spec)` with:

```cpp
[spec = sortSpec_, gk = groupBy_, now = nowFiletime_](
    const std::uint32_t& ar, const std::uint32_t& br) noexcept {
  // ar, br are raw indices into store entries.
  const auto& a = entries[ar];
  const auto& b = entries[br];
  return fast_explorer::core::compareWithGroup(a, b, spec, gk, now) < 0;
}
```

If the existing code sorts the visibleOrder array of raw indices directly via `lessEntries(entries[ar], entries[br], spec)`, the change is just to swap that body for `compareWithGroup(...) < 0`.

Do the same change inside the background sort worker if it uses a separate comparator instance.

- [ ] **Step 4: Run all tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: all existing sort tests PASS (default GroupKey::None preserves old behaviour); the new group-clustering test PASSES.

- [ ] **Step 5: Commit**

```bash
git add src/ui/pane-sort-coordinator.h src/ui/pane-sort-coordinator.cpp tests/pane-sort-coordinator-tests.cpp
git commit -m "feat(ui): PaneSortCoordinator accepts groupBy + nowFiletime"
```

---

### Task 10: PaneController gains `groupBy_` + `setGroupBy` + getters

**Files:**
- Modify: `src/ui/pane-controller.h`
- Modify: `src/ui/pane-controller.cpp`
- Modify: `tests/pane-controller-tests.cpp`

- [ ] **Step 1: Write failing test**

Append to `tests/pane-controller-tests.cpp`:

```cpp
#include "core/file-grouping.h"
using fast_explorer::core::GroupKey;

FE_TEST_CASE(PaneController_Default_GroupByIsNone) {
  PaneController pc(nullptr);
  FE_ASSERT_EQ(static_cast<int>(pc.groupBy()),
               static_cast<int>(GroupKey::None));
}

FE_TEST_CASE(PaneController_SetGroupBy_StoresKey) {
  PaneController pc(nullptr);
  pc.setGroupBy(GroupKey::Modified);
  FE_ASSERT_EQ(static_cast<int>(pc.groupBy()),
               static_cast<int>(GroupKey::Modified));
  // Wall-clock 'now' should be non-zero after setGroupBy with a non-None key.
  FE_ASSERT_TRUE(pc.groupNow() != 0);
}

FE_TEST_CASE(PaneController_SetGroupBy_None_LeavesNowAlone) {
  PaneController pc(nullptr);
  pc.setGroupBy(GroupKey::Modified);   // captures now once
  const uint64_t firstNow = pc.groupNow();
  // Switching back to None should still capture a fresh now (consistent
  // semantics — every setGroupBy call captures); subsequent enumerate
  // calls against None are no-ops so the value is harmless but defined.
  pc.setGroupBy(GroupKey::None);
  FE_ASSERT_EQ(static_cast<int>(pc.groupBy()),
               static_cast<int>(GroupKey::None));
  FE_ASSERT_TRUE(pc.groupNow() >= firstNow);
}
```

- [ ] **Step 2: Update header**

In `src/ui/pane-controller.h`, add to private section:

```cpp
fast_explorer::core::GroupKey groupBy_ =
    fast_explorer::core::GroupKey::None;
uint64_t groupNow_ = 0;
```

Add to public section (near `requestSort`):

```cpp
fast_explorer::core::GroupKey groupBy() const noexcept { return groupBy_; }
uint64_t groupNow() const noexcept { return groupNow_; }

// Sets the grouping key, captures `now`, and triggers a re-sort with
// the current sort spec. Returns the SortDispatch from the underlying
// requestSort so the caller can react to async vs sync sort completion.
SortDispatch setGroupBy(fast_explorer::core::GroupKey key);
```

Also update the existing `requestSort` body to thread groupBy through:

```cpp
SortDispatch requestSort(fast_explorer::core::SortKey key) {
  return sortCoord_.requestSort(
      key,
      workerActive_.load(std::memory_order_acquire),
      groupBy_, groupNow_);
}
```

Add include:

```cpp
#include "core/file-grouping.h"
```

- [ ] **Step 3: Implement setGroupBy**

Add to `src/ui/pane-controller.cpp`:

```cpp
#include <windows.h>  // GetSystemTimeAsFileTime

SortDispatch PaneController::setGroupBy(
    fast_explorer::core::GroupKey key) {
  groupBy_ = key;
  // Capture wall-clock once; the same `now` is used by both the
  // sort comparator and any subsequent enumerateGroups call.
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER ui{};
  ui.LowPart  = ft.dwLowDateTime;
  ui.HighPart = ft.dwHighDateTime;
  groupNow_ = ui.QuadPart;
  return requestSort(sortCoord_.currentSortSpec().key);
}
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: all PASS. Existing pane-controller tests still pass (groupBy defaults to None, behaviour unchanged for old callers).

- [ ] **Step 5: Commit**

```bash
git add src/ui/pane-controller.h src/ui/pane-controller.cpp tests/pane-controller-tests.cpp
git commit -m "feat(ui): PaneController owns groupBy_/groupNow_ + setGroupBy"
```

---

## Phase C — Win32 surface (UI)

### Task 11: Add message + menu constants to `messages.h`

**Files:**
- Modify: `src/ui/messages.h`
- Modify: `tests/ui-messages-tests.cpp`

- [ ] **Step 1: Write failing test (uniqueness check)**

Append to `tests/ui-messages-tests.cpp`:

```cpp
FE_TEST_CASE(messages_group_by_message_is_unique) {
  using namespace fast_explorer::ui;
  // Must not collide with any other kWmFe* in the existing list.
  FE_ASSERT_TRUE(kWmFePaneGroupByChanged != kWmFeFilterDismiss);
  FE_ASSERT_TRUE(kWmFePaneGroupByChanged != kWmFeAddressPopupClick);
}

FE_TEST_CASE(messages_group_by_menu_ids_are_unique) {
  using namespace fast_explorer::ui;
  FE_ASSERT_NE(kMenuGroupByNone, kMenuGroupByName);
  FE_ASSERT_NE(kMenuGroupByNone, kMenuGroupByModified);
  FE_ASSERT_NE(kMenuGroupByNone, kMenuGroupByType);
  FE_ASSERT_NE(kMenuGroupByName, kMenuGroupByModified);
  FE_ASSERT_NE(kMenuGroupByName, kMenuGroupByType);
  FE_ASSERT_NE(kMenuGroupByModified, kMenuGroupByType);
}
```

- [ ] **Step 2: Add constants**

In `src/ui/messages.h`, add after `kWmFeFilterDismiss` (line 33):

```cpp
inline constexpr UINT kWmFePaneGroupByChanged  = kWmFeBase + 0x10;
```

Add to the menu-IDs section (search for existing `kMenu*` constants — likely near 300-range):

```cpp
inline constexpr WORD kMenuGroupByNone     = 380;
inline constexpr WORD kMenuGroupByName     = 381;
inline constexpr WORD kMenuGroupByModified = 382;
inline constexpr WORD kMenuGroupByType     = 383;
```

- [ ] **Step 3: Run tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: new uniqueness tests PASS.

- [ ] **Step 4: Commit**

```bash
git add src/ui/messages.h tests/ui-messages-tests.cpp
git commit -m "feat(ui): add kWmFePaneGroupByChanged + kMenuGroupBy* IDs"
```

---

### Task 12: Extend `handleGetDispInfoBody` to fill `iGroupId`

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate handleGetDispInfoBody**

In `src/ui/main-window.cpp`, find `void MainWindow::handleGetDispInfoBody(NMHDR* hdr)` (line ~2773). After the existing `LVIF_IMAGE` handling and before the `LVIF_TEXT` switch, add:

```cpp
if ((disp->item.mask & LVIF_GROUPID) != 0) {
  const auto gk = sourcePane.groupBy();
  if (gk == fast_explorer::core::GroupKey::None) {
    disp->item.iGroupId = I_GROUPIDNONE;  // ListView's "no group" sentinel
  } else {
    disp->item.iGroupId = fast_explorer::core::groupIdForEntry(
        gk, entry, sourcePane.groupNow());
  }
}
```

Also update the early-return guard at the top of the function (the one that checks `disp->item.mask & (LVIF_TEXT | LVIF_IMAGE)`) to include `LVIF_GROUPID`:

```cpp
if ((disp->item.mask & (LVIF_TEXT | LVIF_IMAGE | LVIF_GROUPID)) == 0) {
  return;
}
```

Add include at top of file (if not already):

```cpp
#include "core/file-grouping.h"
```

- [ ] **Step 2: Build (no new test — this path is exercised via integration)**

Run: `cmake --build build`

Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add src/ui/main-window.cpp
git commit -m "feat(ui): LVN_GETDISPINFO fills iGroupId based on pane's groupBy"
```

---

### Task 13: `applyListViewGroups` — set up native ListView groups for the pane

**Files:**
- Modify: `src/ui/main-window.h`
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Add header declaration**

In `src/ui/main-window.h`, add to private methods near `handleGetDispInfoBody`:

```cpp
// Rebuilds the LVS_OWNERDATA ListView's group definitions to match
// the pane's current groupBy. No-op for GroupKey::None (disables
// group view). Caller invokes after sort completes so visibleOrder
// is already group-clustered.
void applyListViewGroups(std::size_t paneIdx);
```

- [ ] **Step 2: Implement**

Add to `src/ui/main-window.cpp` after `handleGetDispInfoBody`:

```cpp
void MainWindow::applyListViewGroups(std::size_t paneIdx) {
  if (!paneManager_ || paneIdx >= paneManager_->count() ||
      paneIdx >= listViews_.size() || listViews_[paneIdx] == nullptr) {
    return;
  }
  HWND lv = listViews_[paneIdx];
  PaneController& pane = paneManager_->at(paneIdx);
  const auto gk = pane.groupBy();
  // Defensive: turn group view off before mutating definitions, so
  // common-controls doesn't try to render in a half-rebuilt state.
  ListView_EnableGroupView(lv, FALSE);
  ListView_RemoveAllGroups(lv);
  if (gk == fast_explorer::core::GroupKey::None) {
    return;  // grouping disabled
  }
  const auto ids = fast_explorer::core::enumerateGroups(
      gk, pane.store(), pane.groupNow());
  for (const int32_t id : ids) {
    LVGROUP grp{};
    grp.cbSize    = sizeof(grp);
    grp.mask      = LVGF_GROUPID | LVGF_HEADER | LVGF_STATE;
    grp.iGroupId  = id;
    grp.stateMask = LVGS_COLLAPSIBLE;
    grp.state     = LVGS_COLLAPSIBLE;
    std::wstring header = fast_explorer::core::groupTitleForId(
        gk, id, &pane.store(), formatCache_.get());
    grp.pszHeader = const_cast<wchar_t*>(header.c_str());
    grp.cchHeader = static_cast<int>(header.size());
    ListView_InsertGroup(lv, -1, &grp);  // -1 = append
  }
  ListView_EnableGroupView(lv, TRUE);
  // Force dispinfo re-fetch so newly-rendered rows pick up iGroupId.
  const int count = ListView_GetItemCount(lv);
  if (count > 0) {
    ListView_RedrawItems(lv, 0, count - 1);
  }
}
```

If `formatCache_` is not a member (it might be per-pane), adapt to `formatCaches_[paneIdx].get()` or the actual ownership pattern.

- [ ] **Step 3: Build**

Run: `cmake --build build`

Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add src/ui/main-window.h src/ui/main-window.cpp
git commit -m "feat(ui): applyListViewGroups rebuilds native group definitions"
```

---

### Task 14: Hook `applyListViewGroups` into `finalizeSortApply`

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate finalizeSortApply**

In `src/ui/main-window.cpp`, find `void MainWindow::finalizeSortApply(std::size_t paneIdx)` (declared in main-window.h). At the END of that function (after the existing header-arrow + redraw work), append:

```cpp
applyListViewGroups(paneIdx);
```

This ensures every sort completion (sync or async, fresh sort or reapply-after-enum) rebuilds group definitions. For `GroupKey::None` the call is a cheap no-op (only does `EnableGroupView(FALSE)` + `RemoveAllGroups`).

- [ ] **Step 2: Build + run app manually**

Run: `cmake --build build`

Launch `build/FastExplorer.exe`. Verify the existing sort behaviour is unchanged for default state (no groups visible). Click a column header — verify still sorts correctly.

- [ ] **Step 3: Commit**

```bash
git add src/ui/main-window.cpp
git commit -m "feat(ui): finalizeSortApply rebuilds groups after every sort"
```

---

### Task 15: Empty-area right-click → "분류 방법" context menu

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate handleListViewRightClick**

Find `void MainWindow::handleListViewRightClick(NMHDR* hdr)` (line ~2624). Currently it handles iItem >= 0 (file context menu) and iItem == -1 (keyboard-invoked anchor fallback). Modify to dispatch the empty-area path to a new submenu.

Add at the top, after the pane resolution and before the keyboard-anchor fallback:

```cpp
auto* nmia = reinterpret_cast<NMITEMACTIVATE*>(hdr);
if (nmia->iItem < 0) {
  // Mouse right-click on empty area. ptAction is in client coords;
  // convert to screen coords for TrackPopupMenuEx.
  POINT screenPt = nmia->ptAction;
  if (screenPt.x == -1 && screenPt.y == -1) {
    // Keyboard-invoked (Shift+F10) over empty area — anchor to
    // listview origin in screen coords.
    RECT lvRect{};
    GetWindowRect(hdr->hwndFrom, &lvRect);
    screenPt.x = lvRect.left;
    screenPt.y = lvRect.top;
  } else {
    ClientToScreen(hdr->hwndFrom, &screenPt);
  }
  showGroupByContextMenu(paneIdx, screenPt);
  return;
}
```

Add a new private method to the header:

```cpp
// Builds and tracks the empty-area popup (currently just the
// "분류 방법" submenu). Synchronous TrackPopupMenuEx.
void showGroupByContextMenu(std::size_t paneIdx, POINT screenPt);
```

- [ ] **Step 2: Implement showGroupByContextMenu**

Add to `src/ui/main-window.cpp`:

```cpp
void MainWindow::showGroupByContextMenu(std::size_t paneIdx, POINT screenPt) {
  if (!paneManager_ || paneIdx >= paneManager_->count()) return;
  const auto cur = paneManager_->at(paneIdx).groupBy();
  HMENU root = CreatePopupMenu();
  HMENU sub  = CreatePopupMenu();
  AppendMenuW(sub, MF_STRING, kMenuGroupByNone,     L"(없음)");
  AppendMenuW(sub, MF_STRING, kMenuGroupByName,     L"이름");
  AppendMenuW(sub, MF_STRING, kMenuGroupByModified, L"수정한 날짜");
  AppendMenuW(sub, MF_STRING, kMenuGroupByType,     L"유형");
  // Radio-check the current selection.
  const UINT curId =
      cur == fast_explorer::core::GroupKey::Name     ? kMenuGroupByName
    : cur == fast_explorer::core::GroupKey::Modified ? kMenuGroupByModified
    : cur == fast_explorer::core::GroupKey::Type     ? kMenuGroupByType
    :                                                  kMenuGroupByNone;
  CheckMenuRadioItem(sub, kMenuGroupByNone, kMenuGroupByType,
                     curId, MF_BYCOMMAND);
  AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(sub),
              L"분류 방법");
  TrackPopupMenuEx(root, TPM_RIGHTBUTTON, screenPt.x, screenPt.y,
                   hwnd_, nullptr);
  DestroyMenu(root);  // also destroys the submenu
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build`

Expected: clean compile.

- [ ] **Step 4: Manual verify**

Launch the app, right-click on empty area of any pane → "분류 방법" submenu appears with 4 items, current "(없음)" radio-checked.

- [ ] **Step 5: Commit**

```bash
git add src/ui/main-window.h src/ui/main-window.cpp
git commit -m "feat(ui): empty-area right-click shows 분류 방법 submenu"
```

---

### Task 16: WM_COMMAND dispatch for menu IDs → `setGroupBy`

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate onCommand**

Find `LRESULT MainWindow::onCommand(...)` (around line 1800-2100, called from handleMessage). Find the existing switch on `LOWORD(wParam)`. Add cases:

```cpp
case kMenuGroupByNone:
case kMenuGroupByName:
case kMenuGroupByModified:
case kMenuGroupByType: {
  if (!paneManager_) return 0;
  const std::size_t idx = paneManager_->activeIndex();
  if (idx >= paneManager_->count()) return 0;
  using fast_explorer::core::GroupKey;
  GroupKey gk = GroupKey::None;
  switch (LOWORD(wParam)) {
    case kMenuGroupByName:     gk = GroupKey::Name;     break;
    case kMenuGroupByModified: gk = GroupKey::Modified; break;
    case kMenuGroupByType:     gk = GroupKey::Type;     break;
    default:                   gk = GroupKey::None;     break;
  }
  paneManager_->at(idx).setGroupBy(gk);
  // The setGroupBy call triggers requestSort, whose completion path
  // (finalizeSortApply) invokes applyListViewGroups — so the visual
  // update lands automatically when the sort returns.
  return 0;
}
```

- [ ] **Step 2: Build**

Run: `cmake --build build`

Expected: clean compile.

- [ ] **Step 3: Manual verify**

Launch the app, right-click empty area → 분류 방법 > 수정한 날짜. Group headers ("오늘", "어제", ...) should appear.

- [ ] **Step 4: Commit**

```bash
git add src/ui/main-window.cpp
git commit -m "feat(ui): WM_COMMAND for 분류 방법 menu IDs sets pane.groupBy"
```

---

### Task 17: `clearListViewForNavigation` disables groups; re-apply after first batch

**Files:**
- Modify: `src/ui/main-window.cpp`

- [ ] **Step 1: Locate clearListViewForNavigation**

Find `void MainWindow::clearListViewForNavigation(std::size_t paneIdx)`. Add at the end:

```cpp
// Disable group view during the navigation churn so common-controls
// doesn't try to render against stale group IDs while the store is
// being repopulated. finalizeSortApply will re-enable groups via
// applyListViewGroups once the new content's sort completes.
if (paneIdx < listViews_.size() && listViews_[paneIdx] != nullptr) {
  ListView_EnableGroupView(listViews_[paneIdx], FALSE);
  ListView_RemoveAllGroups(listViews_[paneIdx]);
}
```

The `pane.groupBy()` setting itself is intentionally NOT cleared — it survives navigation so the new folder gets the same grouping policy applied after enum completes.

- [ ] **Step 2: Verify the sort-complete path already triggers `applyListViewGroups`**

In Task 14 we hooked `applyListViewGroups(paneIdx)` into `finalizeSortApply`. The post-enum reapply runs through `reapplyPersistedSort()` → `sortCoord_.reapplyAfterEnumeration()` → which posts `kWmFeSortComplete` → which calls `finalizeSortApply`. So no additional wiring is needed: groups rebuild automatically.

- [ ] **Step 3: Build + manual test**

Run: `cmake --build build`

Launch app, set grouping = Modified on a folder, navigate to a different folder via address bar. Groups should disappear briefly during navigation, then reappear in the new folder.

- [ ] **Step 4: Commit**

```bash
git add src/ui/main-window.cpp
git commit -m "feat(ui): clear group state on navigation; re-applied after enum"
```

---

## Phase D — Manual integration verification

### Task 18: Walk through the spec's integration checklist

This task has no code — it's a verification gate before declaring v0.6.0 ready.

**Test folder suggestions:**
- Project root `C:\Users\SOOJANG\dev\github\fast-explorer` — mixed Hangul (none here actually; pick a folder with Korean filenames if available)
- `C:\Windows` — large mixed-type folder
- `C:\Windows\WinSxS` — stress-scale folder (10k+ items)

- [ ] **Step 1: Run all unit tests one last time**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: 100% pass.

- [ ] **Step 2: Launch + walk the checklist**

Launch `build/FastExplorer.exe`. For each item, confirm pass/fail. If any item fails, file a follow-up commit (don't move on with the failure outstanding).

1. **Empty-area right-click → submenu** appears with 4 items, current selection radio-checked.
2. **Select 수정한 날짜** → group headers ("오늘", "어제", ...) render; items rebucket; expand/collapse arrow on each header works.
3. **Click "이름" column header while grouped** → groups stay; items reorder *within* each group; group headers don't move.
4. **Ctrl+F filter** to a substring → groups recompute to show only groups with matching items; headers for empty groups don't render.
5. **Address-bar navigate** to a new folder → groups persist (same policy applies); brief flicker during navigation is acceptable.
6. **F5 refresh** → groups rebuild without visible flicker.
7. **Large folder (`C:\Windows\WinSxS` or similar)** → 첫 화면 < 100ms after selecting group key. (Subjective; if visibly slow, investigate.)
8. **Multi-pane** → set different group keys in two panes; switch active pane and verify each pane retains its own setting.
9. **Dark mode** (toggle if present, else verify under current theme) → group headers legible, contrast acceptable.
10. **Type a letter while grouped** (LVN_ODFINDITEM path) → focus jumps to first matching item across all visible groups; group headers are skipped.

- [ ] **Step 3: Record outcome**

If all 10 pass: commit a marker:

```bash
git commit --allow-empty -m "chore: v0.6.0 file grouping integration checklist complete"
```

If any fail: triage. Common likely fixes:
- Header strings clipped → check `cchHeader` is set; widen via `LVGF_TITLEIMAGE` or accept clipping if Win Explorer also clips
- Type group headers showing wrong labels for hashed extension IDs → verify `groupTitleForId` (Task 7) lookup walks the store correctly
- Group view doesn't appear at all → verify `LVS_OWNERDATA` + `LVM_ENABLEGROUPVIEW` ordering (some MSDN reports say groups must be inserted BEFORE enabling — re-check Task 13 order)

---

## After implementation: release v0.6.0

Out of scope for the task list, but for the human reading this plan: bump `project(VERSION 0.5.0)` → `0.6.0` in `CMakeLists.txt`, commit as `chore: bump version to v0.6.0`, push `main`, tag `v0.6.0`, push tag. The release.yml workflow handles the rest (mirrors the v0.5.0 cycle).

---

## Self-review notes (left in plan for transparency)

- Spec coverage: Phase A covers groupIdForEntry/enumerateGroups/groupTitleForId/compareWithGroup. Phase B covers PaneSortCoordinator + PaneController changes. Phase C covers all Win32 surface (handleGetDispInfoBody, applyListViewGroups, context menu, WM_COMMAND, navigation hook). Phase D covers manual verification.
- Risk from spec (LVS_OWNERDATA + groups quirks): addressed via Phase D #7 large-folder test and explicit triage notes.
- DST/midnight risk: addressed by capturing `now` once in `PaneController::setGroupBy` (Task 10) and threading it through to both the comparator (Task 9) and `applyListViewGroups`'s `enumerateGroups` call (Task 13 reads `pane.groupNow()`).
- Type-to-navigate interaction (just-shipped LVN_ODFINDITEMW): no code path collision; verified in Phase D #10.
