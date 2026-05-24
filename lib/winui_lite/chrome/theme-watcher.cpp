#include "winui_lite/chrome/theme-watcher.h"

namespace fast_explorer::ui {

bool isAppInDarkMode() noexcept {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion"
                    L"\\Themes\\Personalize",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  DWORD value = 1;  // AppsUseLightTheme: 1 = light (default), 0 = dark
  DWORD size = sizeof(value);
  LONG r = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                            reinterpret_cast<BYTE*>(&value), &size);
  RegCloseKey(key);
  return r == ERROR_SUCCESS && value == 0;
}

bool isThemeSettingChange(WPARAM /*wParam*/, LPARAM lParam) noexcept {
  // WM_SETTINGCHANGE's lParam is a wide string naming the affected
  // setting category. "ImmersiveColorSet" is the broadcast Windows
  // sends when the user flips Apps theme / Windows theme. Other
  // settings (font scaling, accessibility, etc.) fire the same
  // message with different lParam strings; we ignore those here.
  if (lParam == 0) return false;
  return lstrcmpiW(reinterpret_cast<const wchar_t*>(lParam),
                   L"ImmersiveColorSet") == 0;
}

namespace {
// Undocumented since Windows 10 1809 but stable: uxtheme.dll ordinal
// 135 is SetPreferredAppMode(AppMode). Calling AllowDark at process
// start is what lets DarkMode_* window themes take effect on listview
// group headers, comboboxes, etc.
enum PreferredAppMode {
  PAM_Default    = 0,
  PAM_AllowDark  = 1,
  PAM_ForceDark  = 2,
  PAM_ForceLight = 3,
};
using SetPreferredAppMode_t = int (WINAPI*)(PreferredAppMode);
}  // namespace

void enableProcessDarkMode() noexcept {
  static bool tried = false;
  if (tried) return;
  tried = true;
  HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr,
                              LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (ux == nullptr) return;
  auto setMode = reinterpret_cast<SetPreferredAppMode_t>(
      GetProcAddress(ux, MAKEINTRESOURCEA(135)));
  if (setMode != nullptr) setMode(PAM_AllowDark);
  // Module handle intentionally leaked: uxtheme stays loaded for the
  // life of the process via comctl32 / SetWindowTheme anyway, and
  // unloading would race in-flight theme callbacks.
}

RowTheme currentRowTheme() noexcept {
  if (isAppInDarkMode()) {
    return RowTheme{
        /*background*/      RGB(32, 32, 32),
        /*text*/            RGB(241, 241, 241),
        /*disabledText*/    RGB(120, 120, 120),
        /*editBackground*/  RGB(40, 40, 40),
        /*editBorder*/      RGB(80, 80, 80),
        /*hoverPill*/       RGB(56, 56, 56),
    };
  }
  return RowTheme{
      /*background*/      GetSysColor(COLOR_BTNFACE),
      /*text*/            GetSysColor(COLOR_BTNTEXT),
      /*disabledText*/    GetSysColor(COLOR_GRAYTEXT),
      /*editBackground*/  GetSysColor(COLOR_WINDOW),
      /*editBorder*/      RGB(180, 180, 180),
      /*hoverPill*/       RGB(225, 225, 225),
  };
}

}  // namespace fast_explorer::ui
