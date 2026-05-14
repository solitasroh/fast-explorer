#include "ui/pane-controller.h"

#include <stop_token>
#include <utility>

#include "core/directory-enumerator.h"
#include "core/fs-backend.h"
#include "core/path-utils.h"
#include "ui/messages.h"

namespace fast_explorer::ui {

PaneController::PaneController(HWND hostWindow)
    : hostWindow_(hostWindow), store_(L"") {}

PaneController::~PaneController() = default;

void PaneController::joinForTest() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

uint32_t PaneController::generation() const noexcept {
  return store_.generation();
}

bool PaneController::openFolder(const std::wstring& path) {
  using fast_explorer::core::DirectoryEnumerator;
  using fast_explorer::core::EnumerationError;
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;

  std::wstring internal;
  if (toInternal(path, internal) != PathConvertError::None) {
    return false;
  }

  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }

  currentPath_ = path;
  store_.reset(path);
  const uint32_t gen = store_.generation();
  const HWND host = hostWindow_;
  std::wstring localPath = path;

  worker_ = std::jthread([this, host, gen,
                          localPath = std::move(localPath)](std::stop_token tok) {
    DirectoryEnumerator enumerator;
    auto onBatch = [this, host, gen](std::size_t /*start*/,
                                     std::size_t /*count*/) {
      if (host) {
        PostMessageW(host, kWmFeEnumBatch, static_cast<WPARAM>(gen),
                     static_cast<LPARAM>(store_.itemCount()));
      }
    };
    const EnumerationError err =
        enumerator.run(backend_, localPath, tok, store_, onBatch);
    if (host) {
      const UINT msg = (err == EnumerationError::None ||
                        err == EnumerationError::Canceled)
                           ? kWmFeEnumComplete
                           : kWmFeEnumError;
      PostMessageW(host, msg, static_cast<WPARAM>(gen),
                   static_cast<LPARAM>(static_cast<int>(err)));
    }
  });

  return true;
}

}  // namespace fast_explorer::ui
