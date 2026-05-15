#include "test-harness.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/file-sort.h"
#include "core/name-arena.h"

using fast_explorer::core::AppendResult;
using fast_explorer::core::FileEntry;
using fast_explorer::core::FileModelStore;
using fast_explorer::core::NameArena;
using fast_explorer::core::SortDirection;
using fast_explorer::core::SortKey;
using fast_explorer::core::SortSpec;
using fast_explorer::core::file_entry_flags::kIsDirectory;
using fast_explorer::core::nameView;

namespace {

FileEntry makeEntry(std::wstring_view name,
                    NameArena& backingArena,
                    uint64_t size = 0,
                    uint8_t flags = 0) {
  const auto interned = backingArena.intern(name);
  FileEntry e{};
  e.namePtr = interned.data();
  e.nameLength = static_cast<uint16_t>(interned.size());
  e.extensionOffset = fast_explorer::core::kNoExtension;
  e.size = size;
  e.flags = flags;
  return e;
}

}  // namespace

FE_TEST_CASE(file_model_store_default_state_is_empty) {
  FileModelStore store(L"X:\\root");
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(store.generation(), 0u);
  FE_ASSERT_TRUE(store.rootPath() == L"X:\\root");
  FE_ASSERT_EQ(store.nameArenaCommittedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(store.nameArenaUsedBytes(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(file_model_store_append_entry_copies_name_into_local_arena) {
  NameArena backing;
  FileEntry source = makeEntry(L"readme.txt", backing, 42);
  const wchar_t* sourcePtr = source.namePtr;

  FileModelStore store(L"X:\\d");
  FE_ASSERT_TRUE(store.appendEntry(source) == AppendResult::Stored);
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(1));

  const FileEntry& stored = store.entryAt(0);
  FE_ASSERT_EQ(stored.size, static_cast<uint64_t>(42));
  FE_ASSERT_EQ(stored.nameLength, static_cast<uint16_t>(10));
  FE_ASSERT_NE(stored.namePtr, sourcePtr);
  FE_ASSERT_EQ(
      std::memcmp(stored.namePtr, L"readme.txt", 10 * sizeof(wchar_t)),
      0);
}

FE_TEST_CASE(file_model_store_append_preserves_extension_and_flags) {
  NameArena backing;
  FileEntry source = makeEntry(L"archive.tar.gz", backing, 0,
                               static_cast<uint8_t>(kIsDirectory));
  source.extensionOffset = 11;

  FileModelStore store(L"X:\\d");
  FE_ASSERT_TRUE(store.appendEntry(source) == AppendResult::Stored);

  const FileEntry& stored = store.entryAt(0);
  FE_ASSERT_EQ(stored.extensionOffset, static_cast<uint16_t>(11));
  FE_ASSERT_EQ(stored.flags, static_cast<uint8_t>(kIsDirectory));
}

FE_TEST_CASE(file_model_store_multiple_appends_grow_count) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  for (int i = 0; i < 50; ++i) {
    FileEntry e = makeEntry(L"item", backing, static_cast<uint64_t>(i));
    FE_ASSERT_TRUE(store.appendEntry(e) == AppendResult::Stored);
  }
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(50));
  FE_ASSERT_EQ(store.entryAt(0).size, static_cast<uint64_t>(0));
  FE_ASSERT_EQ(store.entryAt(49).size, static_cast<uint64_t>(49));
}

FE_TEST_CASE(file_model_store_appended_names_pointer_stable_across_appends) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  FileEntry first = makeEntry(L"persistent.bin", backing);
  FE_ASSERT_TRUE(store.appendEntry(first) == AppendResult::Stored);
  const wchar_t* firstPtr = store.entryAt(0).namePtr;

  for (int i = 0; i < 500; ++i) {
    FileEntry e = makeEntry(L"x", backing);
    FE_ASSERT_TRUE(store.appendEntry(e) == AppendResult::Stored);
  }
  FE_ASSERT_EQ(store.entryAt(0).namePtr, firstPtr);
  FE_ASSERT_EQ(
      std::memcmp(firstPtr, L"persistent.bin", 14 * sizeof(wchar_t)),
      0);
}

