#include "ui/cut-state-tracker.h"

namespace fast_explorer::ui {

void CutStateTracker::mark(std::wstring folderPath,
                            std::vector<std::wstring> leaves) {
  cutFolderPath_ = std::move(folderPath);
  cutLeaves_.clear();
  cutLeaves_.insert(std::make_move_iterator(leaves.begin()),
                    std::make_move_iterator(leaves.end()));
}

void CutStateTracker::clear() noexcept {
  cutFolderPath_.clear();
  cutLeaves_.clear();
}

}  // namespace fast_explorer::ui
