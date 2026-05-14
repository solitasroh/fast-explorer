#pragma once

#include <windows.h>

#include <memory>
#include <string>

namespace fast_explorer::core {
class ProcessMemoryService;
class PerfTracker;
}

namespace fast_explorer::ui {

class PaneController;

class MainWindow {
 public:
  // `memory` is non-owning; the AppServices owner must outlive the window.
  MainWindow(fast_explorer::core::ProcessMemoryService& memory,
             fast_explorer::core::PerfTracker& perf) noexcept;
  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool create(HINSTANCE instance, int showCommand);
  HWND handle() const noexcept { return hwnd_; }

  // Forwards to the owned PaneController. Returns false if the window
  // is not yet created or the path is invalid.
  bool openFolder(const std::wstring& path);

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleListViewNotify(NMHDR* hdr);
  void handleGetDispInfo(NMHDR* hdr);
  LRESULT handleCustomDraw(NMHDR* hdr);

  static constexpr const wchar_t* kClassName = L"FastExplorer.MainWindow";
  static constexpr int kDefaultWidth = 1280;
  static constexpr int kDefaultHeight = 800;

  void setStatusText(const wchar_t* text);

  fast_explorer::core::ProcessMemoryService& memory_;
  fast_explorer::core::PerfTracker& perf_;
  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND listView_ = nullptr;
  HWND statusBar_ = nullptr;
  bool firstBatchSeen_ = false;
  std::unique_ptr<PaneController> pane_;
  std::unique_ptr<class FormatCache> formatCache_;
};

}  // namespace fast_explorer::ui
