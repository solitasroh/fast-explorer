#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/settings-store.h"
#include "explorer/pane-controller.h"
#include "winui_lite/widgets/tab-strip.h"

namespace fast_explorer::ui {

class MainWindow;

class PaneTabHost {
 public:
  PaneTabHost(MainWindow* host, std::size_t paneIdx,
              PaneController*& activeCell);
  ~PaneTabHost();

  PaneTabHost(const PaneTabHost&) = delete;
  PaneTabHost& operator=(const PaneTabHost&) = delete;

  void openNewTab();
  void openInNewTab(const std::wstring& path);
  void closeTab(std::size_t idx);
  void closeOtherTabs(std::size_t keepIdx);
  void closeTabsToRight(std::size_t idx);
  void activateTab(std::size_t idx);
  void moveTab(std::size_t from, std::size_t to);
  void cycleNext();
  void cyclePrev();

  void restoreFromSession(const core::PaneSessionV6& panel);
  core::PaneSessionV6 captureSession() const;

  std::size_t tabCount() const noexcept { return tabs_.size(); }
  std::size_t activeTabIdx() const noexcept { return activeTab_; }
  PaneController& activeTab() noexcept;
  PaneController& tabAt(std::size_t idx) noexcept;

  HWND stripHandle() const noexcept;
  int  stripPreferredHeight() const;

  // Visible for testing only — production attaches via constructor.
  static std::wstring resolveHomeFolder();

 private:
  void rebuildStrip();
  std::wstring homeFolder() const;

  MainWindow* host_;
  std::size_t paneIdx_;
  PaneController*& activeCell_;
  std::vector<std::unique_ptr<PaneController>> tabs_;
  std::size_t activeTab_ = 0;
  std::unique_ptr<TabStrip> strip_;
};

}  // namespace fast_explorer::ui
