#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace fast_explorer::core {

// FileEntry — 40 B per-row record used by FileModelStore (Design §5.1).
//
// The layout is hand-tuned so 100k entries fit the per-pane budget of 4 MB
// structural + ~4.8 MB name arena. Do NOT change field order or sizes
// without updating Design §5.1 and the memory layout summary §5.2.2.
//
// modifiedTime100ns intentionally uses uint64_t rather than FILETIME so this
// public header does not pull <windows.h> (and its macro pollution) into
// every TU that includes it. The bit layout matches FILETIME exactly:
// reconstruct via `ULARGE_INTEGER{.QuadPart = e.modifiedTime100ns}` at the
// call site, or compose `(static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
// ft.dwLowDateTime` from raw FILETIME.

namespace file_entry_flags {

constexpr uint8_t kIsDirectory        = 1u << 0;
constexpr uint8_t kIsHidden           = 1u << 1;
constexpr uint8_t kIsSystem           = 1u << 2;
constexpr uint8_t kIsReparse          = 1u << 3;
constexpr uint8_t kIsCloudPlaceholder = 1u << 4;

}  // namespace file_entry_flags

namespace file_entry_state {

// The `states` byte packs two nibbles: low 4 bits = icon state enum,
// high 4 bits = metadata state enum (Design §5.1, §9.2 plans). Future
// enums must fit in 4 bits each (max 16 distinct states per nibble).
constexpr uint8_t kIconMask     = 0x0F;
constexpr uint8_t kMetadataMask = 0xF0;
constexpr uint8_t kMetadataShift = 4;

}  // namespace file_entry_state

// Sentinel stored in FileEntry::extensionOffset to mean "no extension"
// (i.e. the name has no '.').
inline constexpr uint16_t kNoExtension = UINT16_MAX;

struct FileEntry {
  const wchar_t* namePtr;        // 8 B — points into FileModelStore::nameArena
  uint64_t size;                 // 8 B — 0 for directories
  uint64_t modifiedTime100ns;    // 8 B — 100-ns intervals since 1601-01-01 UTC
  uint32_t attributes;           // 4 B — raw FILE_ATTRIBUTE_* mask
  uint16_t nameLength;           // 2 B — wide-char count
  uint16_t extensionOffset;      // 2 B — offset into name (kNoExtension if none)
  uint8_t  flags;                // 1 B — bit flags (file_entry_flags)
  uint8_t  states;               // 1 B — icon nibble + metadata nibble (file_entry_state)
  uint8_t  errorCode;            // 1 B — ErrorCode enum (0 = no error)
  uint8_t  reserved;             // 1 B — padding / future use
};

static_assert(sizeof(FileEntry) == 40,
              "FileEntry must be exactly 40 B for the memory budget");
static_assert(alignof(FileEntry) == 8,
              "FileEntry must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<FileEntry>,
              "FileEntry must be trivially copyable (memmove-safe batch ops)");
static_assert(std::is_standard_layout_v<FileEntry>,
              "FileEntry must be standard layout");
// is_trivial implies the default ctor is trivial too, so a FileEntry that
// the crash handler observes mid-write (raw bytes via reinterpret_cast) has
// well-defined "all bytes" semantics — no compiler-injected init code can
// race the read.
static_assert(std::is_trivial_v<FileEntry>,
              "FileEntry must be trivial (raw-bytes reinterpret in crash handler)");

// nameView / extensionView return non-owning wstring_views that alias the
// NameArena memory. They are valid as long as the owning FileModelStore is
// alive. nullptr namePtr + zero length yields an empty view (safe).

constexpr std::wstring_view nameView(const FileEntry& e) noexcept {
  return std::wstring_view(e.namePtr, e.nameLength);
}

// Returns the extension including its leading '.', or an empty view when
// no extension is present (offset == kNoExtension) or the offset is past
// the last character (offset >= nameLength). NOTE: an offset equal to
// `nameLength - 1` pointing at a trailing '.' IS honoured and yields the
// 1-char view L"." — producers that want Explorer-parity ("trailing dot
// is not an extension") must emit kNoExtension or offset == nameLength.
constexpr std::wstring_view extensionView(const FileEntry& e) noexcept {
  if (e.extensionOffset == kNoExtension ||
      e.extensionOffset >= e.nameLength) {
    return std::wstring_view();
  }
  return std::wstring_view(e.namePtr + e.extensionOffset,
                           e.nameLength - e.extensionOffset);
}

constexpr bool isDirectory(const FileEntry& e) noexcept {
  return (e.flags & file_entry_flags::kIsDirectory) != 0;
}
constexpr bool isHidden(const FileEntry& e) noexcept {
  return (e.flags & file_entry_flags::kIsHidden) != 0;
}
constexpr bool isSystem(const FileEntry& e) noexcept {
  return (e.flags & file_entry_flags::kIsSystem) != 0;
}
constexpr bool isReparse(const FileEntry& e) noexcept {
  return (e.flags & file_entry_flags::kIsReparse) != 0;
}
constexpr bool isCloudPlaceholder(const FileEntry& e) noexcept {
  return (e.flags & file_entry_flags::kIsCloudPlaceholder) != 0;
}

// Nibble accessors for the `states` byte. Return the 0..15 enum value
// stored in the corresponding half-byte. Used by the icon/metadata
// lookup machinery (§9.2 plans).
constexpr uint8_t iconState(const FileEntry& e) noexcept {
  return static_cast<uint8_t>(e.states & file_entry_state::kIconMask);
}
constexpr uint8_t metadataState(const FileEntry& e) noexcept {
  return static_cast<uint8_t>(
      (e.states & file_entry_state::kMetadataMask) >>
      file_entry_state::kMetadataShift);
}

}  // namespace fast_explorer::core
