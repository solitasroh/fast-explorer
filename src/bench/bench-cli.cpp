#include "bench/bench-cli.h"

#include <cstdint>
#include <cwchar>
#include <string_view>

#include "bench/dataset-generator.h"
#include "bench/enumeration-bench.h"

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
    L"  generate   --preset NAME --out DIR [--seed N]\n"
    L"             NAME: small | medium | large-flat | mixed-names |\n"
    L"                   mixed-types | many-dirs | deep-tree\n"
    L"  enumerate  --path DIR [--runs N]\n"
    L"             default --runs 5; range 1..10000\n"
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

bool parseUint64(std::wstring_view s, uint64_t& out) {
  if (s.empty()) {
    return false;
  }
  uint64_t v = 0;
  for (wchar_t c : s) {
    if (c < L'0' || c > L'9') {
      return false;
    }
    const uint64_t d = static_cast<uint64_t>(c - L'0');
    if (v > (UINT64_MAX - d) / 10) {
      return false;
    }
    v = v * 10 + d;
  }
  out = v;
  return true;
}

bool parseRangedInt(std::wstring_view s, int lo, int hi, int& out) {
  if (lo < 0 || hi < lo) {
    return false;
  }
  uint64_t v = 0;
  if (!parseUint64(s, v)) {
    return false;
  }
  if (v < static_cast<uint64_t>(lo) || v > static_cast<uint64_t>(hi)) {
    return false;
  }
  out = static_cast<int>(v);
  return true;
}

void setUsageError(ParsedCommand& result, std::wstring_view msg,
                   std::wstring_view detail = {}) {
  result.kind = CommandKind::None;
  result.errorMessage.assign(msg);
  if (!detail.empty()) {
    result.errorMessage.append(detail);
  }
}

struct OptionToken {
  std::wstring_view name;
  std::wstring_view value;
  bool ok = false;
};

// Reads argv[*i] as an option flag. Handles both --name=value (inline)
// and --name value (next-arg) forms, advancing *i past the consumed
// value for the latter. On positional input or a flag missing its
// required value, stamps result.errorMessage and returns ok=false.
OptionToken readOption(int argc, const wchar_t* const* argv, int* i,
                       ParsedCommand& result) {
  OptionToken tok;
  const std::wstring_view raw = argv[*i] ? std::wstring_view(argv[*i])
                                         : std::wstring_view();
  if (raw.substr(0, 2) != L"--") {
    setUsageError(result, L"unexpected positional argument: ", raw);
    return tok;
  }
  const auto eq = raw.find(L'=');
  if (eq != std::wstring_view::npos) {
    tok.name = raw.substr(2, eq - 2);
    tok.value = raw.substr(eq + 1);
    tok.ok = true;
    return tok;
  }
  tok.name = raw.substr(2);
  if (*i + 1 >= argc) {
    std::wstring detail(L"--");
    detail.append(tok.name);
    setUsageError(result, L"missing value for ", detail);
    return tok;
  }
  ++(*i);
  tok.value = argv[*i] ? std::wstring_view(argv[*i]) : std::wstring_view();
  tok.ok = true;
  return tok;
}

bool parseGenerateArgs(int argc, const wchar_t* const* argv,
                       ParsedCommand& result) {
  bool gotPreset = false;
  bool gotOut = false;
  for (int i = 2; i < argc; ++i) {
    const OptionToken tok = readOption(argc, argv, &i, result);
    if (!tok.ok) {
      return false;
    }
    if (tok.name == L"preset") {
      const PresetKind p = presetFromName(tok.value);
      if (p == PresetKind::None) {
        setUsageError(result, L"invalid --preset value: ", tok.value);
        return false;
      }
      result.generate.preset = p;
      gotPreset = true;
    } else if (tok.name == L"out") {
      if (tok.value.empty()) {
        setUsageError(result, L"--out cannot be empty");
        return false;
      }
      result.generate.out.assign(tok.value);
      gotOut = true;
    } else if (tok.name == L"seed") {
      uint64_t seed = 0;
      if (!parseUint64(tok.value, seed)) {
        setUsageError(result, L"invalid --seed value: ", tok.value);
        return false;
      }
      result.generate.seed = seed;
    } else {
      setUsageError(result, L"unknown option for generate: --", tok.name);
      return false;
    }
  }
  if (!gotPreset) {
    setUsageError(result, L"generate requires --preset");
    return false;
  }
  if (!gotOut) {
    setUsageError(result, L"generate requires --out");
    return false;
  }
  return true;
}

