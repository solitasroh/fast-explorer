#pragma once

#include <string>
#include <string_view>

namespace fast_explorer::core {

// UTF-8 <-> UTF-16 round-trip helpers. Both call the underlying Win32
// converter twice (size query + actual conversion) and return an empty
// result on conversion failure rather than throwing — the caller is
// responsible for validating non-emptiness when that matters.
//
// Existing inline copies in bench/bench-json.cpp, bench/bench-cli.cpp,
// and core/ring-logger.cpp predate this header and are scheduled to
// migrate on their next touch; the centralized version exists so new
// call sites do not become a fifth copy.

[[nodiscard]] std::wstring widenUtf8(std::string_view bytes);
[[nodiscard]] std::string narrowUtf8(std::wstring_view text);

}  // namespace fast_explorer::core
