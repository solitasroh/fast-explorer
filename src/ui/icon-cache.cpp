#include "ui/icon-cache.h"

#include <shellapi.h>

#include "winui_lite/chrome/dpi-scale.h"

namespace fast_explorer::ui {

namespace {

// SHGFI_USEFILEATTRIBUTES tells the shell to skip disk access entirely
// and resolve the icon from the supplied attribute set alone; the path
// argument is just a syntactic placeholder. SHGFI_SMALLICON pairs with
// the list-view's small-icon metric.
HICON loadPlaceholderIcon(DWORD fileAttributes) noexcept {
  SHFILEINFOW sfi{};
  // Sentinel must not look like a drive root (e.g. "C:\\") — the shell
  // pattern-matches drive roots to the system-drive volume icon (Windows
  // logo overlay) even under SHGFI_USEFILEATTRIBUTES. A bare relative
  // name yields the generic folder/file icon from the attribute mask.
  const wchar_t* sentinel = L"x";
  const UINT flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
  if (SHGetFileInfoW(sentinel, fileAttributes, &sfi, sizeof(sfi), flags) == 0) {
    return nullptr;
  }
  return sfi.hIcon;
}

constexpr int kInitialImageListSlots = 2;
constexpr int kImageListGrowSlots    = 8;

}  // namespace

HIMAGELIST createPlaceholderImageList(unsigned int dpi) noexcept {
  const int cx = scaleForDpi(GetSystemMetrics(SM_CXSMICON), dpi);
  const int cy = scaleForDpi(GetSystemMetrics(SM_CYSMICON), dpi);
  HIMAGELIST list = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK,
                                     kInitialImageListSlots,
                                     kImageListGrowSlots);
  if (list == nullptr) {
    return nullptr;
  }
  HICON folder = loadPlaceholderIcon(FILE_ATTRIBUTE_DIRECTORY);
  HICON file = loadPlaceholderIcon(FILE_ATTRIBUTE_NORMAL);
  const int folderIdx =
      (folder != nullptr) ? ImageList_AddIcon(list, folder) : -1;
  const int fileIdx =
      (file != nullptr) ? ImageList_AddIcon(list, file) : -1;
  if (folder != nullptr) {
    DestroyIcon(folder);
  }
  if (file != nullptr) {
    DestroyIcon(file);
  }
  if (folderIdx != kPlaceholderFolderIndex ||
      fileIdx != kPlaceholderFileIndex) {
    ImageList_Destroy(list);
    return nullptr;
  }
  return list;
}

IconCache::IconCache(unsigned int dpi) noexcept
    : imageList_(createPlaceholderImageList(dpi)) {}

IconCache::~IconCache() {
  if (imageList_ != nullptr) {
    ImageList_Destroy(imageList_);
    imageList_ = nullptr;
  }
}

int IconCache::iconCount() const noexcept {
  if (imageList_ == nullptr) {
    return 0;
  }
  return ImageList_GetImageCount(imageList_);
}

std::size_t IconCache::byteSize() const noexcept {
  if (imageList_ == nullptr) {
    return 0;
  }
  int cx = 0;
  int cy = 0;
  if (!ImageList_GetIconSize(imageList_, &cx, &cy) || cx <= 0 || cy <= 0) {
    return 0;
  }
  const int count = ImageList_GetImageCount(imageList_);
  if (count <= 0) {
    return 0;
  }
  // ILC_COLOR32 = 4 bytes per pixel; ILC_MASK adds 1 bit/pixel.
  const std::size_t colorBytes = static_cast<std::size_t>(cx) *
                                 static_cast<std::size_t>(cy) * 4;
  const std::size_t maskBytes = (static_cast<std::size_t>(cx) *
                                 static_cast<std::size_t>(cy) + 7) / 8;
  return static_cast<std::size_t>(count) * (colorBytes + maskBytes);
}

HIMAGELIST IconCache::swap(HIMAGELIST newList) noexcept {
  HIMAGELIST old = imageList_;
  imageList_ = newList;
  return old;
}

}  // namespace fast_explorer::ui
