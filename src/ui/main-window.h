#pragma once

#include <windows.h>

namespace fast_explorer::ui {

class MainWindow {
 public:
  MainWindow() = default;
  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool create(HINSTANCE instance, int showCommand);
  HWND handle() const noexcept { return hwnd_; }

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  static constexpr const wchar_t* kClassName = L"FastExplorer.MainWindow";
  static constexpr int kDefaultWidth = 1280;
  static constexpr int kDefaultHeight = 800;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
};

}  // namespace fast_explorer::ui
