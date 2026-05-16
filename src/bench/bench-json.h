#pragma once

#include <cstdint>
#include <string>

#include "bench/bench-cli.h"
#include "bench/enumeration-bench.h"

namespace fast_explorer::bench {

struct MachineInfo {
  std::string architecture;          // "x64", "arm64", "x86", ...
  std::uint32_t processorCount = 0;  // SYSTEM_INFO::dwNumberOfProcessors
  std::uint32_t pageSize = 0;        // SYSTEM_INFO::dwPageSize
  std::uint32_t osMajor = 0;
  std::uint32_t osMinor = 0;
  std::uint32_t osBuild = 0;
};

MachineInfo captureMachineInfo() noexcept;

// Serializes an EnumerationBenchResult + run args + machine info to
// a compact UTF-8 JSON document. Caller writes the returned bytes
// to stdout or a file; no trailing newline is emitted.
std::string formatEnumerateBenchJson(const EnumerateArgs& args,
                                     const EnumerationBenchResult& result,
                                     const MachineInfo& machine);

}  // namespace fast_explorer::bench
