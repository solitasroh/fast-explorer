#include "bench/bench-cli.h"

#include <cwchar>
#include <string_view>

namespace fast_explorer::bench {

namespace {

constexpr const wchar_t* kVersionString = L"FastExplorerBench 0.1.0";

constexpr const wchar_t* kHelpText =
    L"FastExplorerBench - directory enumeration benchmark harness\n"
    L"\n"
    L"Usage:\n"
    L"  FastExplorerBench.exe <command> [options]\n"
    L"  FastExplorerBench.exe --help\n"
    L"  FastExplorerBench.exe --version\n"
    L"\n"
    L"Commands:\n"
    L"  generate    create a synthetic directory tree for benchmarking\n"
    L"  enumerate   enumerate a directory and report timing statistics\n"
    L"\n"
    L"Global options:\n"
    L"  -h, --help     show this help and exit\n"
    L"  -v, --version  show version and exit\n";

bool isHelpFlag(std::wstring_view a) {
  return a == L"--help" || a == L"-h" || a == L"/?";
}

bool isVersionFlag(std::wstring_view a) {
  return a == L"--version" || a == L"-v";
}

}  // namespace

ParsedCommand parseCommandLine(int argc, const wchar_t* const* argv) {
  ParsedCommand result;
  if (argv == nullptr || argc < 2) {
    result.errorMessage = L"missing command (try --help)";
    return result;
  }

  const wchar_t* raw = argv[1];
  const std::wstring_view first = raw ? raw : L"";

  if (isHelpFlag(first)) {
    result.kind = CommandKind::Help;
    return result;
  }
  if (isVersionFlag(first)) {
    result.kind = CommandKind::Version;
    return result;
  }
  if (first == L"generate") {
    result.kind = CommandKind::Generate;
    return result;
  }
  if (first == L"enumerate") {
    result.kind = CommandKind::Enumerate;
    return result;
  }

  result.errorMessage = L"unknown command: ";
  result.errorMessage.append(first);
  return result;
}

int runCommand(const ParsedCommand& cmd, std::FILE* out, std::FILE* err) {
  switch (cmd.kind) {
    case CommandKind::Help:
      std::fputws(kHelpText, out);
      return kExitOk;
    case CommandKind::Version:
      std::fputws(kVersionString, out);
      std::fputwc(L'\n', out);
      return kExitOk;
    case CommandKind::Generate:
      std::fputws(L"generate: not implemented yet\n", err);
      return kExitNotImplemented;
    case CommandKind::Enumerate:
      std::fputws(L"enumerate: not implemented yet\n", err);
      return kExitNotImplemented;
    case CommandKind::None:
      if (!cmd.errorMessage.empty()) {
        std::fputws(L"error: ", err);
        std::fputws(cmd.errorMessage.c_str(), err);
        std::fputwc(L'\n', err);
      }
      return kExitUsage;
  }
  return kExitUsage;
}

}  // namespace fast_explorer::bench
