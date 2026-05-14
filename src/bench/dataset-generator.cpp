#include "bench/dataset-generator.h"

#include <windows.h>

#include <cwchar>

#include "core/path-utils.h"

namespace fast_explorer::bench {

namespace {

// xorshift64*. Deterministic + cheap. Not cryptographic; only used to
// shuffle name selection in mixed-names so successive runs reproduce.
struct Prng {
  uint64_t state;
  uint64_t next() {
    uint64_t x = state ? state : 0x9E3779B97F4A7C15ULL;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state = x;
    return x;
  }
  uint32_t nextU32() { return static_cast<uint32_t>(next() >> 32); }
};

bool createEmptyFile(const wchar_t* internalPath) {
  HANDLE h = CreateFileW(internalPath, GENERIC_WRITE, 0, nullptr,
                         CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  CloseHandle(h);
  return true;
}

enum class DirState {
  Empty,
  NotEmpty,
  Inaccessible,
};

DirState classifyDir(const std::wstring& internalPath) {
  std::wstring pattern(internalPath);
  pattern.push_back(L'\\');
  pattern.push_back(L'*');
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                              FindExSearchNameMatch, nullptr,
                              FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD err = GetLastError();
    // A wildcard against an existing-but-empty directory still yields
    // at least "." and ".."; only the not-found codes can legitimately
    // be reached for a directory we just created/ensured.
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) {
      return DirState::Empty;
    }
    return DirState::Inaccessible;
  }
  DirState state = DirState::Empty;
  do {
    if (wcscmp(fd.cFileName, L".") != 0 &&
        wcscmp(fd.cFileName, L"..") != 0) {
      state = DirState::NotEmpty;
      break;
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return state;
}

GenerateResult prepareOutDir(const std::wstring& outDisplay,
                             std::wstring& outInternal) {
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toDisplay;
  using fast_explorer::core::toInternal;

  GenerateResult result;
  const PathConvertError pe = toInternal(outDisplay, outInternal);
  if (pe != PathConvertError::None) {
    result.error = GenerateError::OutPathInvalid;
    result.errorDetail = outDisplay;
    return result;
  }
  // ensureDirectoryRecursive walks separators from the front of the
  // string. The \\?\ extended prefix confuses that walk because the
  // first three characters are themselves separators around '?'. Pass
  // the display form (prefix stripped) — both forms resolve to the
  // same directory on disk and the display form is short enough that
  // long-path concerns do not apply during the create-parents step.
  const std::wstring createPath = toDisplay(outInternal);
  if (!fast_explorer::core::ensureDirectoryRecursive(createPath.c_str())) {
    result.error = GenerateError::OutPathCreateFailed;
    result.errorDetail = outDisplay;
    return result;
  }
  const DirState ds = classifyDir(outInternal);
  if (ds == DirState::Inaccessible) {
    result.error = GenerateError::Internal;
    result.errorDetail = outDisplay;
    return result;
  }
  if (ds == DirState::NotEmpty) {
    result.error = GenerateError::OutNotEmpty;
    result.errorDetail = outDisplay;
    return result;
  }
  return result;
}

GenerateResult generateFlat(const std::wstring& outInternal, uint64_t count,
                            const wchar_t* ext) {
  GenerateResult result;
  wchar_t name[64];
  std::wstring path;
  path.reserve(outInternal.size() + 64);
  for (uint64_t i = 1; i <= count; ++i) {
    swprintf_s(name, _countof(name), L"file_%06llu%ls",
               static_cast<unsigned long long>(i), ext);
    path.assign(outInternal);
    path.push_back(L'\\');
    path.append(name);
    if (!createEmptyFile(path.c_str())) {
      result.error = GenerateError::FileCreateFailed;
      result.errorDetail = path;
      return result;
    }
    ++result.filesCreated;
  }
  return result;
}

constexpr const wchar_t* kMixedTypeExts[] = {
    L".txt", L".log", L".md",   L".json", L".xml",  L".cpp", L".h",  L".py",
    L".js",  L".png", L".pdf",  L".csv",  L".html", L".bin", L".zip",
};

constexpr const wchar_t* kMixedNameParts[] = {
    L"한글",       L"日本語",
    L"中文",       L"Ελληνικά",
    L"العربية",    L"file name with spaces",
    L"long_name",  L"emoji_rocket",
    L"latin",      L"αβγδ",
};

GenerateResult generateMixedNames(const std::wstring& outInternal,
                                  uint64_t count, uint64_t seed) {
  GenerateResult result;
  Prng rng{seed};
  wchar_t suffix[32];
  std::wstring path;
  std::wstring name;
  const size_t n = sizeof(kMixedNameParts) / sizeof(kMixedNameParts[0]);
  for (uint64_t i = 1; i <= count; ++i) {
    const wchar_t* part = kMixedNameParts[rng.nextU32() % n];
    swprintf_s(suffix, _countof(suffix), L"_%06llu.txt",
               static_cast<unsigned long long>(i));
    name.assign(part);
    name.append(suffix);
    path.assign(outInternal);
    path.push_back(L'\\');
    path.append(name);
    if (!createEmptyFile(path.c_str())) {
      result.error = GenerateError::FileCreateFailed;
      result.errorDetail = path;
      return result;
    }
    ++result.filesCreated;
  }
  return result;
}

GenerateResult generateMixedTypes(const std::wstring& outInternal,
                                  uint64_t count) {
  GenerateResult result;
  wchar_t name[64];
  std::wstring path;
  const size_t n = sizeof(kMixedTypeExts) / sizeof(kMixedTypeExts[0]);
  for (uint64_t i = 1; i <= count; ++i) {
    const wchar_t* ext = kMixedTypeExts[(i - 1) % n];
    swprintf_s(name, _countof(name), L"item_%06llu%ls",
               static_cast<unsigned long long>(i), ext);
    path.assign(outInternal);
    path.push_back(L'\\');
    path.append(name);
    if (!createEmptyFile(path.c_str())) {
      result.error = GenerateError::FileCreateFailed;
      result.errorDetail = path;
      return result;
    }
    ++result.filesCreated;
  }
  return result;
}

GenerateResult generateManyDirs(const std::wstring& outInternal,
                                uint64_t count) {
  GenerateResult result;
  wchar_t name[32];
  std::wstring path;
  for (uint64_t i = 1; i <= count; ++i) {
    swprintf_s(name, _countof(name), L"folder_%06llu",
               static_cast<unsigned long long>(i));
    path.assign(outInternal);
    path.push_back(L'\\');
    path.append(name);
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
      result.error = GenerateError::DirCreateFailed;
      result.errorDetail = path;
      return result;
    }
    ++result.dirsCreated;
  }
  return result;
}

GenerateResult generateDeepTree(const std::wstring& outInternal, int depth) {
  GenerateResult result;
  std::wstring path(outInternal);
  for (int i = 0; i < depth; ++i) {
    wchar_t level[8];
    swprintf_s(level, _countof(level), L"l%02d", i);
    path.push_back(L'\\');
    path.append(level);
    if (!CreateDirectoryW(path.c_str(), nullptr)) {
      result.error = GenerateError::DirCreateFailed;
      result.errorDetail = path;
      return result;
    }
    ++result.dirsCreated;
  }
  std::wstring leaf = path;
  leaf.append(L"\\leaf.txt");
  if (!createEmptyFile(leaf.c_str())) {
    result.error = GenerateError::FileCreateFailed;
    result.errorDetail = leaf;
    return result;
  }
  ++result.filesCreated;
  return result;
}

}  // namespace

const wchar_t* generateErrorName(GenerateError e) {
  switch (e) {
    case GenerateError::None: return L"None";
    case GenerateError::InvalidPreset: return L"InvalidPreset";
    case GenerateError::OutPathInvalid: return L"OutPathInvalid";
    case GenerateError::OutPathCreateFailed: return L"OutPathCreateFailed";
    case GenerateError::OutNotEmpty: return L"OutNotEmpty";
    case GenerateError::FileCreateFailed: return L"FileCreateFailed";
    case GenerateError::DirCreateFailed: return L"DirCreateFailed";
    case GenerateError::Internal: return L"Internal";
  }
  return L"Unknown";
}

GenerateResult generateDataset(PresetKind preset, const std::wstring& out,
                               uint64_t seed) {
  GenerateResult result;
  if (preset == PresetKind::None) {
    result.error = GenerateError::InvalidPreset;
    return result;
  }
  if (out.empty()) {
    result.error = GenerateError::OutPathInvalid;
    return result;
  }
  std::wstring outInternal;
  const GenerateResult prep = prepareOutDir(out, outInternal);
  if (prep.error != GenerateError::None) {
    return prep;
  }
  switch (preset) {
    case PresetKind::Small:
      return generateFlat(outInternal, 200, L".txt");
    case PresetKind::Medium:
      return generateFlat(outInternal, 10000, L".txt");
    case PresetKind::LargeFlat:
      return generateFlat(outInternal, 100000, L".txt");
    case PresetKind::MixedNames:
      return generateMixedNames(outInternal, 50000, seed);
    case PresetKind::MixedTypes:
      return generateMixedTypes(outInternal, 30000);
    case PresetKind::ManyDirs:
      return generateManyDirs(outInternal, 20000);
    case PresetKind::DeepTree:
      return generateDeepTree(outInternal, 25);
    case PresetKind::None:
      break;
  }
  result.error = GenerateError::InvalidPreset;
  return result;
}

}  // namespace fast_explorer::bench
