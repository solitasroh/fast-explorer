#include "test-harness.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "core/file-entry.h"

using fast_explorer::core::extensionView;
using fast_explorer::core::FileEntry;
using fast_explorer::core::iconState;
using fast_explorer::core::isCloudPlaceholder;
using fast_explorer::core::isDirectory;
using fast_explorer::core::isHidden;
using fast_explorer::core::isReparse;
using fast_explorer::core::isSystem;
using fast_explorer::core::kNoExtension;
using fast_explorer::core::metadataState;
using fast_explorer::core::nameView;
namespace flags = fast_explorer::core::file_entry_flags;
namespace state = fast_explorer::core::file_entry_state;

namespace {

// Helper that builds a FileEntry whose namePtr aliases the provided
// wstring_view. Length is taken from the view; extension offset/flags
// are caller-supplied.
FileEntry makeEntry(std::wstring_view name,
                    uint16_t extensionOffset,
                    uint8_t flags = 0,
                    uint64_t size = 0) {
  FileEntry e{};
  e.namePtr = name.data();
  e.nameLength = static_cast<uint16_t>(name.size());
  e.extensionOffset = extensionOffset;
  e.flags = flags;
  e.size = size;
  return e;
}

}  // namespace

FE_TEST_CASE(file_entry_sizeof_is_40_bytes) {
  // The compile-time static_assert in the header already guarantees this,
  // but a runtime check makes the constraint visible in the test report
  // alongside the other invariants.
  FE_ASSERT_EQ(sizeof(FileEntry), static_cast<size_t>(40));
}

FE_TEST_CASE(file_entry_alignof_is_8_bytes) {
  FE_ASSERT_EQ(alignof(FileEntry), static_cast<size_t>(8));
}

FE_TEST_CASE(file_entry_is_trivially_copyable_and_trivial) {
  FE_ASSERT_TRUE(std::is_trivially_copyable_v<FileEntry>);
  FE_ASSERT_TRUE(std::is_standard_layout_v<FileEntry>);
  FE_ASSERT_TRUE(std::is_trivial_v<FileEntry>);
}

FE_TEST_CASE(file_entry_default_constructed_is_zeroed) {
  // Value-initialization (`{}`) on a POD must zero every byte so consumers
  // can rely on errorCode == 0, flags == 0, etc. as their "uninitialized"
  // sentinel.
  FileEntry e{};
  uint8_t bytes[sizeof(FileEntry)];
  std::memcpy(bytes, &e, sizeof(FileEntry));
  for (size_t i = 0; i < sizeof(FileEntry); ++i) {
    FE_ASSERT_EQ(static_cast<int>(bytes[i]), 0);
  }
}

FE_TEST_CASE(file_entry_modified_time_field_is_8_bytes) {
  // Lock the modifiedTime field width — must remain 8 B to preserve the
  // 40 B layout when the field is later populated from FILETIME via
  // ULARGE_INTEGER reinterpretation.
  FileEntry e{};
  FE_ASSERT_EQ(sizeof(e.modifiedTime100ns), static_cast<size_t>(8));
}

FE_TEST_CASE(file_entry_name_view_aliases_name_ptr) {
  constexpr std::wstring_view kName = L"readme.txt";
  FileEntry e = makeEntry(kName, /*extensionOffset=*/6);
  const std::wstring_view nv = nameView(e);
  FE_ASSERT_EQ(nv.size(), kName.size());
  FE_ASSERT_TRUE(nv.data() == kName.data());
}

FE_TEST_CASE(file_entry_name_view_on_null_ptr_is_empty) {
  // A value-initialized FileEntry must produce an empty (not crashing)
  // name view. Important for "still being filled" rows that the virtual
  // list might briefly observe.
  FileEntry e{};
  const std::wstring_view nv = nameView(e);
  FE_ASSERT_EQ(nv.size(), static_cast<size_t>(0));
}

FE_TEST_CASE(file_entry_extension_view_returns_extension_with_dot) {
  constexpr std::wstring_view kName = L"readme.txt";
  FileEntry e = makeEntry(kName, /*extensionOffset=*/6);
  const std::wstring_view ext = extensionView(e);
  FE_ASSERT_EQ(ext.size(), static_cast<size_t>(4));
  FE_ASSERT_TRUE(ext == std::wstring_view(L".txt"));
}

FE_TEST_CASE(file_entry_extension_view_empty_when_no_extension_sentinel) {
  constexpr std::wstring_view kName = L"Makefile";
  FileEntry e = makeEntry(kName, /*extensionOffset=*/kNoExtension);
  const std::wstring_view ext = extensionView(e);
  FE_ASSERT_TRUE(ext.empty());
}

