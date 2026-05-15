#include "ui/icon-provider.h"

#include <objbase.h>

#include <utility>

namespace fast_explorer::ui {

IconProvider::IconProvider(HWND host)
    : host_(host),
      worker_([this](std::stop_token tok) { workerMain(tok); }) {}

// jthread's destructor request_stops the worker, and the worker's
// cv wait is stop_token-aware, so the worker exits without help
// from us. No custom teardown needed.
IconProvider::~IconProvider() = default;

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

void IconProvider::processOne(const std::wstring& extension) {
  // Dummy processing pass: the actual SHGetFileInfoW call arrives in
  // the follow-up atom. The release-store on processed_ pairs with
  // acquire-load callers (waitForProcessedForTest) and will, once
  // real result publishing is added in processOne, anchor the
  // happens-before for any HICON / cache entry the worker hands off.
  (void)extension;
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
