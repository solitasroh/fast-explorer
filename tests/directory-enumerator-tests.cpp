#include "test-harness.h"

#include <atomic>
#include <chrono>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "core/directory-enumerator.h"
#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"
#include "core/memory-fs-backend.h"

using fast_explorer::core::BackendKind;
using fast_explorer::core::DirectoryEnumerator;
using fast_explorer::core::EnumerationError;
using fast_explorer::core::EnumerationHandle;
using fast_explorer::core::FaultInjectingMemoryFsBackend;
using fast_explorer::core::FileEntry;
using fast_explorer::core::FileModelStore;
using fast_explorer::core::IFsBackend;
using fast_explorer::core::MemoryFsBackend;
using fast_explorer::core::Result;

namespace {

struct CapturedBatch {
  std::size_t startIndex;
  std::size_t count;
};

DirectoryEnumerator::BatchCallback makeCollector(
    std::vector<CapturedBatch>& sink) {
  return [&sink](std::size_t startIndex, std::size_t count) {
    sink.push_back({startIndex, count});
  };
}

// Backend that calls stop_source::request_stop() from inside nextHook
// when the configured position is reached. Used to test mid-stream
// cancellation deterministically without needing a separate thread.
class CancelOnNextBackend : public MemoryFsBackend {
 public:
  CancelOnNextBackend(std::stop_source& src, std::size_t cancelAt)
      : src_(src), cancelAt_(cancelAt) {}

 protected:
  EnumerationError nextHook(const std::wstring& /*path*/,
                            std::size_t position) override {
    if (position == cancelAt_) {
      src_.request_stop();
    }
    return EnumerationError::None;
  }

 private:
  std::stop_source& src_;
  std::size_t cancelAt_;
};

// Backend that fails openEnumeration with SharingViolation for the
// first N attempts on `targetPath`, then succeeds. Used to exercise the
// DirectoryEnumerator retry path deterministically.
class FlakyOpenBackend : public MemoryFsBackend {
 public:
  FlakyOpenBackend(std::wstring targetPath, int failuresBeforeSuccess)
      : targetPath_(std::move(targetPath)),
        remainingFailures_(failuresBeforeSuccess) {}

  int observedAttempts() const noexcept { return observedAttempts_; }

 protected:
  EnumerationError openHook(const std::wstring& path) override {
    if (path == targetPath_) {
      ++observedAttempts_;
      if (remainingFailures_ > 0) {
        --remainingFailures_;
        return EnumerationError::SharingViolation;
      }
    }
    return EnumerationError::None;
  }

 private:
  std::wstring targetPath_;
  int remainingFailures_;
  int observedAttempts_ = 0;
};

}  // namespace

FE_TEST_CASE(directory_enumerator_empty_directory_returns_none_no_batch) {
  MemoryFsBackend backend;
  backend.addDirectory(L"X:\\empty");
  FileModelStore store(L"X:\\empty");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator enumerator;

  const auto result = enumerator.run(backend, L"X:\\empty",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
  FE_ASSERT_TRUE(batches.empty());
}

FE_TEST_CASE(directory_enumerator_single_batch_posted_when_under_batch_size) {
  MemoryFsBackend backend;
  for (int i = 0; i < 10; ++i) {
    backend.addEntry(L"X:\\d", L"item", static_cast<uint64_t>(i));
  }
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator enumerator;

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(10));
  FE_ASSERT_EQ(batches.size(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(batches[0].startIndex, static_cast<std::size_t>(0));
  FE_ASSERT_EQ(batches[0].count, static_cast<std::size_t>(10));
}

FE_TEST_CASE(directory_enumerator_splits_at_batch_size_boundary) {
  MemoryFsBackend backend;
  for (int i = 0; i < 700; ++i) {
    backend.addEntry(L"X:\\d", L"x", static_cast<uint64_t>(i));
  }
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator::Config cfg;
  cfg.batchSize = 256;
  DirectoryEnumerator enumerator(cfg);

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(700));
  FE_ASSERT_EQ(batches.size(), static_cast<std::size_t>(3));
  FE_ASSERT_EQ(batches[0].count, static_cast<std::size_t>(256));
  FE_ASSERT_EQ(batches[1].count, static_cast<std::size_t>(256));
  FE_ASSERT_EQ(batches[2].count, static_cast<std::size_t>(188));
  FE_ASSERT_EQ(batches[0].startIndex, static_cast<std::size_t>(0));
  FE_ASSERT_EQ(batches[1].startIndex, static_cast<std::size_t>(256));
  FE_ASSERT_EQ(batches[2].startIndex, static_cast<std::size_t>(512));
  FE_ASSERT_EQ(store.entryAt(batches[0].startIndex).size,
               static_cast<uint64_t>(0));
  FE_ASSERT_EQ(store.entryAt(batches[1].startIndex).size,
               static_cast<uint64_t>(256));
  FE_ASSERT_EQ(store.entryAt(batches[2].startIndex).size,
               static_cast<uint64_t>(512));
}

FE_TEST_CASE(directory_enumerator_path_not_found_returns_error) {
  MemoryFsBackend backend;
  FileModelStore store(L"X:\\missing");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator enumerator;

  const auto result = enumerator.run(backend, L"X:\\missing",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::PathNotFound));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
  FE_ASSERT_TRUE(batches.empty());
}

