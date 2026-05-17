#pragma once

#include <string>
#include <string_view>

namespace fast_explorer::core {

// UTF-8 <-> UTF-16 round-trip helpers. Both call the underlying Win32
// converter twice (size query + actual conversion) and return an empty
// result on conversion failure rather than throwing — the caller is
// responsible for validating non-emptiness when that matters.
//
// Note on core/ring-logger.cpp: that TU intentionally uses a stack
// buffer (no heap allocation) for the publish hot path and the crash
// handler re-entry case, so it does NOT call these helpers.

[[nodiscard]] std::wstring widenUtf8(std::string_view bytes);
[[nodiscard]] std::string narrowUtf8(std::wstring_view text);

}  // namespace fast_explorer::core