FE_TEST_CASE(file_entry_extension_view_empty_when_offset_past_end) {
  // Past-end guard: offset == nameLength means "the producer chose to
  // treat the trailing dot (if any) as not an extension". The implementation
  // returns an empty view via the `>= nameLength` branch.
  constexpr std::wstring_view kName = L"trailingDot.";  // 12 wide chars
  FileEntry e = makeEntry(kName, /*extensionOffset=*/12);
  const std::wstring_view ext = extensionView(e);
  FE_ASSERT_TRUE(ext.empty());
}

FE_TEST_CASE(file_entry_extension_view_returns_single_dot_for_trailing_dot) {
  // Pinned contract: when a producer hands us offset = nameLength - 1
  // pointing at a trailing '.', extensionView returns the 1-char view L".".
  // Producers that want Explorer-parity ("trailing dot is not an extension")
  // must instead emit extensionOffset = kNoExtension or offset == nameLength
  // (covered by the previous two tests).
  constexpr std::wstring_view kName = L"trailingDot.";  // 12 wide chars
  FileEntry e = makeEntry(kName, /*extensionOffset=*/11);
  const std::wstring_view ext = extensionView(e);
  FE_ASSERT_EQ(ext.size(), static_cast<size_t>(1));
  FE_ASSERT_TRUE(ext == std::wstring_view(L"."));
}

FE_TEST_CASE(file_entry_flag_bits_match_design) {
  // Lock the bit positions so a future refactor cannot quietly reassign
  // them and break on-disk debug dumps or downstream readers.
  FE_ASSERT_EQ(static_cast<int>(flags::kIsDirectory),        1 << 0);
  FE_ASSERT_EQ(static_cast<int>(flags::kIsHidden),           1 << 1);
  FE_ASSERT_EQ(static_cast<int>(flags::kIsSystem),           1 << 2);
  FE_ASSERT_EQ(static_cast<int>(flags::kIsReparse),          1 << 3);
  FE_ASSERT_EQ(static_cast<int>(flags::kIsCloudPlaceholder), 1 << 4);
}

FE_TEST_CASE(file_entry_flag_accessors_read_individual_bits) {
  FileEntry e{};
  e.flags = static_cast<uint8_t>(flags::kIsDirectory | flags::kIsHidden);
  FE_ASSERT_TRUE(isDirectory(e));
  FE_ASSERT_TRUE(isHidden(e));
  FE_ASSERT_FALSE(isSystem(e));
  FE_ASSERT_FALSE(isReparse(e));
  FE_ASSERT_FALSE(isCloudPlaceholder(e));
}

FE_TEST_CASE(file_entry_flag_accessors_zero_when_unset) {
  FileEntry e{};
  e.flags = 0;
  FE_ASSERT_FALSE(isDirectory(e));
  FE_ASSERT_FALSE(isHidden(e));
  FE_ASSERT_FALSE(isSystem(e));
  FE_ASSERT_FALSE(isReparse(e));
  FE_ASSERT_FALSE(isCloudPlaceholder(e));
}

FE_TEST_CASE(file_entry_no_extension_sentinel_is_uint16_max) {
  FE_ASSERT_EQ(kNoExtension, UINT16_MAX);
}

FE_TEST_CASE(file_entry_state_nibble_masks_match_design) {
  // Lock the nibble masks so future contributors cannot quietly re-pack
  // the states byte without updating Design §5.1.
  FE_ASSERT_EQ(static_cast<int>(state::kIconMask),     0x0F);
  FE_ASSERT_EQ(static_cast<int>(state::kMetadataMask), 0xF0);
  FE_ASSERT_EQ(static_cast<int>(state::kMetadataShift), 4);
}

FE_TEST_CASE(file_entry_icon_state_reads_low_nibble) {
  FileEntry e{};
  e.states = 0xA7;  // metadata = 0xA, icon = 0x7
  FE_ASSERT_EQ(static_cast<int>(iconState(e)),     0x7);
  FE_ASSERT_EQ(static_cast<int>(metadataState(e)), 0xA);
}

FE_TEST_CASE(file_entry_state_accessors_independent) {
  // Setting one nibble must not bleed into the other.
  FileEntry e{};
  e.states = 0x0F;
  FE_ASSERT_EQ(static_cast<int>(iconState(e)),     0xF);
  FE_ASSERT_EQ(static_cast<int>(metadataState(e)), 0x0);

  e.states = 0xF0;
  FE_ASSERT_EQ(static_cast<int>(iconState(e)),     0x0);
  FE_ASSERT_EQ(static_cast<int>(metadataState(e)), 0xF);
}

FE_TEST_CASE(file_entry_state_accessors_zero_when_unset) {
  FileEntry e{};
  e.states = 0;
  FE_ASSERT_EQ(static_cast<int>(iconState(e)),     0);
  FE_ASSERT_EQ(static_cast<int>(metadataState(e)), 0);
}
