#pragma once

#include <cstdio>
#include <string>

namespace fast_explorer::bench {

enum class CommandKind {
  None,
  Help,
  Version,
  Generate,
  Enumerate,
};

struct ParsedCommand {
  CommandKind kind = CommandKind::None;
  std::wstring errorMessage;
};

inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 64;
inline constexpr int kExitNotImplemented = 69;

ParsedCommand parseCommandLine(int argc, const wchar_t* const* argv);

int runCommand(const ParsedCommand& cmd, std::FILE* out, std::FILE* err);

}  // namespace fast_explorer::bench
