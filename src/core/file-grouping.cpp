#include "core/file-grouping.h"

#include "core/file-entry.h"

namespace fast_explorer::core {

int32_t groupIdForEntry(GroupKey key,
                        const FileEntry& /*entry*/,
                        uint64_t /*nowFiletime*/) noexcept {
  if (key == GroupKey::None) {
    return 0;
  }
  return 0;  // other keys filled in later tasks
}

}  // namespace fast_explorer::core
