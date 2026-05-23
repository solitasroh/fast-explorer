#pragma once

#include <cstdint>

namespace fast_explorer::core {

// Seam orientation in a 2-pane layout. Lives in core because the
// value is serialized into settings.json and round-tripped across
// sessions; the ui layer re-exports it via a `using` alias so
// callers stay agnostic to the namespace. Keep the enumerator
// names in sync with the kOrient* string labels in
// settings-store.cpp (the serialization mapping lives there).
enum class LayoutOrientation : std::uint8_t { Vertical = 0, Horizontal = 1 };

}  // namespace fast_explorer::core
