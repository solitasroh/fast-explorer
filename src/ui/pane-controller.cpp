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
  std::wstring p = path;
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

bool PaneController::navigateInternal(const std::wstring& path) {
  using fast_explorer::core::DirectoryEnumerator;
  using fast_explorer::core::EnumerationError;

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
