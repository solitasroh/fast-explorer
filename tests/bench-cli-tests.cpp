#include "bench/bench-cli.h"
#include "test-harness.h"

using fast_explorer::bench::CommandKind;
using fast_explorer::bench::parseCommandLine;

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

FE_TEST_CASE(BenchCli_GenerateSubcommand) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"generate"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Generate);
  FE_ASSERT_TRUE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_EnumerateSubcommand) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"enumerate"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::Enumerate);
  FE_ASSERT_TRUE(p.errorMessage.empty());
}

FE_TEST_CASE(BenchCli_UnknownSubcommand_ReportsError) {
  const wchar_t* argv[] = {L"FastExplorerBench.exe", L"frobnicate"};
  auto p = parseCommandLine(2, argv);
  FE_ASSERT_EQ(p.kind, CommandKind::None);
  FE_ASSERT_TRUE(p.errorMessage.find(L"unknown") != std::wstring::npos);
  FE_ASSERT_TRUE(p.errorMessage.find(L"frobnicate") != std::wstring::npos);
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
