#include "test-harness.h"

#include <cstring>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

#include "core/file-entry.h"
#include "core/fs-backend.h"
#include "core/memory-fs-backend.h"

using fast_explorer::core::EnumerationError;
using fast_explorer::core::EnumerationHandle;
using fast_explorer::core::FaultInjectingMemoryFsBackend;
using fast_explorer::core::FileEntry;
using fast_explorer::core::file_entry_flags::kIsDirectory;
using fast_explorer::core::file_entry_flags::kIsHidden;
using fast_explorer::core::MemoryFsBackend;
using fast_explorer::core::Result;

namespace {

std::optional<FileEntry> nextEntry(MemoryFsBackend& backend,
                                    EnumerationHandle& handle) {
  auto r = backend.next(handle, std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  return r.value;
}

}  // namespace

FE_TEST_CASE(memory_fs_open_unknown_path_returns_path_not_found) {
  MemoryFsBackend backend;
  auto r = backend.openEnumeration(L"X:\\unknown", std::stop_token{});
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::PathNotFound));
}

FE_TEST_CASE(memory_fs_open_empty_directory_streams_zero_entries) {
  MemoryFsBackend backend;
  backend.addDirectory(L"X:\\empty");
  auto r = backend.openEnumeration(L"X:\\empty", std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value != nullptr);
  auto e = nextEntry(backend, *r.value);
  FE_ASSERT_FALSE(e.has_value());
}

FE_TEST_CASE(memory_fs_streams_single_entry_then_end) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"readme.txt", 123);
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto& h = *open.value;

  auto first = nextEntry(backend, h);
  FE_ASSERT_TRUE(first.has_value());
  FE_ASSERT_EQ(first->size, static_cast<uint64_t>(123));
  FE_ASSERT_EQ(first->nameLength, static_cast<uint16_t>(10));

  auto end = nextEntry(backend, h);
  FE_ASSERT_FALSE(end.has_value());
}

FE_TEST_CASE(memory_fs_streams_multiple_entries_in_insertion_order) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a.txt", 1);
  backend.addEntry(L"X:\\d", L"b.txt", 2);
  backend.addEntry(L"X:\\d", L"c.txt", 3);
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto& h = *open.value;

  for (uint64_t expected : {static_cast<uint64_t>(1),
                            static_cast<uint64_t>(2),
                            static_cast<uint64_t>(3)}) {
    auto e = nextEntry(backend, h);
    FE_ASSERT_TRUE(e.has_value());
    FE_ASSERT_EQ(e->size, expected);
  }
  auto end = nextEntry(backend, h);
  FE_ASSERT_FALSE(end.has_value());
}

FE_TEST_CASE(memory_fs_entry_name_pointer_stays_valid_across_next_calls) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"persistent.bin");
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto first = nextEntry(backend, *open.value);
  FE_ASSERT_TRUE(first.has_value());
  const wchar_t* firstPtr = first->namePtr;
  FE_ASSERT_TRUE(firstPtr != nullptr);
  FE_ASSERT_EQ(
      std::memcmp(firstPtr, L"persistent.bin", 14 * sizeof(wchar_t)),
      0);
}

FE_TEST_CASE(memory_fs_entry_size_and_flags_round_trip) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"folder", 0,
                   static_cast<uint8_t>(kIsDirectory | kIsHidden));
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto e = nextEntry(backend, *open.value);
  FE_ASSERT_TRUE(e.has_value());
  FE_ASSERT_EQ(e->size, static_cast<uint64_t>(0));
  FE_ASSERT_EQ(static_cast<int>(e->flags),
               static_cast<int>(kIsDirectory | kIsHidden));
}

FE_TEST_CASE(memory_fs_add_entry_creates_directory_implicitly) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\new", L"a", 99);
  auto open = backend.openEnumeration(L"X:\\new", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto e = nextEntry(backend, *open.value);
  FE_ASSERT_TRUE(e.has_value());
  FE_ASSERT_EQ(e->size, static_cast<uint64_t>(99));
}

