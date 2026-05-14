#include "ui/stall-probe.h"

namespace fast_explorer::ui {

StallLevel classifyStall(uint64_t gapMicros) {
  if (gapMicros >= kStallErrorMicros) return StallLevel::Error;
  if (gapMicros >= kStallWarnMicros)  return StallLevel::Warn;
  if (gapMicros >= kStallInfoMicros)  return StallLevel::Info;
  return StallLevel::None;
}

}  // namespace fast_explorer::ui
