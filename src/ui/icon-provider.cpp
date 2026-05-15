#include "ui/icon-provider.h"

#include <objbase.h>
#include <shellapi.h>

#include <utility>

#include "ui/messages.h"

namespace fast_explorer::ui {

IconProvider::IconProvider(HWND host)
    : host_(host),
      worker_([this](std::stop_token tok) { workerMain(tok); }) {}

IconProvider::~IconProvider() {
  // Stop and join the worker explicitly so resultsReady_ is stable
  // before we touch it. The default jthread teardown would run
  // *after* this body, leaving any HICON the worker resolved between
  // its last drain and our destructor to leak — message-loop
  // shutdown can swallow the kWmFeIconBatch post.
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
  std::lock_guard lk(resultMutex_);
  for (auto& r : resultsReady_) {
    if (r.icon != nullptr) {
      DestroyIcon(r.icon);
      r.icon = nullptr;
    }
  }
  resultsReady_.clear();
}

void IconProvider::request(std::wstring extension) {
  {
    std::lock_guard lk(mutex_);
    pendingRequests_.push(std::move(extension));
  }
  cv_.notify_one();
}

std::vector<IconProvider::IconResult> IconProvider::drainResults() {
  std::vector<IconResult> out;
  {
    std::lock_guard lk(resultMutex_);
    out.swap(resultsReady_);
  }
  // Drain releases the coalesce gate so the next worker result can
  // post a fresh kWmFeIconBatch.
  postPending_.store(false, std::memory_order_release);
  return out;
}

void IconProvider::waitForProcessedForTest(
    std::size_t expected) const noexcept {
  std::size_t current = processed_.load(std::memory_order_acquire);
  while (current < expected) {
    processed_.wait(current, std::memory_order_acquire);
    current = processed_.load(std::memory_order_acquire);
  }
}

std::optional<std::wstring> IconProvider::dequeueOne(std::stop_token tok) {
  std::unique_lock lk(mutex_);
  cv_.wait(lk, tok, [this] { return !pendingRequests_.empty(); });
  if (tok.stop_requested()) {
    return std::nullopt;
  }
  std::wstring extension = std::move(pendingRequests_.front());
  pendingRequests_.pop();
  return extension;
}

HICON IconProvider::resolveIconForExtension(
    const std::wstring& extension) noexcept {
  if (extension.empty()) {
    return nullptr;
  }
  // Build a sentinel filename "x<extension>" so SHGetFileInfoW reads
  // the file-association for the extension without needing the file
  // to exist (SHGFI_USEFILEATTRIBUTES).
  std::wstring sentinel = L"x";
  sentinel += extension;
  SHFILEINFOW sfi{};
  const UINT flags =
      SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
  if (SHGetFileInfoW(sentinel.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi,
                     sizeof(sfi), flags) == 0) {
    return nullptr;
  }
  return sfi.hIcon;
}

void IconProvider::publishResult(const std::wstring& extension, HICON icon) {
  {
    std::lock_guard lk(resultMutex_);
    resultsReady_.emplace_back(extension, icon);
  }
  // Coalesce the host notification: a single kWmFeIconBatch wakes
  // the UI to drain however many results have accumulated since
  // its last drain, so we only post when no batch is in flight.
  bool expected = false;
  if (postPending_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
    if (host_ != nullptr) {
      PostMessageW(host_, kWmFeIconBatch, 0, 0);
    } else {
      // No host to deliver to — keep postPending_ clear so a future
      // drain in the destructor still drains the queue.
      postPending_.store(false, std::memory_order_release);
    }
  }
}

void IconProvider::processOne(const std::wstring& extension) {
  HICON icon = resolveIconForExtension(extension);
  if (icon != nullptr) {
    publishResult(extension, icon);
  }
  // Release-store + notify_all so waitForProcessedForTest sees this
  // request as completed. Ordering also publishes the resultsReady_
  // mutation above through the same release boundary.
  processed_.fetch_add(1, std::memory_order_release);
  processed_.notify_all();
}

void IconProvider::workerMain(std::stop_token tok) {
  // Shell icon resolution needs the STA apartment so SHGetFileInfoW
  // can hand back HICONs the UI thread can hand straight to
  // ImageList_AddIcon without further marshalling.
  const HRESULT coInitResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  while (!tok.stop_requested()) {
    auto req = dequeueOne(tok);
    if (!req) {
      break;
    }
    processOne(*req);
  }

  if (SUCCEEDED(coInitResult)) {
    CoUninitialize();
  }
}

}  // namespace fast_explorer::ui
