// dark-scrollbar-hook.cpp
//
// See dark-scrollbar-hook.h for the rationale. The IAT-walk helpers
// (FindDelayLoadThunkInModule and friends) are adapted from
// stevemk14ebr/PolyHook_2_0 (MIT) via ysc3839/win32-darkmode, the
// reference Win32 dark-mode implementation. Kept in an anonymous
// namespace so they do not leak symbols.
//
// Implementation notes
// --------------------
// - We hook the DELAY-LOAD thunk in comctl32.dll, not its regular IAT.
//   Modern comctl32 v6 imports uxtheme via the delay-load mechanism so
//   the function pointer lives in a separate IMAGE_DELAYLOAD_DESCRIPTOR
//   table reachable through IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT.
// - Ordinal 49 = OpenNcThemeData. Stable since at least Win10 1809;
//   still present on Win11 24H2 / 25H2.
// - VirtualProtect to PAGE_READWRITE for the single 8-byte slot, restore
//   the original protection afterwards. Avoids leaving the .idata
//   page writable for the rest of the process.
// - Once installed the trampoline is leaked intentionally — uxtheme +
//   comctl32 are pinned for the life of the process anyway.

#include "winui_lite/chrome/dark-scrollbar-hook.h"

#include <windows.h>
#include <delayimp.h>
#include <uxtheme.h>

#include <cstdint>
#include <cstring>

namespace fast_explorer::ui {

namespace {

// -- PE / IAT helpers (adapted from PolyHook_2_0/IatHook.cpp, MIT). ---------

template <typename T, typename T1, typename T2>
constexpr T rva2va(T1 base, T2 rva) {
  return reinterpret_cast<T>(reinterpret_cast<ULONG_PTR>(base) +
                             static_cast<ULONG_PTR>(rva));
}

template <typename T>
T dataDirectoryFromModuleBase(void* moduleBase, size_t entryID) {
  auto dosHdr = static_cast<PIMAGE_DOS_HEADER>(moduleBase);
  auto ntHdr  = rva2va<PIMAGE_NT_HEADERS>(moduleBase, dosHdr->e_lfanew);
  const auto& dataDir = ntHdr->OptionalHeader.DataDirectory[entryID];
  if (dataDir.VirtualAddress == 0) return nullptr;
  return rva2va<T>(moduleBase, dataDir.VirtualAddress);
}

PIMAGE_THUNK_DATA findAddressByOrdinal(PIMAGE_THUNK_DATA impName,
                                       PIMAGE_THUNK_DATA impAddr,
                                       std::uint16_t ordinal) noexcept {
  for (; impName->u1.Ordinal != 0; ++impName, ++impAddr) {
    if (IMAGE_SNAP_BY_ORDINAL(impName->u1.Ordinal) &&
        IMAGE_ORDINAL(impName->u1.Ordinal) == ordinal) {
      return impAddr;
    }
  }
  return nullptr;
}

// Walk the delay-load descriptor table of `moduleBase` looking for an
// import of `dllName` referenced by ordinal `ordinal`. Returns the
// IMAGE_THUNK_DATA slot whose u1.Function field is the function pointer
// the loader will patch (or has patched) — i.e. exactly the slot we
// want to overwrite.
PIMAGE_THUNK_DATA findDelayLoadThunkByOrdinal(void* moduleBase,
                                              const char* dllName,
                                              std::uint16_t ordinal) noexcept {
  auto imports = dataDirectoryFromModuleBase<PIMAGE_DELAYLOAD_DESCRIPTOR>(
      moduleBase, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
  if (imports == nullptr) return nullptr;
  for (; imports->DllNameRVA != 0; ++imports) {
    auto name = rva2va<LPCSTR>(moduleBase, imports->DllNameRVA);
    if (_stricmp(name, dllName) != 0) continue;
    auto impName =
        rva2va<PIMAGE_THUNK_DATA>(moduleBase, imports->ImportNameTableRVA);
    auto impAddr =
        rva2va<PIMAGE_THUNK_DATA>(moduleBase, imports->ImportAddressTableRVA);
    return findAddressByOrdinal(impName, impAddr, ordinal);
  }
  return nullptr;
}

// -- Trampoline state ------------------------------------------------------

using OpenNcThemeData_t = HTHEME(WINAPI*)(HWND, LPCWSTR);
OpenNcThemeData_t g_origOpenNcThemeData = nullptr;

// Replacement for uxtheme!OpenNcThemeData that comctl32 will call from
// its scrollbar code paths. Substitutes "ScrollBar" → "Explorer::Scroll
// Bar" so the dark variant is loaded; passes through every other class
// unchanged. HWND is nulled when the substitution fires — the Win11
// Explorer code path does the same and avoids any per-window theme
// state interference (the "ScrollBar" class is window-independent for
// non-client scrollbar drawing anyway).
HTHEME WINAPI myOpenNcThemeData(HWND hWnd, LPCWSTR classList) noexcept {
  if (classList != nullptr && wcscmp(classList, L"ScrollBar") == 0) {
    hWnd      = nullptr;
    classList = L"Explorer::ScrollBar";
  }
  return g_origOpenNcThemeData != nullptr
             ? g_origOpenNcThemeData(hWnd, classList)
             : nullptr;
}

}  // namespace

void installDarkScrollBarHook() noexcept {
  static bool installed = false;
  if (installed) return;
  installed = true;  // mark before any early-return: we never retry.

  // Resolve uxtheme!OpenNcThemeData via ordinal 49 so we can chain to
  // the real implementation after our class substitution. If this
  // fails the hook is meaningless — bail out.
  HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (uxtheme == nullptr) return;
  g_origOpenNcThemeData = reinterpret_cast<OpenNcThemeData_t>(
      GetProcAddress(uxtheme, MAKEINTRESOURCEA(49)));
  if (g_origOpenNcThemeData == nullptr) return;

  // The scrollbar code we care about lives in comctl32.dll v6. Without
  // this load comctl32 might not be pinned yet (it is in practice
  // after InitCommonControlsEx, but the explicit Load makes the order
  // independent of the call site).
  HMODULE comctl = LoadLibraryExW(L"comctl32.dll", nullptr,
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (comctl == nullptr) return;

  PIMAGE_THUNK_DATA thunk =
      findDelayLoadThunkByOrdinal(comctl, "uxtheme.dll", 49);
  if (thunk == nullptr) return;

  DWORD oldProtect = 0;
  if (!VirtualProtect(thunk, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE,
                      &oldProtect)) {
    return;
  }
  thunk->u1.Function = reinterpret_cast<ULONG_PTR>(&myOpenNcThemeData);
  VirtualProtect(thunk, sizeof(IMAGE_THUNK_DATA), oldProtect, &oldProtect);

  // Modules are intentionally NOT freed: uxtheme + comctl32 stay
  // loaded for the lifetime of any GUI app, and unloading would race
  // against in-flight theme callbacks.
}

}  // namespace fast_explorer::ui
