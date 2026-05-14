#include "core/directory-enumerator.h"

#include <cassert>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "core/ring-logger.h"

namespace fast_explorer::core {

DirectoryEnumerator::DirectoryEnumerator(Config config)
    : config_(config) {}

namespace {

void flushBatch(FileModelStore& store,
                std::vector<FileEntry>& batch,
                const DirectoryEnumerator::BatchCallback& onBatch) {
  if (batch.empty()) {
    return;
  }
  const std::size_t before = store.itemCount();
  const std::size_t stored = store.appendBatch(batch);
  batch.clear();
  if (onBatch && stored > 0) {
    onBatch(before, stored);
  }
}

// Interruptible sleep — polls the stop_token every kPollingQuantumMs
// so a shutdown signal cannot get stuck behind a long sharing-violation
// back-off.
constexpr int kPollingQuantumMs = 10;

void interruptibleSleep(int durationMs, std::stop_token tok) {
  if (durationMs <= 0) {
    return;
  }
  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(durationMs);
  while (std::chrono::steady_clock::now() < end) {
    if (tok.stop_requested()) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollingQuantumMs));
  }
}

}  // namespace

EnumerationError DirectoryEnumerator::run(IFsBackend& backend,
                                          const std::wstring& path,
                                          std::stop_token tok,
                                          FileModelStore& store,
                                          const BatchCallback& onBatch) {
  std::unique_ptr<EnumerationHandle> handle;

  const int maxRetries = config_.sharingViolationRetries < 0
                              ? 0
                              : config_.sharingViolationRetries;
  for (int attempt = 0; attempt <= maxRetries; ++attempt) {
    if (tok.stop_requested()) {
      return EnumerationError::Canceled;
    }
    auto open = backend.openEnumeration(path, tok);
    if (open.ok()) {
      handle = std::move(open.value);
      break;
    }
    if (open.error == EnumerationError::SharingViolation &&
        attempt < maxRetries) {
      if (logger_ != nullptr) {
        logger_->warn(L"DirectoryEnumerator: SharingViolation, retrying");
      }
      interruptibleSleep(config_.sharingViolationRetryMs, tok);
      continue;
    }
    if (logger_ != nullptr) {
      logger_->error(L"DirectoryEnumerator: open failed (err=%d)",
                     static_cast<int>(open.error));
    }
    return open.error;
  }
  assert(handle != nullptr);
  if (handle == nullptr) {
    return EnumerationError::Internal;
  }

  std::vector<FileEntry> batch;
  batch.reserve(config_.batchSize);

  while (true) {
    if (tok.stop_requested()) {
      flushBatch(store, batch, onBatch);
      return EnumerationError::Canceled;
    }
    auto r = backend.next(*handle, tok);
    if (!r.ok()) {
      flushBatch(store, batch, onBatch);
      // Permissions failure midway through enumeration with at least
      // one already-stored entry: surface as a successful partial
      // result so the UI can render what we have.
      if (r.error == EnumerationError::AccessDenied &&
          store.itemCount() > 0) {
        if (logger_ != nullptr) {
          logger_->warn(
              L"DirectoryEnumerator: AccessDenied mid-stream, partial result");
        }
        return EnumerationError::None;
      }
      if (logger_ != nullptr) {
        logger_->error(L"DirectoryEnumerator: next failed (err=%d)",
                       static_cast<int>(r.error));
      }
      return r.error;
    }
    if (!r.value.has_value()) {
      flushBatch(store, batch, onBatch);
      return EnumerationError::None;
    }
    batch.push_back(*r.value);
    if (batch.size() >= config_.batchSize) {
      flushBatch(store, batch, onBatch);
    }
  }
}

}  // namespace fast_explorer::core
