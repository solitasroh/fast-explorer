#include "bench/bench-json.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace fast_explorer::bench {

namespace {

const char* archName(WORD wProcessorArchitecture) noexcept {
  switch (wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
    case PROCESSOR_ARCHITECTURE_ARM:   return "arm";
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    case PROCESSOR_ARCHITECTURE_IA64:  return "ia64";
    default:                            return "unknown";
  }
}

// RtlGetVersion bypasses the MS-deprecated VerifyVersionInfo
// behaviour that lies to apps not manifested for the latest OS.
// It is a documented ntdll entry point; we resolve it dynamically
// so we do not have to link ntdll explicitly.
struct RtlOsVersion {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t build = 0;
  bool ok = false;
};

RtlOsVersion queryRtlOsVersion() noexcept {
  RtlOsVersion out;
  HMODULE nt = GetModuleHandleW(L"ntdll.dll");
  if (nt == nullptr) {
    return out;
  }
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  auto fn = reinterpret_cast<RtlGetVersionFn>(
      GetProcAddress(nt, "RtlGetVersion"));
  if (fn == nullptr) {
    return out;
  }
  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  if (fn(&info) != 0 /*STATUS_SUCCESS*/) {
    return out;
  }
  out.major = info.dwMajorVersion;
  out.minor = info.dwMinorVersion;
  out.build = info.dwBuildNumber;
  out.ok = true;
  return out;
}

void appendEscapedJsonString(std::string& out, std::string_view utf8) {
  out.push_back('"');
  for (unsigned char c : utf8) {
    switch (c) {
      case '"':  out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\b': out.append("\\b");  break;
      case '\f': out.append("\\f");  break;
      case '\n': out.append("\\n");  break;
      case '\r': out.append("\\r");  break;
      case '\t': out.append("\\t");  break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04X", c);
          out.append(buf);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
}

std::string wideToUtf8(const std::wstring& wide) {
  if (wide.empty()) {
    return std::string();
  }
  const int needed =
      WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), nullptr, 0,
                          nullptr, nullptr);
  if (needed <= 0) {
    return std::string();
  }
  std::string out(static_cast<std::size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                      static_cast<int>(wide.size()), out.data(), needed,
                      nullptr, nullptr);
  return out;
}

void appendU64(std::string& out, std::uint64_t value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%llu",
                static_cast<unsigned long long>(value));
  out.append(buf);
}

void appendU32(std::string& out, std::uint32_t value) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u", value);
  out.append(buf);
}

}  // namespace

MachineInfo captureMachineInfo() noexcept {
  MachineInfo info;
  SYSTEM_INFO sys{};
  GetNativeSystemInfo(&sys);
  info.architecture = archName(sys.wProcessorArchitecture);
  info.processorCount = sys.dwNumberOfProcessors;
  info.pageSize = sys.dwPageSize;
  const RtlOsVersion ver = queryRtlOsVersion();
  if (ver.ok) {
    info.osMajor = ver.major;
    info.osMinor = ver.minor;
    info.osBuild = ver.build;
  }
  return info;
}

std::string formatEnumerateBenchJson(const EnumerateArgs& args,
                                     const EnumerationBenchResult& result,
                                     const MachineInfo& machine) {
  std::string out;
  out.reserve(1024);
  out.append("{");

  // machine block
  out.append("\"machine\":{\"architecture\":");
  appendEscapedJsonString(out, machine.architecture);
  out.append(",\"processor_count\":");
  appendU32(out, machine.processorCount);
  out.append(",\"page_size\":");
  appendU32(out, machine.pageSize);
  out.append(",\"os\":{\"major\":");
  appendU32(out, machine.osMajor);
  out.append(",\"minor\":");
  appendU32(out, machine.osMinor);
  out.append(",\"build\":");
  appendU32(out, machine.osBuild);
  out.append("}}");

  // args
  out.append(",\"args\":{\"path\":");
  appendEscapedJsonString(out, wideToUtf8(args.path));
  out.append(",\"runs\":");
  appendU32(out, static_cast<std::uint32_t>(args.runs));
  out.append("}");

  // timing
  out.append(",\"timing\":{\"median_us\":");
  appendU64(out, result.medianMicroseconds);
  out.append(",\"p95_us\":");
  appendU64(out, result.p95Microseconds);
  out.append(",\"total_entries\":");
  appendU64(out, result.totalEntries);
  out.append(",\"runs\":[");
  for (std::size_t i = 0; i < result.runs.size(); ++i) {
    if (i > 0) out.append(",");
    out.append("{\"microseconds\":");
    appendU64(out, result.runs[i].microseconds);
    out.append(",\"entries\":");
    appendU64(out, result.runs[i].entriesObserved);
    out.append("}");
  }
  out.append("]}");

  // memory
  out.append(",\"memory\":{\"last_run_entries_bytes\":");
  appendU64(out, result.lastRunEntriesBytes);
  out.append(",\"last_run_arena_committed_bytes\":");
  appendU64(out, result.lastRunArenaCommittedBytes);
  out.append(",\"working_set\":{\"baseline_bytes\":");
  appendU64(out, result.workingSet.baselineBytes);
  out.append(",\"peak_bytes\":");
  appendU64(out, result.workingSet.peakBytes);
  out.append(",\"final_bytes\":");
  appendU64(out, result.workingSet.finalBytes);
  out.append(",\"max_cycle_drift_bytes\":");
  appendU64(out, result.workingSet.maxCycleDriftBytes);
  out.append(",\"post_cycle_bytes\":[");
  for (std::size_t i = 0; i < result.workingSet.postCycleBytes.size(); ++i) {
    if (i > 0) out.append(",");
    appendU64(out, result.workingSet.postCycleBytes[i]);
  }
  out.append("]}}");

  out.append("}");
  return out;
}

}  // namespace fast_explorer::bench
