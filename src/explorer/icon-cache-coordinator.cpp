#include "explorer/icon-cache-coordinator.h"

#include <shellapi.h>
#include <shlobj.h>

#include <string>

#include "core/location.h"
#include "explorer/extension-icon-cache.h"
#include "explorer/icon-cache.h"
#include "explorer/icon-provider.h"

namespace fast_explorer::ui {

namespace {

HICON loadShellNamespaceIcon(std::wstring_view location) noexcept {
  std::wstring text(location);
  PIDLIST_ABSOLUTE pidl = nullptr;
  SFGAOF attrs = 0;
  if (FAILED(SHParseDisplayName(text.c_str(), nullptr, &pidl, 0, &attrs)) ||
      pidl == nullptr) {
    return nullptr;
  }
  SHFILEINFOW sfi{};
  const UINT flags = SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON;
  const DWORD_PTR ok = SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0,
                                      &sfi, sizeof(sfi), flags);
  CoTaskMemFree(pidl);
  return ok != 0 ? sfi.hIcon : nullptr;
}

HICON loadPathIcon(std::wstring_view location,
                   const fast_explorer::core::FileEntry& entry) noexcept {
  std::wstring path(location);
  SHFILEINFOW sfi{};
  const UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
  if (SHGetFileInfoW(path.c_str(), entry.attributes, &sfi, sizeof(sfi),
                     flags) != 0) {
    return sfi.hIcon;
  }
  const DWORD fallbackAttrs =
      fast_explorer::core::isDirectory(entry) ? FILE_ATTRIBUTE_DIRECTORY
                                              : FILE_ATTRIBUTE_NORMAL;
  const UINT fallbackFlags =
      SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
  return SHGetFileInfoW(path.c_str(), fallbackAttrs, &sfi, sizeof(sfi),
                        fallbackFlags) != 0
             ? sfi.hIcon
             : nullptr;
}

HICON loadLocationIcon(std::wstring_view location,
                       const fast_explorer::core::FileEntry& entry) noexcept {
  if (location.empty()) {
    return nullptr;
  }
  if (fast_explorer::core::isShellLocation(location)) {
    if (HICON icon = loadShellNamespaceIcon(location)) {
      return icon;
    }
  }
  return loadPathIcon(location, entry);
}

}  // namespace

IconCacheCoordinator::IconCacheCoordinator(HWND host, HWND listView,
                                           unsigned int dpi,
                                           std::size_t paneIndex)
    : listView_(listView),
      dpi_(dpi),
      iconCache_(std::make_unique<IconCache>(dpi)),
      extensionCache_(std::make_unique<ExtensionIconCache>()),
      iconProvider_(std::make_unique<IconProvider>(host, paneIndex)) {}

IconCacheCoordinator::~IconCacheCoordinator() = default;

bool IconCacheCoordinator::ok() const noexcept {
  return iconCache_ && iconCache_->ok();
}

HIMAGELIST IconCacheCoordinator::imageListHandle() const noexcept {
  return iconCache_ ? iconCache_->handle() : nullptr;
}

int IconCacheCoordinator::resolveIconIndex(
    const fast_explorer::core::FileEntry& entry) {
  const int placeholder = placeholderIndexFor(entry);
  if (fast_explorer::core::isDirectory(entry) || !extensionCache_ ||
      !iconProvider_) {
    return placeholder;
  }
  const auto extView = fast_explorer::core::extensionView(entry);
  if (extView.empty()) {
    return placeholder;
  }
  const int cached = extensionCache_->lookup(extView);
  if (cached != ExtensionIconCache::kMissingIndex) {
    return cached;
  }
  // First sighting of this extension. Pin the row against the
  // placeholder slot so later repaints stop re-requesting, then
  // hand the real lookup off to the icon worker.
  extensionCache_->insert(extView, placeholder);
  iconProvider_->request(std::wstring(extView));
  return placeholder;
}

int IconCacheCoordinator::resolveIconIndex(
    const fast_explorer::core::FileEntry& entry,
    std::wstring_view iconLocation) {
  if (iconLocation.empty() || iconCache_ == nullptr || !iconCache_->ok()) {
    return resolveIconIndex(entry);
  }
  const int placeholder = placeholderIndexFor(entry);
  std::wstring key(iconLocation);
  const auto cached = locationIconCache_.find(key);
  if (cached != locationIconCache_.end()) {
    return cached->second;
  }

  HICON icon = loadLocationIcon(key, entry);
  if (icon == nullptr) {
    locationIconCache_.emplace(std::move(key), placeholder);
    return placeholder;
  }
  const int slot = ImageList_AddIcon(iconCache_->handle(), icon);
  DestroyIcon(icon);
  const int resolved = slot >= 0 ? slot : placeholder;
  locationIconCache_.emplace(std::move(key), resolved);
  return resolved;
}

bool IconCacheCoordinator::onIconBatch() {
  if (iconProvider_ == nullptr) {
    return false;
  }
  auto results = iconProvider_->drainResults();
  if (results.empty()) {
    return false;
  }
  if (iconCache_ == nullptr || !iconCache_->ok() ||
      extensionCache_ == nullptr) {
    // Defensive: drop the icons rather than leaking the HICON
    // handles if any dependency is missing.
    for (auto& r : results) {
      if (r.icon != nullptr) {
        DestroyIcon(r.icon);
      }
    }
    return false;
  }
  HIMAGELIST imageList = iconCache_->handle();
  bool any = false;
  for (auto& r : results) {
    if (r.icon == nullptr) {
      continue;
    }
    const int slot = ImageList_AddIcon(imageList, r.icon);
    DestroyIcon(r.icon);
    if (slot < 0) {
      continue;
    }
    extensionCache_->insert(r.extension, slot);
    any = true;
  }
  return any;
}

bool IconCacheCoordinator::shrinkIconCache() {
  if (!iconCache_ || !iconCache_->ok()) {
    return false;
  }
  HIMAGELIST fresh = createPlaceholderImageList(dpi_);
  if (fresh == nullptr) {
    return false;
  }
  HIMAGELIST old = iconCache_->swap(fresh);
  if (listView_ != nullptr) {
    ListView_SetImageList(listView_, iconCache_->handle(), LVSIL_SMALL);
  }
  if (old != nullptr) {
    ImageList_Destroy(old);
  }
  if (extensionCache_) {
    extensionCache_->clear();
  }
  locationIconCache_.clear();
  return true;
}

}  // namespace fast_explorer::ui