FE_TEST_CASE(memory_fs_open_with_stop_requested_returns_canceled) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"x", 1);
  std::stop_source src;
  src.request_stop();
  auto r = backend.openEnumeration(L"X:\\d", src.get_token());
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::Canceled));
}

FE_TEST_CASE(memory_fs_next_with_stop_requested_returns_canceled) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"x", 1);
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());

  std::stop_source src;
  src.request_stop();
  auto r = backend.next(*open.value, src.get_token());
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::Canceled));
}

FE_TEST_CASE(memory_fs_two_handles_iterate_independently) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a", 10);
  backend.addEntry(L"X:\\d", L"b", 20);

  auto open1 = backend.openEnumeration(L"X:\\d", std::stop_token{});
  auto open2 = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open1.ok());
  FE_ASSERT_TRUE(open2.ok());

  auto a1 = nextEntry(backend, *open1.value);
  FE_ASSERT_TRUE(a1.has_value());
  FE_ASSERT_EQ(a1->size, static_cast<uint64_t>(10));

  auto a2 = nextEntry(backend, *open2.value);
  FE_ASSERT_TRUE(a2.has_value());
  FE_ASSERT_EQ(a2->size, static_cast<uint64_t>(10));

  auto b2 = nextEntry(backend, *open2.value);
  FE_ASSERT_TRUE(b2.has_value());
  FE_ASSERT_EQ(b2->size, static_cast<uint64_t>(20));

  auto b1 = nextEntry(backend, *open1.value);
  FE_ASSERT_TRUE(b1.has_value());
  FE_ASSERT_EQ(b1->size, static_cast<uint64_t>(20));
}

FE_TEST_CASE(memory_fs_handle_snapshot_independent_of_post_open_mutation) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a", 1);
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());

  backend.addEntry(L"X:\\d", L"b", 2);

  auto first = nextEntry(backend, *open.value);
  FE_ASSERT_TRUE(first.has_value());
  FE_ASSERT_EQ(first->size, static_cast<uint64_t>(1));
  auto end = nextEntry(backend, *open.value);
  FE_ASSERT_FALSE(end.has_value());
}

// ---------------------------------------------------------------------------
// FaultInjectingMemoryFsBackend
// ---------------------------------------------------------------------------

FE_TEST_CASE(fault_injecting_memory_fs_set_open_error_propagates) {
  FaultInjectingMemoryFsBackend backend;
  backend.addDirectory(L"X:\\locked");
  backend.setOpenError(L"X:\\locked", EnumerationError::AccessDenied);
  auto r = backend.openEnumeration(L"X:\\locked", std::stop_token{});
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::AccessDenied));
}

FE_TEST_CASE(fault_injecting_memory_fs_set_next_error_aborts_mid_stream) {
  FaultInjectingMemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a", 1);
  backend.addEntry(L"X:\\d", L"b", 2);
  backend.addEntry(L"X:\\d", L"c", 3);
  backend.setNextError(L"X:\\d", 1, EnumerationError::AccessDenied);
  auto open = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto& h = *open.value;

  auto first = backend.next(h, std::stop_token{});
  FE_ASSERT_TRUE(first.ok());
  FE_ASSERT_TRUE(first.value.has_value());

  auto bad = backend.next(h, std::stop_token{});
  FE_ASSERT_FALSE(bad.ok());
  FE_ASSERT_EQ(static_cast<int>(bad.error),
               static_cast<int>(EnumerationError::AccessDenied));
}

FE_TEST_CASE(fault_injecting_memory_fs_post_open_open_error_only_affects_new_opens) {
  FaultInjectingMemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a", 1);
  auto open1 = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_TRUE(open1.ok());

  backend.setOpenError(L"X:\\d", EnumerationError::AccessDenied);

  auto first = nextEntry(backend, *open1.value);
  FE_ASSERT_TRUE(first.has_value());
  auto end = nextEntry(backend, *open1.value);
  FE_ASSERT_FALSE(end.has_value());

  auto open2 = backend.openEnumeration(L"X:\\d", std::stop_token{});
  FE_ASSERT_FALSE(open2.ok());
  FE_ASSERT_EQ(static_cast<int>(open2.error),
               static_cast<int>(EnumerationError::AccessDenied));
}