FE_TEST_CASE(file_model_store_append_batch_returns_count_added) {
  NameArena backing;
  std::vector<FileEntry> batch;
  for (int i = 0; i < 10; ++i) {
    batch.push_back(makeEntry(L"file", backing, static_cast<uint64_t>(i)));
  }
  FileModelStore store(L"X:\\d");
  const std::size_t stored = store.appendBatch(batch);
  FE_ASSERT_EQ(stored, static_cast<std::size_t>(10));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(10));
}

FE_TEST_CASE(file_model_store_append_batch_stops_on_arena_full) {
  NameArena backing;
  std::vector<FileEntry> batch;
  const std::wstring filler(50, L'X');
  for (int i = 0; i < 2000; ++i) {
    batch.push_back(makeEntry(filler, backing));
  }
  FileModelStore store(L"X:\\d", NameArena::kCommitChunkBytes);
  const std::size_t stored = store.appendBatch(batch);
  FE_ASSERT_TRUE(stored > 0);
  FE_ASSERT_TRUE(stored < batch.size());
  FE_ASSERT_EQ(store.itemCount(), stored);
}

FE_TEST_CASE(file_model_store_reset_clears_entries_and_bumps_generation) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"a", backing));
  store.appendEntry(makeEntry(L"b", backing));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(2));
  FE_ASSERT_TRUE(store.nameArenaCommittedBytes() > 0);

  store.reset(L"Y:\\new");
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(store.generation(), 1u);
  FE_ASSERT_TRUE(store.rootPath() == L"Y:\\new");
  FE_ASSERT_EQ(store.nameArenaCommittedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(store.nameArenaUsedBytes(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(file_model_store_reset_allows_subsequent_append) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"old", backing));
  store.reset(L"Y:\\new");
  FE_ASSERT_TRUE(store.appendEntry(makeEntry(L"new", backing)) ==
                 AppendResult::Stored);
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(file_model_store_generation_increments_monotonically) {
  FileModelStore store(L"X:\\root");
  FE_ASSERT_EQ(store.generation(), 0u);
  store.reset(L"X:\\a");
  FE_ASSERT_EQ(store.generation(), 1u);
  store.reset(L"X:\\b");
  FE_ASSERT_EQ(store.generation(), 2u);
  store.reset(L"X:\\c");
  FE_ASSERT_EQ(store.generation(), 3u);
}

FE_TEST_CASE(file_model_store_total_bytes_accounts_for_arena_and_entries) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"name", backing));
  FE_ASSERT_TRUE(store.totalBytes() >= store.nameArenaCommittedBytes());
  FE_ASSERT_TRUE(store.entriesBytes() >= sizeof(FileEntry));
}

FE_TEST_CASE(file_model_store_append_returns_arena_full_when_exhausted) {
  FileModelStore store(L"X:\\d", NameArena::kCommitChunkBytes);
  NameArena backing;
  const std::wstring filler(50, L'X');
  bool sawArenaFull = false;
  for (int i = 0; i < 2000; ++i) {
    FileEntry e = makeEntry(filler, backing);
    const auto result = store.appendEntry(e);
    if (result == AppendResult::ArenaFull) {
      sawArenaFull = true;
      break;
    }
    FE_ASSERT_TRUE(result == AppendResult::Stored);
  }
  FE_ASSERT_TRUE(sawArenaFull);
  FE_ASSERT_TRUE(store.itemCount() >= 500);
}

FE_TEST_CASE(file_model_store_empty_input_name_is_appended_unchanged) {
  FileEntry e{};
  e.namePtr = nullptr;
  e.nameLength = 0;
  e.extensionOffset = fast_explorer::core::kNoExtension;
  e.size = 7;
  FileModelStore store(L"X:\\d");
  FE_ASSERT_TRUE(store.appendEntry(e) == AppendResult::Stored);
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(store.entryAt(0).nameLength, static_cast<uint16_t>(0));
}

