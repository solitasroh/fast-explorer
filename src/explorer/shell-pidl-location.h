#pragma once

#include <shlobj.h>

#include <string>

namespace fast_explorer::ui {

[[nodiscard]] std::wstring addressLocationForPidl(LPCITEMIDLIST absolute);

}  // namespace fast_explorer::ui
