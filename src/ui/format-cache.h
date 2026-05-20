#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>

#include "core/file-entry.h"

namespace fast_explorer::ui {

// Bounded LRU cache of column-text strings, populated lazily on the
// LVN_GETDISPINFOW hot path.
//
// Threading: UI-thread only.  The internal std::list / std::unordered_map
// pairs are not synchronized.  A worker thread must not call any
// member.
class FormatCache {
 public:
  static constexpr std::size_t kDefaultCapacity = 256;

  explicit FormatCache(std::size_t capacity = kDefaultCapacity);

  // Returns column text for the entry.  References remain valid until
  // the matching cache slot is evicted (capacity exceeded with a
  // different key); callers must copy the contents before any further
  // FormatCache mutation.

  const std::wstring& sizeForEntry(
      const fast_explorer::core::FileEntry& entry);
  const std::wstring& typeForEntry(
      const fast_explorer::core::FileEntry& entry);
  const std::wstring& modifiedAt(uint64_t ft100ns);

  std::size_t capacity() const noexcept { return sizeLru_.capacity; }

  // Test-only observers — production code shouldn't depend on these,
  // but the LRU eviction test needs a deterministic way to verify
  // that the cache honours its capacity bound (pointer-address
  // comparison was allocator-dependent and flaked on CI).
  std::size_t sizeCacheCount() const noexcept { return sizeLru_.size(); }

 private:
  template <typename Key>
  struct Lru {
    using Pair = std::pair<Key, std::wstring>;
    std::list<Pair> entries;
    std::unordered_map<Key, typename std::list<Pair>::iterator> index;
    std::size_t capacity = kDefaultCapacity;

    const std::wstring* get(const Key& k) {
      auto it = index.find(k);
      if (it == index.end()) {
        return nullptr;
      }
      entries.splice(entries.begin(), entries, it->second);
      return &it->second->second;
    }

    const std::wstring& put(Key k, std::wstring value) {
      entries.emplace_front(std::move(k), std::move(value));
      index[entries.front().first] = entries.begin();
      while (entries.size() > capacity) {
        index.erase(entries.back().first);
        entries.pop_back();
      }
      return entries.front().second;
    }

    std::size_t size() const noexcept { return entries.size(); }
  };

  template <typename Key, typename Compute>
  static const std::wstring& getOrFill(Lru<Key>& lru, Key key,
                                       Compute&& compute) {
    if (const auto* hit = lru.get(key)) {
      return *hit;
    }
    return lru.put(std::move(key), compute());
  }

  Lru<uint64_t>     sizeLru_;
  Lru<std::wstring> typeLru_;
  Lru<uint64_t>     modifiedLru_;
  std::wstring emptyText_;
  std::wstring folderText_;
};

}  // namespace fast_explorer::ui
