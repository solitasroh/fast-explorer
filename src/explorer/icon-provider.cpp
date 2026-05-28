#include "explorer/icon-provider.h"

#include <objbase.h>
#include <shellapi.h>

#include <utility>

#include "explorer/messages.h"

namespace fast_explorer::ui {

IconProvider::IconProvider(HWND host, std::size_t paneIndex)
    : results_(host, kWmFeIconBatch, paneIndex),
      worker_([this](std::stop_token tok) { workerMain(tok); }) {}

IconProvider::~IconProvider() {
  // Stop and join the worker explicitly so the channel is quiet
  // before we drain orphan HICONs. The default jthread teardown
  // would run *after* this body, leaving any HICON the worker
  // resolved between its last UI-side drain and our destructor to
  // leak — message-loop shutdown can swallow the kWmFeIconBatch
  // post.
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
  for (auto& r : results_.drainResults()) {
    if (r.icon != nullptr) {
      DestroyIcon(r.icon);
      r.icon = nullptr;
    }
  }
}

void IconProvider::request(std::wstring extension) {
  {
    std::lock_guard lk(mutex_);
    pendingRequests_.push(std::move(extension));
  }
  cv_.notify_one();
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

void IconProvider::processOne(const std::wstring& extension) {
  HICON icon = resolveIconForExtension(extension);
  if (icon != nullptr) {
    results_.publish(IconResult{extension, icon});
  }
  // Release-store + notify_all so waitForProcessedForTest sees this
  // request as completed. The channel's publish() already serializes
  // the result vector mutation under its own mutex.
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
