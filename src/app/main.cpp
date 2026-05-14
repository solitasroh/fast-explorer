#include <windows.h>
#include <ole2.h>

#include "ui/main-window.h"

namespace {

int runMessageLoop() {
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                     _In_opt_ HINSTANCE /*prev*/,
                     _In_ PWSTR /*cmdLine*/,
                     _In_ int showCommand) {
  HRESULT hr = OleInitialize(nullptr);
  if (FAILED(hr)) {
    return 1;
  }

  int exitCode = 1;
  {
    fast_explorer::ui::MainWindow window;
    if (window.create(instance, showCommand)) {
      exitCode = runMessageLoop();
    }
  }

  OleUninitialize();
  return exitCode;
}
