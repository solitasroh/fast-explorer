#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstddef>
#include <memory>

#include "core/file-entry.h"

namespace fast_explorer::ui {

class ExtensionIconCache;
class IconCache;
class IconProvider;

// Owns the per-window icon caches (placeholder ImageList,
// extension LRU, background STA worker) and the kWmFeIconBatch /
// kWmFeLowMemory message handling that mutates them.
class IconCacheCoordinator {
 public:
  IconCacheCoordinator(HWND host, HWND listView, unsigned int dpi,
                       std::size_t paneIndex = 0);
  ~IconCacheCoordinator();

  IconCacheCoordinator(const IconCacheCoordinator&) = delete;
  IconCacheCoordinator& operator=(const IconCacheCoordinator&) = delete;
  IconCacheCoordinator(IconCacheCoordinator&&) = delete;
  IconCacheCoordinator& operator=(IconCacheCoordinator&&) = delete;

  // True when the underlying ImageList was created successfully.
  // Caller (MainWindow) gates the LVS_SMALL ImageList attach on
  // this.
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] HIMAGELIST imageListHandle() const noexcept;

  // Resolution for LVN_GETDISPINFO. Returns the slot index for
  // the row's icon: placeholder on first sight, cached slot on
  // subsequent hits, placeholder + background request on first
  // sight of a non-cached extension.
  int resolveIconIndex(const fast_explorer::core::FileEntry& entry);

  // Drain the icon-provider's resolved HICONs into the ImageList
  // and update the extension cache. Returns true when at least
  // one slot was inserted, signalling the caller to redraw.
  bool onIconBatch();

  // Replace the ImageList with a fresh placeholder-only list,
  // re-point the list-view, destroy the old handle, and clear
  // the extension cache. Returns true on success so the caller
  // can redraw visible rows.
  bool shrinkIconCache();

 private:
  HWND listView_;
  unsigned int dpi_;
  std::unique_ptr<IconCache> iconCache_;
  std::unique_ptr<ExtensionIconCache> extensionCache_;
  std::unique_ptr<IconProvider> iconProvider_;
};

}  // namespace fast_explorer::ui