bool parseEnumerateArgs(int argc, const wchar_t* const* argv,
                        ParsedCommand& result) {
  bool gotPath = false;
  for (int i = 2; i < argc; ++i) {
    const OptionToken tok = readOption(argc, argv, &i, result);
    if (!tok.ok) {
      return false;
    }
    if (tok.name == L"path") {
      if (tok.value.empty()) {
        setUsageError(result, L"--path cannot be empty");
        return false;
      }
      result.enumerate.path.assign(tok.value);
      gotPath = true;
    } else if (tok.name == L"runs") {
      int runs = 0;
      if (!parseRangedInt(tok.value, 1, 10000, runs)) {
        setUsageError(result, L"invalid --runs value (1..10000): ", tok.value);
        return false;
      }
      result.enumerate.runs = runs;
    } else {
      setUsageError(result, L"unknown option for enumerate: --", tok.name);
      return false;
    }
  }
  if (!gotPath) {
    setUsageError(result, L"enumerate requires --path");
    return false;
  }
  return true;
}

int runGenerate(const GenerateArgs& args, std::FILE* out, std::FILE* err) {
  std::fwprintf(out, L"generate preset=%ls out=%ls seed=%llu\n",
                presetName(args.preset), args.out.c_str(),
                static_cast<unsigned long long>(args.seed));
  const GenerateResult r = generateDataset(args.preset, args.out, args.seed);
  if (r.error != GenerateError::None) {
    std::fwprintf(err, L"generate failed: %ls (%ls)\n",
                  generateErrorName(r.error),
                  r.errorDetail.empty() ? L"" : r.errorDetail.c_str());
    return kExitFailure;
  }
  std::fwprintf(out, L"created files=%llu dirs=%llu\n",
                static_cast<unsigned long long>(r.filesCreated),
                static_cast<unsigned long long>(r.dirsCreated));
  return kExitOk;
}

int runEnumerate(const EnumerateArgs& args, std::FILE* out, std::FILE* err) {
  std::fwprintf(out, L"enumerate path=%ls runs=%d\n", args.path.c_str(),
                args.runs);
  const EnumerationBenchResult r = runEnumerationBench(args.path, args.runs);
  if (r.error != EnumerationBenchError::None) {
    std::fwprintf(err, L"enumerate failed: %ls (%ls)\n",
                  enumerationBenchErrorName(r.error),
                  r.errorDetail.empty() ? L"" : r.errorDetail.c_str());
    return kExitFailure;
  }
  for (size_t i = 0; i < r.runs.size(); ++i) {
    std::fwprintf(out, L"  run[%zu] %llu us  entries=%llu\n", i,
                  static_cast<unsigned long long>(r.runs[i].microseconds),
                  static_cast<unsigned long long>(r.runs[i].entriesObserved));
  }
  std::fwprintf(out, L"median=%llu us  p95=%llu us  entries=%llu\n",
                static_cast<unsigned long long>(r.medianMicroseconds),
                static_cast<unsigned long long>(r.p95Microseconds),
                static_cast<unsigned long long>(r.totalEntries));
  return kExitOk;
}

}  // namespace

PresetKind presetFromName(std::wstring_view name) {
  if (name == L"small") return PresetKind::Small;
  if (name == L"medium") return PresetKind::Medium;
  if (name == L"large-flat") return PresetKind::LargeFlat;
  if (name == L"mixed-names") return PresetKind::MixedNames;
  if (name == L"mixed-types") return PresetKind::MixedTypes;
  if (name == L"many-dirs") return PresetKind::ManyDirs;
  if (name == L"deep-tree") return PresetKind::DeepTree;
  return PresetKind::None;
}

const wchar_t* presetName(PresetKind preset) {
  switch (preset) {
    case PresetKind::Small: return L"small";
    case PresetKind::Medium: return L"medium";
    case PresetKind::LargeFlat: return L"large-flat";
    case PresetKind::MixedNames: return L"mixed-names";
    case PresetKind::MixedTypes: return L"mixed-types";
    case PresetKind::ManyDirs: return L"many-dirs";
    case PresetKind::DeepTree: return L"deep-tree";
    case PresetKind::None: return L"";
  }
  return L"";
}

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
    if (!parseGenerateArgs(argc, argv, result)) {
      return result;
    }
    return result;
  }
  if (first == L"enumerate") {
    result.kind = CommandKind::Enumerate;
    if (!parseEnumerateArgs(argc, argv, result)) {
      return result;
    }
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
      return runGenerate(cmd.generate, out, err);
    case CommandKind::Enumerate:
      return runEnumerate(cmd.enumerate, out, err);
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
