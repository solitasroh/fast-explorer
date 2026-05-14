#include "bench/bench-cli.h"
#include "test-harness.h"

using fast_explorer::bench::CommandKind;
using fast_explorer::bench::parseCommandLine;
using fast_explorer::bench::PresetKind;
using fast_explorer::bench::presetFromName;
using fast_explorer::bench::presetName;

FE_TEST_CASE(BenchCli_NoArgs_ReportsMissingCommand) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe"};
  auto p = parseCommandLine(1, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_FALSE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_NullArgv_ReportsMissingCommand) {
  auto p = parseCommandLine(3, nullptr);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_FALSE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_LongHelp) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"--help"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Help);
  FE_ASSERT_TRUE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_ShortHelp) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"-h"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Help);
}

FE_TEST_CASE(BenchCli_SlashQuestionHelp) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"/?"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Help);
}

FE_TEST_CASE(BenchCli_LongVersion) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"--version"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Version);
}

FE_TEST_CASE(BenchCli_ShortVersion) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"-v"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Version);
}

FE_TEST_CASE(BenchCli_UnknownSubcommand_ReportsError) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"frobnicate"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"unknown") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_EmptyFirstArg_ReportsUnknown) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L""};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_FALSE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_NullSecondArg_ReportsUnknown) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", nullptr};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_FALSE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_PresetFromName_AllSeven) {
  FE_ASSERT_EQ(presetFromName(L"small"), PresetKind::Small);
  FE_ASSERT_EQ(presetFromName(L"medium"), PresetKind::Medium);
  FE_ASSERT_EQ(presetFromName(L"large-flat"), PresetKind::LargeFlat);
  FE_ASSERT_EQ(presetFromName(L"mixed-names"), PresetKind::MixedNames);
  FE_ASSERT_EQ(presetFromName(L"mixed-types"), PresetKind::MixedTypes);
  FE_ASSERT_EQ(presetFromName(L"many-dirs"), PresetKind::ManyDirs);
  FE_ASSERT_EQ(presetFromName(L"deep-tree"), PresetKind::DeepTree);
}

FE_TEST_CASE(BenchCli_PresetFromName_UnknownReturnsNone) {
  FE_ASSERT_EQ(presetFromName(L"bogus"), PresetKind::None);
  FE_ASSERT_EQ(presetFromName(L""), PresetKind::None);
  FE_ASSERT_EQ(presetFromName(L"SMALL"), PresetKind::None);
}

FE_TEST_CASE(BenchCli_PresetName_Roundtrip) {
  FE_ASSERT_WSTREQ(presetName(PresetKind::Small), L"small");
  FE_ASSERT_WSTREQ(presetName(PresetKind::Medium), L"medium");
  FE_ASSERT_WSTREQ(presetName(PresetKind::LargeFlat), L"large-flat");
  FE_ASSERT_WSTREQ(presetName(PresetKind::DeepTree), L"deep-tree");
  FE_ASSERT_WSTREQ(presetName(PresetKind::None), L"");
}

FE_TEST_CASE(BenchCli_Generate_SeparateValues) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset", L"small", L"--out", L"C:\\tmp\\x"};
  auto p = parseCommandLine(6, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Generate);
  FE_ASSERT_EQ(p.generate.preset, PresetKind::Small);
  FE_ASSERT_WSTREQ(p.generate.out, L"C:\\tmp\\x");
  FE_ASSERT_EQ(p.generate.seed, 1ULL);
}

FE_TEST_CASE(BenchCli_Generate_InlineValues) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset=medium", L"--out=C:\\tmp\\y"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Generate);
  FE_ASSERT_EQ(p.generate.preset, PresetKind::Medium);
  FE_ASSERT_WSTREQ(p.generate.out, L"C:\\tmp\\y");
}

FE_TEST_CASE(BenchCli_Generate_WithSeed) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset=small", L"--out=X", L"--seed=42"};
  auto p = parseCommandLine(5, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Generate);
  FE_ASSERT_EQ(p.generate.seed, 42ULL);
}

FE_TEST_CASE(BenchCli_Generate_MissingPreset) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--out=X"};
  auto p = parseCommandLine(3, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"--preset") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Generate_MissingOut) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset=small"};
  auto p = parseCommandLine(3, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"--out") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Generate_BogusPreset) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset=bogus", L"--out=X"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"invalid") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Generate_BogusSeed) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset=small", L"--out=X", L"--seed=notanum"};
  auto p = parseCommandLine(5, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"seed") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Generate_MissingValueAfterFlag) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset"};
  auto p = parseCommandLine(3, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"missing value") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Generate_UnknownOption) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate",
                           L"--preset=small", L"--out=X", L"--foo=1"};
  auto p = parseCommandLine(5, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"unknown option") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Enumerate_PathOnly_DefaultRuns) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate",
                           L"--path", L"C:\\tmp"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Enumerate);
  FE_ASSERT_WSTREQ(p.enumerate.path, L"C:\\tmp");
  FE_ASSERT_EQ(p.enumerate.runs, 5);
}

FE_TEST_CASE(BenchCli_Enumerate_WithRuns) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate",
                           L"--path=C:\\tmp", L"--runs=10"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Enumerate);
  FE_ASSERT_EQ(p.enumerate.runs, 10);
}

FE_TEST_CASE(BenchCli_Enumerate_MissingPath) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate",
                           L"--runs=5"};
  auto p = parseCommandLine(3, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"--path") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Enumerate_ZeroRuns_Rejected) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate",
                           L"--path=X", L"--runs=0"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"runs") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Enumerate_RunsTooLarge_Rejected) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate",
                           L"--path=X", L"--runs=99999"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"runs") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Enumerate_UnknownOption) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate",
                           L"--path=X", L"--bogus=1"};
  auto p = parseCommandLine(4, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"unknown option") != std::wstring::npos);
}

FE_TEST_CASE(BenchCli_Enumerate_PositionalRejected) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate", L"C:\\x"};
  auto p = parseCommandLine(3, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"positional") != std::wstring::npos);
}
