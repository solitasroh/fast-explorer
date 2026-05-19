#pragma once

#include <cstddef>
#include <functional>
#include <stop_token>
#include <string>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "core/fs-backend.h"

namespace fast_explorer::core {

class RingLogger;

// Synchronous enumeration driver. The caller chooses whether to wrap
// run() in std::jthread; the class itself owns no thread or queue.
//
// run() drives backend.openEnumeration -> backend.next loop, appends
// each entry to the provided FileModelStore, and invokes onBatch when a
// batch boundary is reached. Cancellation is observed at every open/
// next boundary via std::stop_token. On ERROR_SHARING_VIOLATION at
// open time the call sleeps and retries up to a small bounded number
// of attempts. On ERROR_ACCESS_DENIED partway through enumeration
// (after at least one entry was stored) the call returns success so
// the caller can present a partial result.
class DirectoryEnumerator {
 public:
  struct Config {
    std::size_t batchSize = 256;
    int sharingViolationRetryMs = 100;
    int sharingViolationRetries = 1;
    // When false, entries with FILE_ATTRIBUTE_HIDDEN are dropped
    // before reaching the FileModelStore (matches Windows Explorer's
    // default behavior). Defaults to true so existing call sites keep
    // their previous behavior — only the v0.2 view-toggle wires this
    // off when the user unchecks "Show hidden items".
    bool includeHidden = true;
  };

  // Reports a freshly committed batch. The range is
  // [startIndex, startIndex + count) inside the FileModelStore the
  // caller passed to run(). count is always non-zero. The callback
  // must remain valid for the duration of the run() call.
  using BatchCallback =
      std::function<void(std::size_t startIndex, std::size_t count)>;

  DirectoryEnumerator() = default;
  explicit DirectoryEnumerator(Config config);

  void setLogger(RingLogger* logger) noexcept { logger_ = logger; }
  const Config& config() const noexcept { return config_; }

  EnumerationError run(IFsBackend& backend,
                       const std::wstring& path,
                       std::stop_token tok,
                       FileModelStore& store,
                       const BatchCallback& onBatch);

 private:
  Config config_{};
  RingLogger* logger_ = nullptr;
};

}  // namespace fast_explorer::core
