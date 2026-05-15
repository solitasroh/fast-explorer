#include "ui/extension-icon-cache.h"

#include <cwctype>

namespace fast_explorer::ui {

namespace {

std::wstring normalize(std::wstring_view extension) {
  std::wstring out;
  out.reserve(extension.size());
  for (wchar_t ch : extension) {
    // ASCII fast path: matches the locale-invariant lowercase
    // semantics used elsewhere (CompareStringOrdinal IgnoreCase).
    // Non-ASCII falls through to std::towlower so a Unicode
    // extension at least normalises to *something*, but real file
    // extensions on Windows are ASCII in practice.
    if (ch >= L'A' && ch <= L'Z') {
      ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
    } else if (static_cast<std::wint_t>(ch) >= 0x80) {
      ch = static_cast<wchar_t>(
          std::towlower(static_cast<std::wint_t>(ch)));
    }
    out.push_back(ch);
  }
  return out;
}

}  // namespace

ExtensionIconCache::ExtensionIconCache(std::size_t capacity) noexcept
    : capacity_(capacity == 0 ? 1 : capacity) {}

int ExtensionIconCache::lookup(std::wstring_view extension) {
  const std::wstring key = normalize(extension);
  auto it = index_.find(key);
  if (it == index_.end()) {
    return kMissingIndex;
  }
  // Promote to MRU.
  entries_.splice(entries_.begin(), entries_, it->second);
  return it->second->value;
}

int ExtensionIconCache::insert(std::wstring_view extension,
                               int imageListIndex, int* evictedOut) {
  if (evictedOut != nullptr) {
    *evictedOut = kMissingIndex;
  }
  std::wstring key = normalize(extension);
  if (auto it = index_.find(key); it != index_.end()) {
    // Refresh the value and bump the entry to MRU.
    it->second->value = imageListIndex;
    entries_.splice(entries_.begin(), entries_, it->second);
    return imageListIndex;
  }
  entries_.push_front(Entry{std::move(key), imageListIndex});
  index_[entries_.front().key] = entries_.begin();
  if (entries_.size() > capacity_) {
    const auto evictedIt = std::prev(entries_.end());
    if (evictedOut != nullptr) {
      *evictedOut = evictedIt->value;
    }
    index_.erase(evictedIt->key);
    entries_.pop_back();
  }
  return imageListIndex;
}

void ExtensionIconCache::clear() noexcept {
  entries_.clear();
  index_.clear();
}

}  // namespace fast_explorer::ui