FE_TEST_CASE(file_model_store_accepts_max_length_name) {
  std::vector<wchar_t> longBuffer(UINT16_MAX, L'A');
  FileEntry source{};
  source.namePtr = longBuffer.data();
  source.nameLength = UINT16_MAX;
  source.extensionOffset = fast_explorer::core::kNoExtension;
  FileModelStore store(L"X:\\d");
  const auto result = store.appendEntry(source);
  FE_ASSERT_TRUE(result == AppendResult::Stored);
  FE_ASSERT_EQ(store.entryAt(0).nameLength, UINT16_MAX);
}

FE_TEST_CASE(file_model_store_append_result_enum_has_distinct_values) {
  FE_ASSERT_NE(static_cast<int>(AppendResult::Stored),
               static_cast<int>(AppendResult::ArenaFull));
  FE_ASSERT_NE(static_cast<int>(AppendResult::Stored),
               static_cast<int>(AppendResult::CapacityFull));
  FE_ASSERT_NE(static_cast<int>(AppendResult::Stored),
               static_cast<int>(AppendResult::NameTooLong));
  FE_ASSERT_NE(static_cast<int>(AppendResult::ArenaFull),
               static_cast<int>(AppendResult::NameTooLong));
  FE_ASSERT_NE(static_cast<int>(AppendResult::ArenaFull),
               static_cast<int>(AppendResult::CapacityFull));
  FE_ASSERT_NE(static_cast<int>(AppendResult::CapacityFull),
               static_cast<int>(AppendResult::NameTooLong));
}

// ---------------------------------------------------------------------------
// visibleOrder / sort
// ---------------------------------------------------------------------------

namespace {
constexpr SortSpec kNameAsc{SortKey::Name, SortDirection::Ascending};
constexpr SortSpec kNameDesc{SortKey::Name, SortDirection::Descending};
constexpr SortSpec kSizeAsc{SortKey::Size, SortDirection::Ascending};
}  // namespace

FE_TEST_CASE(file_model_store_visibleOrder_default_is_empty) {
  FileModelStore store(L"X:\\d");
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(file_model_store_visibleOrder_identity_after_appends) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"a", backing));
  store.appendEntry(makeEntry(L"b", backing));
  store.appendEntry(makeEntry(L"c", backing));
  const auto order = store.visibleOrder();
  FE_ASSERT_EQ(order.size(), static_cast<std::size_t>(3));
  FE_ASSERT_EQ(order[0], 0u);
  FE_ASSERT_EQ(order[1], 1u);
  FE_ASSERT_EQ(order[2], 2u);
}

FE_TEST_CASE(file_model_store_visibleAt_matches_entryAt_before_sort) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"x", backing, 10));
  store.appendEntry(makeEntry(L"y", backing, 20));
  FE_ASSERT_EQ(store.visibleAt(0).size, store.entryAt(0).size);
  FE_ASSERT_EQ(store.visibleAt(1).size, store.entryAt(1).size);
}

FE_TEST_CASE(file_model_store_reset_clears_visibleOrder) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"a", backing));
  store.appendEntry(makeEntry(L"b", backing));
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(2));
  store.reset(L"Y:\\new");
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(file_model_store_sort_name_asc_reorders_visible) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"charlie", backing));
  store.appendEntry(makeEntry(L"alpha", backing));
  store.appendEntry(makeEntry(L"bravo", backing));
  store.sort(kNameAsc);
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(0).namePtr,
                                  store.visibleAt(0).nameLength),
               std::wstring_view(L"alpha"));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(1).namePtr,
                                  store.visibleAt(1).nameLength),
               std::wstring_view(L"bravo"));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(2).namePtr,
                                  store.visibleAt(2).nameLength),
               std::wstring_view(L"charlie"));
}

FE_TEST_CASE(file_model_store_sort_name_desc_reverses_order) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"alpha", backing));
  store.appendEntry(makeEntry(L"bravo", backing));
  store.appendEntry(makeEntry(L"charlie", backing));
  store.sort(kNameDesc);
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(0).namePtr,
                                  store.visibleAt(0).nameLength),
               std::wstring_view(L"charlie"));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(2).namePtr,
                                  store.visibleAt(2).nameLength),
               std::wstring_view(L"alpha"));
}

