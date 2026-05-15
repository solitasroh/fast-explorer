#include "test-harness.h"

#include "core/file-entry.h"
#include "ui/icon-cache.h"

using fast_explorer::core::FileEntry;
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
