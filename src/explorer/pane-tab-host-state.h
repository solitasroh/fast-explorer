#pragma once
#include <cstddef>

namespace fast_explorer::ui::detail {

// Returns the new activeTab index after erasing `eraseIdx` from a
// tabs vector of size `oldSize`. Caller must ensure eraseIdx < oldSize
// and oldSize >= 2 (single-tab close is handled separately).
inline std::size_t adjustActiveAfterErase(std::size_t active,
                                          std::size_t eraseIdx,
                                          std::size_t oldSize) {
  if (eraseIdx < active) return active - 1;
  if (eraseIdx == active) {
    const std::size_t newSize = oldSize - 1;
    return (active >= newSize) ? newSize - 1 : active;
  }
  return active;
}

// Returns the new activeTab index after moving the element at `from`
// to position `to` in a tabs vector of size `n`.
inline std::size_t adjustActiveAfterMove(std::size_t active,
                                         std::size_t from,
                                         std::size_t to) {
  if (active == from) return to;
  if (from < active && active <= to) return active - 1;
  if (to <= active && active < from) return active + 1;
  return active;
}

}  // namespace fast_explorer::ui::detail
