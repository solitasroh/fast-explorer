#include "ui/pane-controller.h"

#include <stop_token>
#include <utility>

#include <shellapi.h>

#include "core/directory-enumerator.h"
#include "core/fs-backend.h"
#include "core/fs-watcher.h"
#include "core/path-utils.h"
#include "ui/jthread-utils.h"
#include "ui/messages.h"

namespace fast_explorer::ui {

PaneController::PaneController(HWND hostWindow, std::size_t paneIndex)
    : hostWindow_(hostWindow),
      paneIndex_(paneIndex),
      store_(L""),
      sortCoord_(store_, hostWindow, paneIndex),
      shellWorker_(hostWindow, paneIndex) {}

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
  // walk below sees a plain "X:\..." or "\\server\share\..." form.
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
  // UNC root detection: "\\server\share" has its share-separator at
  // the position of the third backslash counted from the start. If
  // there's no fourth separator, we're at the UNC root and going
  // "up" would land on "\\server" which the OS can't enumerate.
  if (p.size() >= 2 && (p[0] == L'\\' || p[0] == L'/') &&
      (p[1] == L'\\' || p[1] == L'/')) {
    const size_t shareSep = p.find_first_of(L"\\/", 2);
    if (shareSep == std::wstring::npos) {
      // "\\server" only — not a real folder.
      return std::wstring();
    }
    const size_t fourth = p.find_first_of(L"\\/", shareSep + 1);
    if (fourth == std::wstring::npos) {
      // "\\server\share" with no trailing folder — this is the UNC root.
      return std::wstring();
    }
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

bool PaneController::canGoUp() const {
  return !computeParent(currentPath_).empty();
}

bool PaneController::refresh() {
  if (currentPath_.empty() || !isPathValid(currentPath_)) {
    return false;
  }
  return navigateInternal(currentPath_);
}

namespace {

bool shellOpenPath(const std::wstring& path, const std::wstring& cwd,
                   HWND host) noexcept {
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  // No SEE_MASK_FLAG_NO_UI: that flag suppresses not just the shell's
  // own error dialog but also the UAC consent prompt that AppInfo
  // raises when activating a manifest-elevated exe. Double-clicking
  // an admin-required installer was silently no-op'ing as a result.
  // SEE_MASK_NOASYNC keeps the call synchronous so AppInfo can wire
  // the consent.exe handshake back to our process before lpFile goes
  // out of scope when we return.
  info.fMask = SEE_MASK_NOASYNC;
  info.hwnd = host;
  // lpVerb = nullptr lets the shell pick the registered default verb
  // for the file class. For .exe that resolves to "open", same as
  // before; for .msi / .lnk / .url it matches Win Explorer's double-
  // click handling.
  info.lpVerb = nullptr;
  info.lpFile = path.c_str();
  // Parent folder as CWD — matches Win Explorer parity and is what
  // many installers (NSIS, InstallShield) implicitly assume when
  // reading bundled resources via relative paths.
  info.lpDirectory = cwd.empty() ? nullptr : cwd.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

}  // namespace

bool PaneController::resolveRowSourcePath(std::uint32_t row,
                                          std::wstring& out) const {
  if (row >= store_.publishedCount()) {
    return false;
  }
  const auto& entry = store_.visibleAt(row);
  out = fast_explorer::core::joinPath(
      currentPath_, fast_explorer::core::nameView(entry));
  return true;
}

bool PaneController::openItem(std::uint32_t row) {
  std::wstring fullPath;
  if (!resolveRowSourcePath(row, fullPath)) {
    return false;
  }
  // resolveRowSourcePath already validated row < publishedCount(),
  // so this second visibleAt read is safe.
  if (fast_explorer::core::isDirectory(store_.visibleAt(row))) {
    return openFolder(fullPath);
  }
  return shellOpenPath(fullPath, currentPath_, hostWindow_);
}

bool PaneController::deleteItem(std::uint32_t row) {
  ShellCommand cmd;
  if (!resolveRowSourcePath(row, cmd.sourcePath)) {
    return false;
  }
  cmd.kind = ShellCommandKind::Delete;
  shellWorker_.request(std::move(cmd));
  return true;
}

bool PaneController::renameItem(std::uint32_t row,
                                const std::wstring& newName) {
  if (newName.empty()) {
    return false;
  }
  ShellCommand cmd;
  if (!resolveRowSourcePath(row, cmd.sourcePath)) {
    return false;
  }
  cmd.kind = ShellCommandKind::Rename;
  cmd.newName = newName;
  shellWorker_.request(std::move(cmd));
  return true;
}

bool PaneController::createSubfolder(const std::wstring& name) {
  if (name.empty() || currentPath_.empty()) {
    return false;
  }
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::CreateFolder;
  cmd.sourcePath = currentPath_;
  cmd.newName = name;
  shellWorker_.request(std::move(cmd));
  return true;
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

SortDispatch PaneController::setGroupBy(
    fast_explorer::core::GroupKey key) {
  groupBy_ = key;
  // Capture wall-clock once; the same `now` is used by both the sort
  // comparator and any subsequent enumerateGroups call.
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER ui{};
  ui.LowPart  = ft.dwLowDateTime;
  ui.HighPart = ft.dwHighDateTime;
  groupNow_ = ui.QuadPart;
  return requestSort(sortCoord_.currentSortSpec().key);
}

bool PaneController::navigateInternal(const std::wstring& path) {
  using fast_explorer::core::DirectoryEnumerator;
  using fast_explorer::core::EnumerationError;

  stopAndJoin(worker_);
  // The enumeration worker is about to reset() the store. Any pending
  // background sort references entries_ via entryAt(); cancel it
  // first so the sort sees a coherent snapshot or exits early.
  sortCoord_.cancel();
  selectedRaws_.clear();
  // A new folder invalidates the prior filter — store.reset() will
  // wipe the subset bytes anyway, but clearing the cached pattern
  // here keeps hasActiveFilter() / currentFilter() honest from the
  // UI's point of view between the navigate start and the enum end.
  currentFilter_ = FilterPattern{};
  fsWatcher_.stop();

  currentPath_ = path;
  store_.reset(path);
  const uint32_t gen = store_.generation();
  const HWND host = hostWindow_;
  const std::size_t paneIdx = paneIndex_;
  std::wstring localPath = path;

  // Order matters: workerActive_ must be true before the thread starts
  // appending, and the thread must clear it on exit (success, error,
  // or cancellation) so requestSort() can re-arm.
  workerActive_.store(true, std::memory_order_release);
  // Snapshot the toggle here so the worker uses the value at navigate
  // start, not whatever the user happens to flip mid-enum.
  const bool includeHiddenSnapshot = includeHidden_;
  worker_ = std::jthread([this, host, gen, paneIdx, includeHiddenSnapshot,
                          localPath = std::move(localPath)](std::stop_token tok) {
    DirectoryEnumerator::Config cfg{};
    cfg.includeHidden = includeHiddenSnapshot;
    DirectoryEnumerator enumerator(cfg);
    auto onBatch = [this, host, gen, paneIdx](std::size_t /*start*/,
                                              std::size_t /*count*/) {
      // publish() before PostMessage so the UI thread that processes
      // kWmFeEnumBatch observes the matching entries on its acquire-
      // load of publishedCount().
      const auto count = static_cast<std::uint32_t>(store_.itemCount());
      store_.publish(count);
      if (host) {
        PostMessageW(host, kWmFeEnumBatch,
                     makePaneWParam(paneIdx, gen),
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
      PostMessageW(host, msg, makePaneWParam(paneIdx, gen),
                   static_cast<LPARAM>(static_cast<int>(err)));
    }
    // Release-store after PostMessageW so any future worker-side
    // bookkeeping added between enumerator.run and the completion post
    // remains protected by the same release boundary requestSort()
    // acquires from.
    workerActive_.store(false, std::memory_order_release);
  });

  if (host != nullptr) {
    fsWatcher_.watch(path, host, kWmFeFsChange, paneIndex_);
  }
  return true;
}

}  // namespace fast_explorer::ui
