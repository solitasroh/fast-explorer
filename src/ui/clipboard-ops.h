#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace fast_explorer::ui {

class ClipboardOps {
 public:
  // Place the selection on the OLE clipboard. `cut` flags the data
  // object with CFSTR_PREFERREDDROPEFFECT = DROPEFFECT_MOVE so a
  // subsequent paste moves rather than copies.
  static bool copy(const std::wstring& folderPath,
                   const std::vector<std::wstring>& selectedLeaves,
                   bool cut);

  // Drops the OLE clipboard's IDataObject onto `folderPath` via the
  // shell IDropTarget, which honours CFSTR_PREFERREDDROPEFFECT and
  // runs the shell's progress / conflict UI itself.
  static bool paste(const std::wstring& folderPath, HWND ownerHwnd);
};

}  // namespace fast_explorer::ui
