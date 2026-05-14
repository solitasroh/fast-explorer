#pragma once

#include <cstdint>
#include <string>

#include "bench/bench-cli.h"

namespace fast_explorer::bench {

enum class GenerateError : uint8_t {
  None,
  InvalidPreset,
  OutPathInvalid,
  OutPathCreateFailed,
  OutNotEmpty,
  FileCreateFailed,
  DirCreateFailed,
  Internal,
};

struct GenerateResult {
  uint64_t filesCreated = 0;
  uint64_t dirsCreated = 0;
  GenerateError error = GenerateError::None;
  std::wstring errorDetail;
};

const wchar_t* generateErrorName(GenerateError e);

GenerateResult generateDataset(PresetKind preset, const std::wstring& out,
                               uint64_t seed);

}  // namespace fast_explorer::bench
