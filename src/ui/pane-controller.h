#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "core/file-model-store.h"

namespace fast_explorer::ui {

// Per-pane state holder. Owns the FileModelStore that backs one
// list-view and remembers the host HWND for cross-thread message
// routing.
class PaneController {
 public:
  explicit PaneController(HWND hostWindow);
  ~PaneController() = default;

  PaneController(const PaneController&) = delete;
  PaneController& operator=(const PaneController&) = delete;

  // Validates `path` and resets the underlying store on success.
  // Returns false on invalid path (empty / relative / UNC / etc.)
  // and leaves the controller unchanged.
  bool openFolder(const std::wstring& path);

  uint32_t generation() const noexcept;
  const std::wstring& currentPath() const noexcept { return currentPath_; }
  const fast_explorer::core::FileModelStore& store() const noexcept {
    return store_;
  }
  HWND hostWindow() const noexcept { return hostWindow_; }

 private:
  HWND hostWindow_;
  std::wstring currentPath_;
  fast_explorer::core::FileModelStore store_;
};

}  // namespace fast_explorer::ui
