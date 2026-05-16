#pragma once

#include <span>
#include <string>
#include <string_view>

namespace fast_explorer::ui {

// Returns a leaf name that does not collide with `existingNames`.
// Tries `base`, then `base (2)`, `base (3)`, ... Comparison uses
// Win32 ordinal case-insensitive folding.
std::wstring uniqueFolderLeaf(
    std::span<const std::wstring_view> existingNames,
    std::wstring_view base);

}  // namespace fast_explorer::ui
