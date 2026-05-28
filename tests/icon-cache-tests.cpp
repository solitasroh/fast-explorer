#include "test-harness.h"

#include <windows.h>
#include <commctrl.h>

#include "core/file-entry.h"
#include "explorer/icon-cache.h"

using fast_explorer::core::FileEntry;
using fast_explorer::ui::createPlaceholderImageList;
using fast_explorer::ui::IconCache;
using fast_explorer::ui::kPlaceholderFileIndex;
using fast_explorer::ui::kPlaceholderFolderIndex;
using fast_explorer::ui::placeholderIndexFor;
namespace flags = fast_explorer::core::file_entry_flags;

FE_TEST_CASE(IconCache_PlaceholderIndices_AreDistinct) {
  FE_ASSERT_NE(kPlaceholderFolderIndex, kPlaceholderFileIndex);
}

FE_TEST_CASE(IconCache_placeholderIndexFor_Directory_ReturnsFolderSlot) {
  FileEntry e{};
  e.flags = flags::kIsDirectory;
  FE_ASSERT_EQ(placeholderIndexFor(e), kPlaceholderFolderIndex);
}

FE_TEST_CASE(IconCache_placeholderIndexFor_File_ReturnsFileSlot) {
  FileEntry e{};
  e.flags = 0;
  FE_ASSERT_EQ(placeholderIndexFor(e), kPlaceholderFileIndex);
}

FE_TEST_CASE(IconCache_placeholderIndexFor_OtherFlags_DoNotConfuseDirectoryBit) {
  FileEntry e{};
  e.flags = flags::kIsHidden | flags::kIsSystem | flags::kIsReparse |
            flags::kIsCloudPlaceholder;
  // No directory bit set, even with every other flag asserted.
  FE_ASSERT_EQ(placeholderIndexFor(e), kPlaceholderFileIndex);

  e.flags |= flags::kIsDirectory;
  FE_ASSERT_EQ(placeholderIndexFor(e), kPlaceholderFolderIndex);
}

FE_TEST_CASE(IconCache_FreshConstruction_HasTwoPlaceholderSlots) {
  IconCache cache(96);
  if (!cache.ok()) {
    // Headless builds without a desktop session may not return
    // SHGetFileInfoW icons; skip the assertion rather than failing
    // a CI without a display.
    return;
  }
  FE_ASSERT_EQ(cache.iconCount(), 2);
  FE_ASSERT_TRUE(cache.byteSize() > 0);
}

FE_TEST_CASE(IconCache_ByteSize_ScalesWithIconCount) {
  IconCache cache(96);
  if (!cache.ok()) {
    return;
  }
  const std::size_t baseline = cache.byteSize();
  HICON extra = LoadIconW(nullptr, IDI_APPLICATION);
  if (extra != nullptr) {
    ImageList_AddIcon(cache.handle(), extra);
    // LoadIcon-returned shared icons must not be DestroyIcon'd, but
    // ImageList copies the bits so we are safe to leave it alone.
  }
  // Three slots if extra was added, two otherwise.
  if (extra != nullptr) {
    FE_ASSERT_EQ(cache.iconCount(), 3);
    FE_ASSERT_TRUE(cache.byteSize() > baseline);
  } else {
    FE_ASSERT_EQ(cache.iconCount(), 2);
  }
}

FE_TEST_CASE(IconCache_Swap_ReplacesHandleAndReturnsOld) {
  IconCache cache(96);
  if (!cache.ok()) {
    return;
  }
  HIMAGELIST original = cache.handle();
  HIMAGELIST fresh = createPlaceholderImageList(96);
  if (fresh == nullptr) {
    return;
  }
  HIMAGELIST returned = cache.swap(fresh);
  FE_ASSERT_TRUE(returned == original);
  FE_ASSERT_TRUE(cache.handle() == fresh);
  FE_ASSERT_EQ(cache.iconCount(), 2);
  ImageList_Destroy(returned);
}

FE_TEST_CASE(IconCache_Shrink_DropsExtraSlotsButKeepsPlaceholders) {
  IconCache cache(96);
  if (!cache.ok()) {
    return;
  }
  HICON extra = LoadIconW(nullptr, IDI_APPLICATION);
  if (extra != nullptr) {
    ImageList_AddIcon(cache.handle(), extra);
  }
  const int beforeCount = cache.iconCount();
  HIMAGELIST fresh = createPlaceholderImageList(96);
  FE_ASSERT_TRUE(fresh != nullptr);
  HIMAGELIST old = cache.swap(fresh);
  ImageList_Destroy(old);
  // After the swap, only the two placeholder slots remain.
  FE_ASSERT_EQ(cache.iconCount(), 2);
  if (extra != nullptr) {
    FE_ASSERT_TRUE(beforeCount >= 3);
  }
}

FE_TEST_CASE(CreatePlaceholderImageList_ReturnsTwoSlotList) {
  HIMAGELIST list = createPlaceholderImageList(96);
  if (list == nullptr) {
    // Same headless caveat as above.
    return;
  }
  FE_ASSERT_EQ(ImageList_GetImageCount(list), 2);
  ImageList_Destroy(list);
}
