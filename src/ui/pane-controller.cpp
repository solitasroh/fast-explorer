#include "ui/pane-controller.h"

#include <algorithm>
#include <stop_token>
#include <utility>

#include "core/directory-enumerator.h"
#include "core/file-sort.h"
#include "core/fs-backend.h"
#include "core/fs-watcher.h"
#include "core/path-utils.h"
#include "ui/messages.h"

namespace fast_explorer::ui {

namespace {

// Joins a jthread idempotently, stopping it first so a worker that
// is blocked in a long operation gets the signal before we wait on
// it. Used by navigateInternal and requestSort to retire the
// enumeration and sort workers before letting their captured state
// be replaced.
void stopAndJoin(std::jthread& thread) noexcept {
  if (thread.joinable()) {
    thread.request_stop();
    thread.join();
  }
}

}  // namespace

PaneController::PaneController(HWND hostWindow)
    : hostWindow_(hostWindow), store_(L"") {}

PaneController::~PaneController() = default;

void PaneController::joinForTest() noexcept {
  if (worker_.joinable()) {
    worker_.join();
  }
}

uint32_t PaneController::generation() const noexcept {
  return store_.generation();
}

namespace {

bool isPathValid(const std::wstring& path) {
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;
  std::wstring internal;
  return toInternal(path, internal) == PathConvertError::None;
}

std::wstring computeParent(const std::wstring& path) {
  if (path.empty()) {
    return std::wstring();
  }
  // Normalize away the \\?\ extended-length prefix so the separator
  // walk below sees a plain "X:\..." form.
  std::wstring p = fast_explorer::core::toDisplay(path);
  // Trim trailing separators except when we are already at the drive
  // root form "X:\".
  if (p.size() > 3) {
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) {
      p.pop_back();
    }
  }
  if (p.size() <= 3) {
    return std::wstring();
  }
  const size_t lastSep = p.find_last_of(L"\\/");
  if (lastSep == std::wstring::npos) {
    return std::wstring();
  }
  if (lastSep == 2 && p[1] == L':') {
    return p.substr(0, 3);
  }
  return p.substr(0, lastSep);
}

}  // namespace

bool PaneController::openFolder(const std::wstring& path) {
  if (!isPathValid(path)) {
    return false;
  }
  if (!currentPath_.empty()) {
    backStack_.push_back(currentPath_);
  }
  forwardStack_.clear();
  return navigateInternal(path);
}

bool PaneController::back() {
  if (backStack_.empty()) {
    return false;
  }
  const std::wstring target = backStack_.back();
  if (!isPathValid(target)) {
    return false;
  }
  backStack_.pop_back();
  if (!currentPath_.empty()) {
    forwardStack_.push_back(currentPath_);
  }
  return navigateInternal(target);
}

bool PaneController::forward() {
  if (forwardStack_.empty()) {
    return false;
  }
  const std::wstring target = forwardStack_.back();
  if (!isPathValid(target)) {
    return false;
  }
  forwardStack_.pop_back();
  if (!currentPath_.empty()) {
    backStack_.push_back(currentPath_);
  }
  return navigateInternal(target);
}

bool PaneController::up() {
  const std::wstring parent = computeParent(currentPath_);
  if (parent.empty()) {
    return false;
  }
  return openFolder(parent);
}

bool PaneController::refresh() {
  if (currentPath_.empty() || !isPathValid(currentPath_)) {
    return false;
  }
  return navigateInternal(currentPath_);
}

SortDispatch PaneController::requestSort(
    fast_explorer::core::SortKey key) {
  using fast_explorer::core::SortDirection;
  if (workerActive_.load(std::memory_order_acquire)) {
    return SortDispatch::Rejected;
  }
  // Any prior background sort must finish (or be cancelled) before a
  // new request can claim entries_ / pendingSortedOrder_.
  stopAndJoin(sortWorker_);
  const auto count = store_.publishedCount();
  if (count == 0) {
    return SortDispatch::Rejected;
  }
  if (sorted_ && sortSpec_.key == key) {
    sortSpec_.direction =
        (sortSpec_.direction == SortDirection::Ascending)
            ? SortDirection::Descending
            : SortDirection::Ascending;
  } else {
    sortSpec_.key = key;
    sortSpec_.direction = SortDirection::Ascending;
  }
  if (count < sortThresholdRows_) {
    store_.sort(sortSpec_);
    sorted_ = true;
    return SortDispatch::AppliedSync;
  }

  // Background path: the worker reads entries_ (stable while no
  // enumeration is running — workerActive_ acquire-load above plus the
  // navigateInternal join policy ensure the worker observes a frozen
  // entries_) and writes pendingSortedOrder_. The UI commits via
  // applyPendingSort() on kWmFeSortComplete; the generation gate
  // (pendingSortGen_ vs store_.generation()) rejects stale results.
  const auto spec = sortSpec_;
  const HWND host = hostWindow_;
  const auto gen = store_.generation();
  pendingSortGen_ = gen;
  sortWorker_ = std::jthread([this, host, spec, count, gen](
                                 std::stop_token tok) {
    std::vector<std::uint32_t> order;
    order.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      order[i] = i;
    }
    if (tok.stop_requested()) {
      return;
    }
    std::sort(order.begin(), order.end(),
              [this, spec](std::uint32_t a, std::uint32_t b) {
                return fast_explorer::core::lessEntries(
                    store_.entryAt(a), store_.entryAt(b), spec);
              });
    if (tok.stop_requested()) {
      return;
    }
    pendingSortedOrder_ = std::move(order);
    if (host) {
      PostMessageW(host, kWmFeSortComplete, static_cast<WPARAM>(gen), 0);
    }
  });
  return SortDispatch::Dispatched;
}

