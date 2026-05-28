#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstddef>

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

  // Current number of slots in the ImageList.
  [[nodiscard]] int iconCount() const noexcept;
  // Estimated bytes used by the current ImageList: count × per-slot
  // (color + mask) bytes at the cached metric. Diagnostic only.
  [[nodiscard]] std::size_t byteSize() const noexcept;

  // Replaces the owned ImageList with `newList`, returning the old
  // handle. Caller updates any consumer (e.g. ListView_SetImageList)
  // before destroying the returned handle. Takes ownership of
  // `newList`. After the call handle() returns newList.
  HIMAGELIST swap(HIMAGELIST newList) noexcept;

 private:
  HIMAGELIST imageList_ = nullptr;
};

// Builds a fresh ImageList sized for `dpi` with the two placeholder
// icons (folder + file) at kPlaceholderFolderIndex /
// kPlaceholderFileIndex. Returns nullptr on failure.
[[nodiscard]] HIMAGELIST createPlaceholderImageList(unsigned int dpi) noexcept;

}  // namespace fast_explorer::ui
