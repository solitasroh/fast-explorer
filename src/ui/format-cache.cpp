#include "ui/format-cache.h"

#include "ui/column-formatter.h"

namespace fast_explorer::ui {

namespace {

constexpr uint64_t kTicksPerMinute = 60ULL * 10'000'000ULL;

}  // namespace

FormatCache::FormatCache(std::size_t capacity)
    : folderText_(L"File folder") {
  const std::size_t cap = capacity == 0 ? 1 : capacity;
  sizeLru_.capacity = cap;
  typeLru_.capacity = cap;
  modifiedLru_.capacity = cap;
}

const std::wstring& FormatCache::sizeForEntry(
    const fast_explorer::core::FileEntry& entry) {
  if (fast_explorer::core::isDirectory(entry)) {
    return emptyText_;
  }
  return getOrFill(sizeLru_, entry.size,
                   [&]() { return formatSize(entry.size); });
}

const std::wstring& FormatCache::typeForEntry(
    const fast_explorer::core::FileEntry& entry) {
  if (fast_explorer::core::isDirectory(entry)) {
    return folderText_;
  }
  const auto ext = fast_explorer::core::extensionView(entry);
  return getOrFill(typeLru_, std::wstring(ext),
                   [&]() { return formatType(ext, false); });
}

const std::wstring& FormatCache::modifiedAt(uint64_t ft100ns) {
  if (ft100ns == 0) {
    return emptyText_;
  }
  const uint64_t minuteKey = ft100ns - (ft100ns % kTicksPerMinute);
  return getOrFill(modifiedLru_, minuteKey,
                   [&]() { return formatModified(ft100ns); });
}

}  // namespace fast_explorer::ui
