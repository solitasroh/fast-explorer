#pragma once

#include <cstdint>

namespace fast_explorer::ui {

enum class StallLevel : uint8_t {
  None,
  Info,
  Warn,
  Error,
};

inline constexpr uint64_t kStallInfoMicros  = 50'000;
inline constexpr uint64_t kStallWarnMicros  = 100'000;
inline constexpr uint64_t kStallErrorMicros = 500'000;

StallLevel classifyStall(uint64_t gapMicros);

}  // namespace fast_explorer::ui
