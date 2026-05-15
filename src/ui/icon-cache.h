#pragma once

#include <windows.h>
#include <commctrl.h>

#include "core/file-entry.h"

namespace fast_explorer::ui {

// Maps a FileEntry to an index into IconCache's HIMAGELIST. Pure
// function (no Win32 calls) so the placeholder routing policy stays
// unit-testable apart from the list-view's image-list machinery.
constexpr int kPlaceholderFolderIndex = 0;
constexpr int kPlaceholderFileIndex   = 1;

[[nodiscard]] constexpr int placeholderIndexFor(
    const fast_explorer::core::FileEntry& entry) noexcept {
  return fast_explorer::core::isDirectory(entry) ? kPlaceholderFolderIndex
                                                  : kPlaceholderFileIndex;
}

// Owns the per-window HIMAGELIST that the list-view's
// LVS_SHAREIMAGELISTS style uses. The cache pre-populates two
// placeholder icons (folder + file) drawn from SHGetFileInfoW with
// SHGFI_USEFILEATTRIBUTES, which is fast and synchronous because the
// shell does not have to touch disk for the placeholder lookup.
class IconCache {
 public:
  // Constructs an empty image list at the given DPI. The list is
  // sized for the small-icon metric — small-icon view is what
  // FastExplorer's list-view uses. Call ok() to confirm the
  // underlying ImageList_Create / placeholder population succeeded.
  explicit IconCache(unsigned int dpi) noexcept;
  ~IconCache();

  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;
  IconCache(IconCache&&) = delete;
  IconCache& operator=(IconCache&&) = delete;

  [[nodiscard]] bool ok() const noexcept { return imageList_ != nullptr; }
  [[nodiscard]] HIMAGELIST handle() const noexcept { return imageList_; }

 private:
  HIMAGELIST imageList_ = nullptr;
};

}  // namespace fast_explorer::ui
