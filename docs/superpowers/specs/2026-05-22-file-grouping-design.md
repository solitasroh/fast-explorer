# v0.6.0 Design — File Grouping ("분류 방법")

Status: draft for review
Date: 2026-05-22
Source: brainstorming session 2026-05-22

## Goal

Add Windows 11 Explorer parity for the "분류 방법" (Group by) feature: a right-click context menu on the list-view's empty area lets the user group items under collapsible headers by one of three keys (Name first-letter, Modified date bucket, Type). The current column-header sort behaviour is unchanged — grouping is a secondary axis layered on top of sort.

User request (verbatim): "그리고 파일 분류 방법을 추가해줬으면 해. 윈도우 탐색기에서와 같은 동작을 하고 싶어." + screenshot of Win11 Korean Explorer "분류 방법" submenu.

## Decisions made in brainstorming

| Item | Decision |
|---|---|
| Korean menu label | "분류 방법" (Win11 Korean Explorer's actual term for "Group by") |
| Group keys supported | None, Name (first letter), Modified (date bucket), Type (folder + extension) |
| Group keys deferred | Size, Created date, Author, Category, Tag, Title (would require FileEntry expansion or shell property store; out of scope) |
| UI surface | Empty-area right-click context menu only. No hamburger entry, no keyboard shortcut. Item right-click still shows the existing file context menu. |
| Persistence | Per-pane, session-only. No `settings.json` schema change. App restart resets group state. |
| Rendering | Native ListView grouping (`LVM_ENABLEGROUPVIEW` + `LVM_INSERTGROUPW`). `iGroupId` per row via `LVN_GETDISPINFO` callback (LVS_OWNERDATA-compatible). |
| Sort interaction | Group is the primary ordering key; existing SortKey becomes secondary (within-group). Changing SortKey while grouped only re-sorts within groups; group definitions stay. |
| Folder navigation | Group key persists across navigates within the same pane. New folder enumeration re-applies the current group key after the first batch arrives. |
| Filter interaction | Groups are computed against the displayed subset (post-filter). Empty groups are not rendered. |

## Architecture

### Component responsibilities

| Component | Responsibility | v0.6.0 change |
|---|---|---|
| `core/file-grouping.{h,cpp}` (NEW) | Pure bucketization. `groupIdForEntry`, `groupTitleForId`, `enumerateGroups`. No Win32. | new files |
| `core/file-sort.{h,cpp}` | Existing tri-state comparator | add optional group-aware variant that compares groupId first, falls through to existing logic on tie |
| `ui/pane-controller.{h,cpp}` | Per-pane state, sort dispatch | add `groupBy_` field + `setGroupBy(GroupKey)` + getter; thread groupBy into sort dispatch; notify MainWindow via existing message queue |
| `ui/pane-sort-coordinator.{h,cpp}` | Off-thread sort runner | accept (sortSpec, groupBy, now) tuple; produces visibleOrder with group as primary key |
| `ui/main-window.{h,cpp}` | Win32 surface | extend `handleListViewRightClick` for empty-area path; new `applyListViewGroups(paneIdx)`; extend `handleGetDispInfoBody` for `LVIF_GROUPID`; new menu IDs in `messages.h`; new `kWmFePaneGroupByChanged` message |
| `tests/file-grouping-tests.cpp` (NEW) | Unit tests | new file under existing ctest target |

### Module boundaries

`file-grouping` is the only module that knows about group semantics. PaneController stores the key but treats it opaquely (just passes through to sort). MainWindow turns group IDs into Win32 group definitions. This keeps the Win32 surface thin and the group logic pure-function testable without HWND fixtures.

## API surface

### `core/file-grouping.h`

```cpp
enum class GroupKey : uint8_t {
  None     = 0,  // grouping disabled
  Name     = 1,  // first-letter buckets (Korean choseong + A-Z + digit + other)
  Modified = 2,  // 6 date buckets relative to `now`
  Type     = 3,  // folder + per-extension dynamic groups
};

// Returns the group ID this entry belongs to under `key`.
// `now` is used only for GroupKey::Modified; safe to pass 0 otherwise.
// Caller MUST capture `now` once at sort-start so the result is stable
// across the sort pass even if real wall-clock crosses midnight.
[[nodiscard]] int32_t groupIdForEntry(GroupKey key,
                                      const FileEntry& entry,
                                      uint64_t nowFiletime) noexcept;

// UTF-16 header string for a given group id. Cached by caller; this
// function does not allocate beyond the returned std::wstring.
[[nodiscard]] std::wstring groupTitleForId(GroupKey key, int32_t id,
                                           const FormatCache& cache);

// Walks the store's published portion (respecting displaySubset under
// filter) and returns the group IDs present, in render order. Empty
// groups are not included.
[[nodiscard]] std::vector<int32_t> enumerateGroups(
    GroupKey key, const FileModelStore& store, uint64_t nowFiletime);
```

### `ui/pane-controller.h` (additions)

```cpp
GroupKey groupBy() const noexcept { return groupBy_; }

// Sets the grouping key and triggers a re-sort. Caller (MainWindow)
// receives kWmFePaneGroupByChanged when the sort completes; that's
// the trigger to call applyListViewGroups.
void setGroupBy(GroupKey key);
```

### `ui/messages.h` (additions)

```cpp
inline constexpr UINT kWmFePaneGroupByChanged = kWmFeBase + 0x10;  // next slot after 0x0F (kWmFeFilterDismiss)

inline constexpr WORD kMenuGroupByNone     = 380;
inline constexpr WORD kMenuGroupByName     = 381;
inline constexpr WORD kMenuGroupByModified = 382;
inline constexpr WORD kMenuGroupByType     = 383;
```

Implementation note: prefer `CheckMenuRadioItem(submenu, 0, 3, currentIdx, MF_BYPOSITION)` over per-item `MFS_CHECKED` for radio-group semantics — proper exclusivity, single-call setup.

## Data flow

```
User right-click on empty area
   ↓ NM_RCLICK (NMITEMACTIVATE.iItem == -1)
MainWindow::handleListViewRightClick
   ↓ if empty: build popup with current groupBy_ radio-checked
TrackPopupMenuEx (synchronous)
   ↓ WM_COMMAND with kMenuGroupBy*
MainWindow::onCommand
   ↓ paneManager_->at(activeIdx).setGroupBy(...)
PaneController::setGroupBy
   ↓ groupBy_ = key; requestSort(currentSortSpec)
PaneSortCoordinator (off-thread)
   ↓ comparator wraps existing compareEntries with groupId-first tiebreak
   ↓ produces new visibleOrder
   ↓ posts kWmFeSortComplete back to MainWindow
MainWindow::handleSortComplete (existing path)
   ↓ finalizeSortApply(paneIdx)
   ↓ if pane.groupBy() != None: applyListViewGroups(paneIdx)
applyListViewGroups
   ├─ LVM_ENABLEGROUPVIEW(FALSE) (defensive re-entry guard)
   ├─ LVM_REMOVEALLGROUPS
   ├─ enumerateGroups(groupBy_, store, nowFiletime_)
   ├─ for each id: LVM_INSERTGROUPW{ iGroupId=id, pszHeader=title, state=LVGS_COLLAPSIBLE }
   └─ LVM_ENABLEGROUPVIEW(TRUE) + InvalidateRect
ListView fires LVN_GETDISPINFOW with item.mask containing LVIF_GROUPID
   ↓ MainWindow::handleGetDispInfoBody
   ↓ if (mask & LVIF_GROUPID): disp->item.iGroupId = groupIdForEntry(...)
```

## Group definitions

### Name (first-letter)

| ID range | Bucket | Header |
|---|---|---|
| 0..18 | Korean choseong ㄱ..ㅎ (from `(syllable - 0xAC00) / 588`) | ㄱ, ㄲ, ㄴ, ㄷ, ㄸ, ㄹ, ㅁ, ㅂ, ㅃ, ㅅ, ㅆ, ㅇ, ㅈ, ㅉ, ㅊ, ㅋ, ㅌ, ㅍ, ㅎ |
| 19..44 | Latin A..Z (uppercase-normalized) | A, B, ..., Z |
| 45 | Digits 0..9 | 0 - 9 |
| 46 | Other (symbols, CJK ideographs, emoji, leading whitespace, etc.) | 기타 |

Normalization:
- Hangul compatibility jamo (U+3131..U+314E) at start → map to the same choseong ID via lookup table
- Lowercase Latin → uppercase
- Names starting with whitespace → bucket 46

### Modified (date bucket)

`now` captured once at sort start (FILETIME, UTC) by PaneSortCoordinator. Comparison done after converting `entry.modifiedTime` and `now` to the system's local time zone (to make "오늘" mean "today in user's TZ", matching Win Explorer).

| ID | Bucket | Header |
|---|---|---|
| 0 | now ≥ today_00:00_local | 오늘 |
| 1 | now ≥ yesterday_00:00_local | 어제 |
| 2 | now ≥ this_week_monday_00:00_local | 이번 주 |
| 3 | now ≥ this_month_01_00:00_local | 이번 달 |
| 4 | now ≥ this_year_jan01_00:00_local | 올해 |
| 5 | else | 더 오래전 |

Future-dated files (modifiedTime > now): bucket 0 (오늘). Matches Win Explorer's behaviour of clamping to "오늘" rather than introducing a "미래" bucket.

### Type (folder + extension)

| ID | Bucket | Header |
|---|---|---|
| 0 | `isDirectory(entry)` | 폴더 |
| 1 | extensionOffset == kNoExtension (file with no '.') | 파일 |
| 2..(N+1) | distinct extension strings (case-folded) | `formatCache_->typeForEntry(entry)` |

Extension ID assignment: walk the store once at group-build time. Lowercase extension strings are inserted into a stable map (ext → id); ID values are assigned in encounter order so the same extension always gets the same ID within one `applyListViewGroups` call. Headers are sorted alphabetically by header text in `enumerateGroups`, so the visible order is `폴더 → 파일 → JPG 파일 → PNG 파일 → ...` even if encounter order was different.

## Sort interaction (key invariant)

```cpp
int compareWithGroup(const FileEntry& a, const FileEntry& b,
                     SortSpec spec, GroupKey gk, uint64_t now) {
  if (gk != GroupKey::None) {
    const int ga = groupIdForEntry(gk, a, now);
    const int gb = groupIdForEntry(gk, b, now);
    if (ga != gb) return ga - gb;
  }
  return compareEntries(a, b, spec);  // existing path
}
```

`PaneSortCoordinator` snapshots `(spec, groupBy, now)` at job dispatch. The off-thread comparator is the lambda above. `compareEntries` is untouched — group-awareness is added as a wrapper, not a modification of the existing comparator. This guarantees the sort-only code path stays exactly as it is for `GroupKey::None`.

## Edge cases & policies

| Case | Behaviour |
|---|---|
| Folder navigated to a new path | `groupBy_` retained on PaneController; clear listview group state in `clearListViewForNavigation`; after first enum batch, re-call `applyListViewGroups`. |
| Filter (Ctrl+F) active | `enumerateGroups` walks `displaySubset` (already filter-aware via `store.visibleAt`). Hidden items don't form groups. |
| Column-header click while grouped | Re-sort within groups (`compareWithGroup` produces new order); `applyListViewGroups` NOT re-called (group definitions identical). |
| F5 refresh | After re-enum completes, call `applyListViewGroups` to rebuild group definitions (file set may have changed). |
| Empty folder | `enumerateGroups` returns empty vector; we still `LVM_ENABLEGROUPVIEW(TRUE)` but with zero groups (ListView shows nothing, same as no items). |
| Single-group result (everything is "오늘") | Group header still rendered. Matches Win Explorer. |
| Items added to a grouped folder via fs-watch | Existing fs-watch refresh path triggers re-sort; sort-complete handler re-applies groups. No special path. |
| Inactive pane | `setGroupBy` works on inactive panes too (each pane independent). Active-pane context menu only operates on activeIndex, so inactive groups stay until that pane is selected and changed. |
| DPI change | Group headers are native — common-controls handles. No extra work. |
| Dark mode | Group header colors come from the listview's current theme. Existing dark mode setup (themed listview) covers this. Visual verification on first build. |

## UI specifics

### Context menu construction

```cpp
HMENU root = CreatePopupMenu();
HMENU submenu = CreatePopupMenu();
const GroupKey cur = pane.groupBy();
auto addRadio = [&](WORD id, const wchar_t* label, GroupKey key) {
  MENUITEMINFOW mii{};
  mii.cbSize = sizeof(mii);
  mii.fMask  = MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_STATE;
  mii.fType  = MFT_STRING | MFT_RADIOCHECK;
  mii.fState = (cur == key) ? MFS_CHECKED : MFS_UNCHECKED;
  mii.wID    = id;
  mii.dwTypeData = const_cast<wchar_t*>(label);
  InsertMenuItemW(submenu, GetMenuItemCount(submenu), TRUE, &mii);
};
addRadio(kMenuGroupByNone,     L"(없음)",       GroupKey::None);
addRadio(kMenuGroupByName,     L"이름",         GroupKey::Name);
addRadio(kMenuGroupByModified, L"수정한 날짜",   GroupKey::Modified);
addRadio(kMenuGroupByType,     L"유형",         GroupKey::Type);
AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
            L"분류 방법");
TrackPopupMenuEx(root, TPM_RIGHTBUTTON, screenPt.x, screenPt.y, hwnd_, nullptr);
DestroyMenu(root);
```

### Detecting "empty area"

`NMITEMACTIVATE.iItem == -1` indicates the right-click did not land on a row. This is the existing convention used by `handleListViewRightClick` for the keyboard-invoked context menu (Shift+F10) fallback path. Reuse it.

### Conflict with existing item right-click

Item right-click (iItem >= 0) keeps showing the existing shell context menu (Copy/Cut/Delete/Properties etc.). No change to that path.

## Performance budget

- `enumerateGroups`: O(N) one pass over `displaySubset` or `visibleOrder`. For 100k items: ~500µs (Hangul prefix extraction is a single subtraction + table lookup).
- `LVM_INSERTGROUPW` per call: ~5µs. Practical group count caps at ~50 (Name has 47 buckets max, Modified has 6, Type rarely exceeds 30 in real folders).
- `LVN_GETDISPINFO` with `LVIF_GROUPID`: adds one `groupIdForEntry` call per visible row. ~50ns per row. Inside the existing dispinfo histogram budget.
- No allocation in the hot path: `groupIdForEntry` is allocation-free; `groupTitleForId` only allocates during `applyListViewGroups` (once per group, not per row).

## Testing

### Unit (`tests/file-grouping-tests.cpp`)

- `groupIdForEntry`:
  - Hangul syllable "가나다.txt" → 0, "마라톤.exe" → 6, "ㅎ파일.txt" → 18
  - Compatibility jamo "ㄱ.txt" (U+3131) → 0 (matches "가" via normalization)
  - Latin "Apple" → 19, "zebra" → 44 (case folded), digit "9.txt" → 45, emoji "🚀.txt" → 46
  - Modified buckets at fake `now = 2026-06-15 12:00 KST`: modifiedTime at 2026-06-15 00:01 → 0; 2026-06-14 23:59 → 1; 2026-06-10 → 2 (this week, Mon = 2026-06-08 KST start); 2026-06-01 → 3; 2026-02-01 → 4; 2025-01-01 → 5
  - Modified DST boundary: pick a date inside DST transition; verify bucket determined by local midnight, not UTC
  - Type folder: `isDirectory` true → 0; same extension twice → same ID; different extension → different ID; empty extension twice → same ID (= 1)
- `enumerateGroups`:
  - Empty store → empty vector
  - Subset of buckets used → only those present, in ascending ID order
  - Type with mixed folders + files → 폴더 first, then alphabetically by header
- `compareWithGroup`:
  - Cross-group pair → ordered by groupId
  - Same-group pair → ordered by underlying `compareEntries`

### Integration (manual, documented checklist)

1. Empty-area right-click in a pane with files → "분류 방법" submenu appears, current selection radio-checked
2. Select "수정한 날짜" → group headers ("오늘", "어제", ...) appear, items rebucket
3. Click "이름" column header while grouped → groups stay, items reorder within each group
4. Ctrl+F filter to a substring → groups recompute to show only matching items
5. Navigate via address bar to a new folder → groups persist (still applied to new content)
6. F5 refresh → groups rebuild without visual flicker
7. 100k-item folder (e.g., `C:\Windows\WinSxS` view) → first frame < 100ms after selecting group key
8. Inactive pane retains its own groupBy when active pane switches
9. Dark mode + group headers render legibly (visual sanity)
10. Type a letter while grouped → type-to-navigate still works (LVN_ODFINDITEM unchanged)

## Out of scope (not in v0.6.0)

- Size, Created date, Author, Category, Tag, Title group keys
- Right-click "정렬 기준" submenu (sort is already on column-header click)
- "보기 모드" (Details / List / Tiles) — separate feature
- Persisting group state to `settings.json` — would require SessionState v5 → v6 migration
- Per-folder group memory (Win Explorer's full parity) — requires a separate per-path settings store
- MUI / non-Korean group header strings
- Custom group expansion state persistence

## Risks

| Risk | Mitigation |
|---|---|
| LVS_OWNERDATA + groups has historically had quirks (LVS_AUTOARRANGE conflict, jumpy scroll on huge folders) | We use LVS_REPORT exclusively; first build will exercise the 10k+ scenario from the test checklist before merge. If a quirk surfaces, fall back option is non-virtual groups with `LVM_SETITEM` per row (slower but functional). |
| `now` shifts across sort pass (midnight, DST, NTP adjust) | Capture once into `nowFiletime_` at sort-job dispatch; pass through to all `groupIdForEntry` calls. |
| Hangul compatibility jamo vs syllable mismatch produces split buckets ("ㄱ.txt" vs "가.txt") | Normalization in `groupIdForEntry`; explicit unit test. |
| Group header keyboard focus interferes with type-to-navigate (just-shipped LVN_ODFINDITEMW) | `LVN_ODFINDITEMW` only returns item indices; ListView itself routes group-header keys (collapse/expand) without re-firing the find notification. Integration test #10 verifies. |
| Sort coordinator API change (adding `groupBy` to job payload) breaks existing callers | All call sites go through `requestSort` — a single function. Adding a defaulted parameter is backward-compatible for callers that don't care. |

## Open questions (resolved during brainstorming)

- ~~Sort or Group?~~ → Group (분류 방법 = Win11 Korean "Group by")
- ~~Which group keys?~~ → None / Name / Modified / Type
- ~~UI surface?~~ → Empty-area right-click only
- ~~Persistence?~~ → Per-pane, session-only
- ~~Korean menu label?~~ → "분류 방법" (Win11 native)

No open questions remain at spec-write time.