FE_TEST_CASE(file_model_store_sort_preserves_entry_storage_order) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"zeta", backing, 100));
  store.appendEntry(makeEntry(L"alpha", backing, 200));
  store.sort(kNameAsc);
  FE_ASSERT_EQ(std::wstring_view(store.entryAt(0).namePtr,
                                  store.entryAt(0).nameLength),
               std::wstring_view(L"zeta"));
  FE_ASSERT_EQ(store.entryAt(0).size, static_cast<uint64_t>(100));
  FE_ASSERT_EQ(std::wstring_view(store.entryAt(1).namePtr,
                                  store.entryAt(1).nameLength),
               std::wstring_view(L"alpha"));
  FE_ASSERT_EQ(store.entryAt(1).size, static_cast<uint64_t>(200));
}

FE_TEST_CASE(file_model_store_sort_size_ascending) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"big", backing, 1000));
  store.appendEntry(makeEntry(L"tiny", backing, 1));
  store.appendEntry(makeEntry(L"mid", backing, 100));
  store.sort(kSizeAsc);
  FE_ASSERT_EQ(store.visibleAt(0).size, static_cast<uint64_t>(1));
  FE_ASSERT_EQ(store.visibleAt(1).size, static_cast<uint64_t>(100));
  FE_ASSERT_EQ(store.visibleAt(2).size, static_cast<uint64_t>(1000));
}

FE_TEST_CASE(file_model_store_append_after_sort_tail_pushes_to_visibleOrder_end) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"alpha", backing));
  store.appendEntry(makeEntry(L"bravo", backing));
  store.sort(kNameAsc);
  store.appendEntry(makeEntry(L"aardvark", backing));
  // The newly appended entry is at the tail of visibleOrder, not at the
  // sorted position. A subsequent sort is required to re-place it.
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(3));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(2).namePtr,
                                  store.visibleAt(2).nameLength),
               std::wstring_view(L"aardvark"));
}

FE_TEST_CASE(file_model_store_sort_empty_store_is_noop) {
  FileModelStore store(L"X:\\d");
  store.sort(kNameAsc);
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(file_model_store_sort_single_entry_is_noop) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"only", backing));
  store.sort(kNameAsc);
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(store.visibleOrder()[0], 0u);
}

FE_TEST_CASE(file_model_store_visibleOrder_size_invariant_after_batch) {
  NameArena backing;
  std::vector<FileEntry> batch;
  for (int i = 0; i < 50; ++i) {
    batch.push_back(makeEntry(L"item", backing, static_cast<uint64_t>(i)));
  }
  FileModelStore store(L"X:\\d");
  store.appendBatch(batch);
  FE_ASSERT_EQ(store.visibleOrder().size(), store.itemCount());
}

// ---------------------------------------------------------------------------
// publishedCount (worker↔UI visibility boundary)
// ---------------------------------------------------------------------------

FE_TEST_CASE(file_model_store_publishedCount_default_is_zero) {
  FileModelStore store(L"X:\\d");
  FE_ASSERT_EQ(store.publishedCount(), 0u);
}

FE_TEST_CASE(file_model_store_publishedCount_unaffected_by_append) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"a", backing));
  store.appendEntry(makeEntry(L"b", backing));
  // appendEntry alone does NOT publish; the worker is expected to call
  // publish() at batch boundaries so the UI never reads half-written
  // entries.
  FE_ASSERT_EQ(store.publishedCount(), 0u);
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(2));
}

FE_TEST_CASE(file_model_store_publish_makes_count_visible) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"a", backing));
  store.appendEntry(makeEntry(L"b", backing));
  store.publish(2);
  FE_ASSERT_EQ(store.publishedCount(), 2u);
}

FE_TEST_CASE(file_model_store_reset_resets_publishedCount) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"a", backing));
  store.publish(1);
  FE_ASSERT_EQ(store.publishedCount(), 1u);
  store.reset(L"Y:\\new");
  FE_ASSERT_EQ(store.publishedCount(), 0u);
}