void PaneController::selectRaw(std::uint32_t rawIndex) {
  selectedRaws_.insert(rawIndex);
}

void PaneController::deselectRaw(std::uint32_t rawIndex) noexcept {
  selectedRaws_.erase(rawIndex);
}

void PaneController::clearSelection() noexcept {
  selectedRaws_.clear();
}

bool PaneController::isRawSelected(std::uint32_t rawIndex) const noexcept {
  return selectedRaws_.contains(rawIndex);
}

std::vector<int> PaneController::selectedRowsUnderCurrentOrder() const {
  std::vector<int> rows;
  if (selectedRaws_.empty()) {
    return rows;
  }
  const auto order = store_.visibleOrder();
  rows.reserve(selectedRaws_.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (selectedRaws_.contains(order[i])) {
      rows.push_back(static_cast<int>(i));
    }
  }
  return rows;
}

void PaneController::applyPendingSort(std::uint32_t gen) {
  if (workerActive_.load(std::memory_order_acquire)) {
    return;
  }
  if (sortWorker_.joinable()) {
    sortWorker_.join();
  }
  // Stale result: navigation happened between worker post and now.
  if (gen != pendingSortGen_ || gen != store_.generation()) {
    pendingSortedOrder_.clear();
    return;
  }
  if (pendingSortedOrder_.size() != store_.publishedCount()) {
    pendingSortedOrder_.clear();
    return;
  }
  store_.applySortedOrder(std::move(pendingSortedOrder_));
  pendingSortedOrder_.clear();
  sorted_ = true;
}

bool PaneController::navigateInternal(const std::wstring& path) {
  using fast_explorer::core::DirectoryEnumerator;
  using fast_explorer::core::EnumerationError;

  stopAndJoin(worker_);
  // The enumeration worker is about to reset() the store. Any pending
  // background sort references entries_ via entryAt(); join it first
  // so the sort sees a coherent snapshot or exits early.
  stopAndJoin(sortWorker_);
  pendingSortedOrder_.clear();
  selectedRaws_.clear();
  fsWatcher_.stop();

  currentPath_ = path;
  store_.reset(path);
  sorted_ = false;
  const uint32_t gen = store_.generation();
  const HWND host = hostWindow_;
  std::wstring localPath = path;

  // Order matters: workerActive_ must be true before the thread starts
  // appending, and the thread must clear it on exit (success, error,
  // or cancellation) so requestSort() can re-arm.
  workerActive_.store(true, std::memory_order_release);
  worker_ = std::jthread([this, host, gen,
                          localPath = std::move(localPath)](std::stop_token tok) {
    DirectoryEnumerator enumerator;
    auto onBatch = [this, host, gen](std::size_t /*start*/,
                                     std::size_t /*count*/) {
      // publish() before PostMessage so the UI thread that processes
      // kWmFeEnumBatch observes the matching entries on its acquire-
      // load of publishedCount().
      const auto count = static_cast<std::uint32_t>(store_.itemCount());
      store_.publish(count);
      if (host) {
        PostMessageW(host, kWmFeEnumBatch, static_cast<WPARAM>(gen),
                     static_cast<LPARAM>(count));
      }
    };
    const EnumerationError err =
        enumerator.run(backend_, localPath, tok, store_, onBatch);
    // Final publish() covers the case where the last batch was flushed
    // but onBatch was already invoked from inside enumerator.run; this
    // is a no-op if publishedCount already matches.
    store_.publish(static_cast<std::uint32_t>(store_.itemCount()));
    if (host) {
      const UINT msg = (err == EnumerationError::None ||
                        err == EnumerationError::Canceled)
                           ? kWmFeEnumComplete
                           : kWmFeEnumError;
      PostMessageW(host, msg, static_cast<WPARAM>(gen),
                   static_cast<LPARAM>(static_cast<int>(err)));
    }
    // Release-store after PostMessageW so any future worker-side
    // bookkeeping added between enumerator.run and the completion post
    // remains protected by the same release boundary requestSort()
    // acquires from.
    workerActive_.store(false, std::memory_order_release);
  });

  if (host != nullptr) {
    fsWatcher_.watch(path, host, kWmFeFsChange);
  }
  return true;
}

}  // namespace fast_explorer::ui
