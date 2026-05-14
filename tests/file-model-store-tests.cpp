#include "test-harness.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/name-arena.h"

using fast_explorer::core::AppendResult;
using fast_explorer::core::FileEntry;
using fast_explorer::core::FileModelStore;
using fast_explorer::core::NameArena;
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
               static_cast<int>(AppendResult::NameTooLong));
  FE_ASSERT_NE(static_cast<int>(AppendResult::ArenaFull),
               static_cast<int>(AppendResult::NameTooLong));
}
