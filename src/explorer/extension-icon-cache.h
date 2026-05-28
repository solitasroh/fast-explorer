#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fast_explorer::ui {

// Bounded LRU mapping from a file extension (lower-cased, leading
// dot preserved) to its slot index in the per-window HIMAGELIST.
//
// This is the routing table the icon worker will fill in a later
// atom — given an extension it answers "do I already have a
// resolved system icon for this extension, and if so, at which
// image-list slot?" Lookups and inserts are both O(1) average and
// move the touched entry to the most-recently-used end. When
// capacity is exceeded the least-recently-used entry is evicted;
// the caller can recover the evicted image-list index through the
// optional out-parameter so the matching HIMAGELIST slot can be
// recycled.
//
// Threading: UI-thread only; the icon worker hands resolved indices
// to the UI thread through a message rather than mutating the cache
// directly.
class ExtensionIconCache {
 public:
  static constexpr std::size_t kDefaultCapacity = 256;
  static constexpr int kMissingIndex = -1;

  explicit ExtensionIconCache(
      std::size_t capacity = kDefaultCapacity) noexcept;

  // Case-insensitive lookup. Returns kMissingIndex when the
  // extension is not cached. A hit promotes the entry to the most-
  // recently-used position.
  int lookup(std::wstring_view extension);

  // Insert or refresh. Case-insensitive normalisation applies.
  // Returns the index that is now stored under the (normalised) key.
  // If the insert evicts an LRU entry, *evictedOut (when non-null)
  // receives that entry's image-list index so the caller can free
  // the corresponding HIMAGELIST slot.
  int insert(std::wstring_view extension, int imageListIndex,
             int* evictedOut = nullptr);

  std::size_t size() const noexcept { return entries_.size(); }
  std::size_t capacity() const noexcept { return capacity_; }
  bool empty() const noexcept { return entries_.empty(); }
  void clear() noexcept;

 private:
  struct Entry {
    std::wstring key;
    int value;
  };
  using EntryIt = std::list<Entry>::iterator;

  std::list<Entry> entries_;
  std::unordered_map<std::wstring, EntryIt> index_;
  std::size_t capacity_;
};

}  // namespace fast_explorer::ui
