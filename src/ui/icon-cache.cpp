#include "ui/icon-cache.h"

#include <shellapi.h>

#include "ui/dpi-scale.h"

namespace fast_explorer::ui {

namespace {

// SHGFI_USEFILEATTRIBUTES tells the shell to skip disk access entirely
// and resolve the icon from the supplied attribute set alone; the path
// argument is just a syntactic placeholder. SHGFI_SMALLICON pairs with
// the list-view's small-icon metric.
HICON loadPlaceholderIcon(DWORD fileAttributes) noexcept {
  SHFILEINFOW sfi{};
  // The sentinel path is not required to exist under
  // SHGFI_USEFILEATTRIBUTES; any well-formed Win32 path syntax works.
  const wchar_t* sentinel = L"C:\\";
  const UINT flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
  if (SHGetFileInfoW(sentinel, fileAttributes, &sfi, sizeof(sfi), flags) == 0) {
    return nullptr;
  }
  return sfi.hIcon;
}

// Slot count we will populate at construction (kPlaceholderFolderIndex
// + kPlaceholderFileIndex). Growth headroom covers system icons that
// later atoms will add through SHGFI_ICON without SHGFI_USEFILEATTRIBUTES.
constexpr int kInitialImageListSlots = 2;
constexpr int kImageListGrowSlots    = 8;

}  // namespace

IconCache::IconCache(unsigned int dpi) noexcept {
  const int cx = scaleForDpi(GetSystemMetrics(SM_CXSMICON), dpi);
  const int cy = scaleForDpi(GetSystemMetrics(SM_CYSMICON), dpi);
  imageList_ = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK,
                                kInitialImageListSlots,
                                kImageListGrowSlots);
  if (imageList_ == nullptr) {
    return;
  }

  HICON folder = loadPlaceholderIcon(FILE_ATTRIBUTE_DIRECTORY);
  HICON file = loadPlaceholderIcon(FILE_ATTRIBUTE_NORMAL);
  // ImageList_AddIcon returns the slot index it occupied (or -1). The
  // kPlaceholderFolder/FileIndex constants assume the two placeholders
  // land at slot 0 and slot 1 in that order; if either insert fails
  // the invariant is silently broken, so tear the cache down and let
  // the caller observe ok() == false.
  const int folderIdx =
      (folder != nullptr) ? ImageList_AddIcon(imageList_, folder) : -1;
  const int fileIdx =
      (file != nullptr) ? ImageList_AddIcon(imageList_, file) : -1;
  if (folder != nullptr) {
    DestroyIcon(folder);
  }
  if (file != nullptr) {
    DestroyIcon(file);
  }
  if (folderIdx != kPlaceholderFolderIndex ||
      fileIdx != kPlaceholderFileIndex) {
    ImageList_Destroy(imageList_);
    imageList_ = nullptr;
  }
}

IconCache::~IconCache() {
  if (imageList_ != nullptr) {
    ImageList_Destroy(imageList_);
    imageList_ = nullptr;
  }
}

}  // namespace fast_explorer::ui