FE_TEST_CASE(directory_enumerator_pre_stopped_token_returns_canceled) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a");
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator enumerator;
  std::stop_source src;
  src.request_stop();

  const auto result = enumerator.run(backend, L"X:\\d",
                                     src.get_token(), store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::Canceled));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(directory_enumerator_access_denied_with_partial_result_returns_none) {
  FaultInjectingMemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a", 1);
  backend.addEntry(L"X:\\d", L"b", 2);
  backend.addEntry(L"X:\\d", L"c", 3);
  backend.addEntry(L"X:\\d", L"d", 4);
  backend.setNextError(L"X:\\d", 2, EnumerationError::AccessDenied);

  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator::Config cfg;
  cfg.batchSize = 256;
  DirectoryEnumerator enumerator(cfg);

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(2));
}

FE_TEST_CASE(directory_enumerator_access_denied_at_first_entry_returns_error) {
  FaultInjectingMemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a", 1);
  backend.setNextError(L"X:\\d", 0, EnumerationError::AccessDenied);
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator enumerator;

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::AccessDenied));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(directory_enumerator_sharing_violation_retries_then_succeeds) {
  FlakyOpenBackend backend(L"X:\\d", /*failuresBeforeSuccess=*/1);
  backend.addEntry(L"X:\\d", L"a", 7);
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator::Config cfg;
  cfg.batchSize = 256;
  cfg.sharingViolationRetryMs = 0;  // skip the sleep in tests
  cfg.sharingViolationRetries = 1;
  DirectoryEnumerator enumerator(cfg);

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(backend.observedAttempts(), 2);
}

FE_TEST_CASE(directory_enumerator_sharing_violation_exhausts_retries) {
  FlakyOpenBackend backend(L"X:\\d", /*failuresBeforeSuccess=*/5);
  backend.addEntry(L"X:\\d", L"a", 7);
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator::Config cfg;
  cfg.sharingViolationRetryMs = 0;
  cfg.sharingViolationRetries = 1;
  DirectoryEnumerator enumerator(cfg);

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::SharingViolation));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(backend.observedAttempts(), 2);
}

FE_TEST_CASE(directory_enumerator_runs_without_batch_callback) {
  MemoryFsBackend backend;
  backend.addEntry(L"X:\\d", L"a");
  backend.addEntry(L"X:\\d", L"b");
  FileModelStore store(L"X:\\d");
  DirectoryEnumerator enumerator;

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     /*onBatch=*/{});
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_EQ(store.itemCount(), static_cast<std::size_t>(2));
}

FE_TEST_CASE(directory_enumerator_mid_stream_cancel_flushes_partial_batch) {
  std::stop_source src;
  CancelOnNextBackend backend(src, /*cancelAt=*/100);
  for (int i = 0; i < 500; ++i) {
    backend.addEntry(L"X:\\d", L"item", static_cast<uint64_t>(i));
  }
  FileModelStore store(L"X:\\d");
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator::Config cfg;
  cfg.batchSize = 256;
  DirectoryEnumerator enumerator(cfg);

  const auto result = enumerator.run(backend, L"X:\\d", src.get_token(),
                                     store, makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::Canceled));
  FE_ASSERT_TRUE(store.itemCount() > 0);
  FE_ASSERT_TRUE(store.itemCount() < 500);
  FE_ASSERT_FALSE(batches.empty());
  FE_ASSERT_EQ(store.itemCount(), batches.back().startIndex + batches.back().count);
}

FE_TEST_CASE(directory_enumerator_partial_batch_when_arena_exhausts) {
  MemoryFsBackend backend;
  const std::wstring filler(50, L'X');
  for (int i = 0; i < 2000; ++i) {
    backend.addEntry(L"X:\\d", filler);
  }
  FileModelStore store(L"X:\\d",
                       fast_explorer::core::NameArena::kCommitChunkBytes);
  std::vector<CapturedBatch> batches;
  DirectoryEnumerator::Config cfg;
  cfg.batchSize = 256;
  DirectoryEnumerator enumerator(cfg);

  const auto result = enumerator.run(backend, L"X:\\d",
                                     std::stop_token{}, store,
                                     makeCollector(batches));
  FE_ASSERT_EQ(static_cast<int>(result),
               static_cast<int>(EnumerationError::None));
  FE_ASSERT_TRUE(store.itemCount() > 0);
  FE_ASSERT_TRUE(store.itemCount() < 2000);
  FE_ASSERT_FALSE(batches.empty());
}
