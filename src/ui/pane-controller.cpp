#include "ui/pane-controller.h"

#include "core/path-utils.h"

namespace fast_explorer::ui {

PaneController::PaneController(HWND hostWindow)
    : hostWindow_(hostWindow), store_(L"") {}

bool PaneController::openFolder(const std::wstring& path) {
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;

  std::wstring internal;
  if (toInternal(path, internal) != PathConvertError::None) {
    return false;
  }
  currentPath_ = path;
  store_.reset(path);
  return true;
}

uint32_t PaneController::generation() const noexcept {
  return store_.generation();
}

}  // namespace fast_explorer::ui
