#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fast_explorer::ui {

enum class PasteResult : std::uint8_t {
  Success,
  NoData,
  NoTarget,
  Rejected,
};

class ClipboardOps {
 public:
  // OLE clipboard write. cut => CFSTR_PREFERREDDROPEFFECT = MOVE.
  static bool copy(const std::wstring& folderPath,
                   const std::vector<std::wstring>& selectedLeaves,
                   bool cut);

  // Shell IDropTarget delegation honouring CFSTR_PREFERREDDROPEFFECT;
  // shell runs progress / conflict UI itself.
  static PasteResult paste(const std::wstring& folderPath, HWND ownerHwnd);
};

}  // namespace fast_explorer::ui
