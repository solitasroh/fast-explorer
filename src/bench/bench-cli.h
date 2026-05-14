#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace fast_explorer::bench {

enum class CommandKind {
  None,
  Help,
  Version,
  Generate,
  Enumerate,
};

enum class PresetKind : uint8_t {
  None,
  Small,
  Medium,
  LargeFlat,
  MixedNames,
  MixedTypes,
  ManyDirs,
  DeepTree,
};

struct GenerateArgs {
  PresetKind preset = PresetKind::None;
  std::wstring out;
  uint64_t seed = 1;
};

struct EnumerateArgs {
  std::wstring path;
  int runs = 5;
};

struct ParsedCommand {
  CommandKind kind = CommandKind::None;
  std::wstring errorMessage;
  GenerateArgs generate;
  EnumerateArgs enumerate;
};

inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 64;
inline constexpr int kExitNotImplemented = 69;
inline constexpr int kExitFailure = 70;

PresetKind presetFromName(std::wstring_view name);

const wchar_t* presetName(PresetKind preset);

ParsedCommand parseCommandLine(int argc, const wchar_t* const* argv);

int runCommand(const ParsedCommand& cmd, std::FILE* out, std::FILE* err);

}  // namespace fast_explorer::bench