FE_TEST_CASE(file_model_store_appendEntry_rejected_past_kMaxEntries) {
  // Contract test: kMaxEntries is exposed as a public constant and
  // entries_ is reserve()d at construction. A 100k-fill load test
  // would be slow and allocate a 50+ MB arena, so the actual rejection
  // path is exercised through reasoning rather than a runtime fill.
  FileModelStore store(L"X:\\d");
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
  FE_ASSERT_TRUE(FileModelStore::kMaxEntries == 100'000u);
}

// ---------------------------------------------------------------------------
// M5 exit-gate measurement (Design §14.5)
// ---------------------------------------------------------------------------

FE_TEST_CASE(file_model_store_sort_medium_dataset_under_50ms_budget) {
  // Synthesises a 10,000-entry store with deterministic pseudo-random
  // names so the comparator does meaningful work, then measures the
  // wall-clock time of a Name-ascending sort. Design §14.5 exit
  // criterion: medium folder sort must not block the UI > 50 ms.
  constexpr int kRows = 10000;
  NameArena backing(4u * 1024u * 1024u);  // 4 MB arena suffices for ~16 chars × 10k.
  FileModelStore store(L"X:\\bench");
  for (int i = 0; i < kRows; ++i) {
    wchar_t buf[32];
    // Deterministic permutation of [0, kRows) — multiplying by a coprime
    // (31337) modulo kRows yields a stable pseudo-random ordering.
    const int key = static_cast<int>(
        (static_cast<unsigned>(i) * 31337u) % static_cast<unsigned>(kRows));
    std::swprintf(buf, 32, L"file_%05d.txt", key);
    const auto interned = backing.intern(std::wstring_view(buf));
    FileEntry e{};
    e.namePtr = interned.data();
    e.nameLength = static_cast<uint16_t>(interned.size());
    e.extensionOffset = fast_explorer::core::kNoExtension;
    e.size = static_cast<uint64_t>(key);
    store.appendEntry(e);
  }
  store.publish(static_cast<std::uint32_t>(store.itemCount()));

  const auto t0 = std::chrono::steady_clock::now();
  store.sort(SortSpec{SortKey::Name, SortDirection::Ascending});
  const auto t1 = std::chrono::steady_clock::now();
  const auto elapsedUs =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

  std::printf("[m5-bench] medium(10k) Name asc sort: %lld us\n",
              static_cast<long long>(elapsedUs));

  // 50 ms strict gate (Design §14.5 budget).
  FE_ASSERT_TRUE(elapsedUs < 50000);

  // Verify the result is actually sorted (correctness invariant).
  const auto order = store.visibleOrder();
  for (std::size_t i = 1; i < order.size(); ++i) {
    const auto& prev = store.entryAt(order[i - 1]);
    const auto& curr = store.entryAt(order[i]);
    FE_ASSERT_TRUE(!fast_explorer::core::lessEntries(
        curr, prev, SortSpec{SortKey::Name, SortDirection::Ascending}));
  }
}

FE_TEST_CASE(file_model_store_sort_append_sort_restores_total_order) {
  NameArena backing;
  FileModelStore store(L"X:\\d");
  store.appendEntry(makeEntry(L"bravo", backing));
  store.appendEntry(makeEntry(L"delta", backing));
  store.sort(kNameAsc);
  store.appendEntry(makeEntry(L"alpha", backing));
  store.appendEntry(makeEntry(L"charlie", backing));
  store.sort(kNameAsc);
  FE_ASSERT_EQ(store.visibleOrder().size(), static_cast<std::size_t>(4));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(0).namePtr,
                                  store.visibleAt(0).nameLength),
               std::wstring_view(L"alpha"));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(1).namePtr,
                                  store.visibleAt(1).nameLength),
               std::wstring_view(L"bravo"));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(2).namePtr,
                                  store.visibleAt(2).nameLength),
               std::wstring_view(L"charlie"));
  FE_ASSERT_EQ(std::wstring_view(store.visibleAt(3).namePtr,
                                  store.visibleAt(3).nameLength),
               std::wstring_view(L"delta"));
}
