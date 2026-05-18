#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace fast_explorer::ui {

// Tracks which leaves the user marked with Ctrl+X so MainWindow can
// paint LVIS_CUT on the matching list-view rows.
class CutStateTracker {
 public:
  void mark(std::wstring folderPath, std::vector<std::wstring> leaves);
  void clear() noexcept;

  bool empty() const noexcept { return cutFolderPath_.empty(); }
  const std::wstring& folder() const noexcept { return cutFolderPath_; }
  bool contains(const std::wstring& leaf) const noexcept {
    return cutLeaves_.count(leaf) > 0;
  }

 private:
  std::wstring cutFolderPath_;
  std::unordered_set<std::wstring> cutLeaves_;
};

}  // namespace fast_explorer::ui
