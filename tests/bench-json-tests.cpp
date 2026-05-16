#include "test-harness.h"

#include <cstddef>
#include <string>

#include "bench/bench-cli.h"
#include "bench/bench-json.h"
#include "bench/enumeration-bench.h"

using fast_explorer::bench::captureMachineInfo;
using fast_explorer::bench::EnumerateArgs;
using fast_explorer::bench::EnumerationBenchError;
using fast_explorer::bench::EnumerationBenchResult;
using fast_explorer::bench::EnumerationRun;
using fast_explorer::bench::formatEnumerateBenchJson;
using fast_explorer::bench::MachineInfo;

namespace {

bool contains(const std::string& s, std::string_view needle) {
  return s.find(needle) != std::string::npos;
}

}  // namespace

FE_TEST_CASE(BenchJson_CaptureMachineInfo_NonZeroFields) {
  const MachineInfo m = captureMachineInfo();
  FE_ASSERT_FALSE(m.architecture.empty());
  FE_ASSERT_TRUE(m.processorCount > 0);
  FE_ASSERT_TRUE(m.pageSize > 0);
  // On any modern Windows host the major version is at least 6
  // (Vista was 6.0). RtlGetVersion does not lie about manifested
  // compatibility, so this is stable.
  FE_ASSERT_TRUE(m.osMajor >= 6);
}

FE_TEST_CASE(BenchJson_FormatEnumerate_HasAllTopLevelKeys) {
  EnumerateArgs args;
  args.path = L"C:\\tmp\\sample";
  args.runs = 3;
  EnumerationBenchResult r;
  r.medianMicroseconds = 1234;
  r.p95Microseconds = 5678;
  r.totalEntries = 200;
  r.lastRunEntriesBytes = 8000;
  r.lastRunArenaCommittedBytes = 4096;
  r.runs.push_back(EnumerationRun{1234, 200});
  r.workingSet.baselineBytes = 4 * 1024 * 1024;
  r.workingSet.peakBytes = 5 * 1024 * 1024;
  r.workingSet.finalBytes = 4 * 1024 * 1024;
  r.workingSet.postCycleBytes.push_back(4 * 1024 * 1024);
  r.workingSet.maxCycleDriftBytes = 0;

  MachineInfo m;
  m.architecture = "x64";
  m.processorCount = 8;
  m.pageSize = 4096;
  m.osMajor = 10;
  m.osMinor = 0;
  m.osBuild = 22621;

  const std::string json = formatEnumerateBenchJson(args, r, m);
  FE_ASSERT_TRUE(contains(json, "\"machine\":"));
  FE_ASSERT_TRUE(contains(json, "\"architecture\":\"x64\""));
  FE_ASSERT_TRUE(contains(json, "\"args\":"));
  FE_ASSERT_TRUE(contains(json, "\"timing\":"));
  FE_ASSERT_TRUE(contains(json, "\"memory\":"));
  FE_ASSERT_TRUE(contains(json, "\"working_set\":"));
  FE_ASSERT_TRUE(contains(json, "\"median_us\":1234"));
  FE_ASSERT_TRUE(contains(json, "\"p95_us\":5678"));
  FE_ASSERT_TRUE(contains(json, "\"runs\":3"));
  FE_ASSERT_TRUE(contains(json, "\"build\":22621"));
  FE_ASSERT_TRUE(contains(json, "\"path\":\"C:\\\\tmp\\\\sample\""));
  // Trailing brace + matched braces (cheap well-formedness check).
  FE_ASSERT_TRUE(!json.empty() && json.back() == '}');
  std::size_t open = 0;
  std::size_t close = 0;
  for (char c : json) {
    if (c == '{') ++open;
    else if (c == '}') ++close;
  }
  FE_ASSERT_EQ(open, close);
}

FE_TEST_CASE(BenchJson_PathEscaping_QuotesAndBackslashes) {
  EnumerateArgs args;
  args.path = L"C:\\\"weird\"\\path";
  args.runs = 1;
  EnumerationBenchResult r;
  r.runs.push_back(EnumerationRun{0, 0});
  MachineInfo m;
  m.architecture = "x64";
  const std::string json = formatEnumerateBenchJson(args, r, m);
  // Backslash escaped as \\ ; double-quote escaped as \" .
  FE_ASSERT_TRUE(contains(json, "C:\\\\\\\"weird\\\"\\\\path"));
}
