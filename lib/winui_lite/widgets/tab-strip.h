#pragma once

#include <windows.h>

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#include "winui_lite/widgets/tab-strip-geometry.h"

namespace fast_explorer::ui {

// Per-instance palette that is rebuilt whenever the OS theme changes.
// Colours are chosen to match pane-toolbar-row / status-bar styling.
struct TabPalette {
  COLORREF inactiveBg;    // inactive tab background
  COLORREF activeBg;      // active   tab background
  COLORREF border;        // tab border lines
  COLORREF text;          // tab title text
  COLORREF hoverCloseX;   // hovered close-X background (Explorer red)
  COLORREF dropIndicator; // drag drop-indicator line (matches border)
};

class TabStrip {
 public:
  TabStrip(HWND parent, std::size_t paneIdx);
  ~TabStrip();

  TabStrip(const TabStrip&) = delete;
  TabStrip& operator=(const TabStrip&) = delete;

  HWND handle() const noexcept { return hwnd_; }
  int preferredHeight() const;

  // Replace the model. Pass an empty span to clear. Triggers paint.
  void setTabs(std::span<const TabModel> models);
  void setActive(std::size_t idx);

  // Wired by the host once at construction time.
  std::function<void(std::size_t)> onActivate;
  std::function<void(std::size_t)> onClose;
  std::function<void()> onNew;
  std::function<void(std::size_t /*from*/, std::size_t /*to*/)> onReorder;
  std::function<void(std::size_t /*idx*/, POINT /*screen*/)>
      onContextMenu;

 public:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

 private:
  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

  void paint(HDC dc, const RECT& client);
  void rebuildRects();
  int  hitTabAt(int x);
  int  hitCloseAt(int x);
  void refreshPalette() noexcept;

  HWND hwnd_;
  std::size_t paneIdx_;
  TabPalette palette_{};
  std::vector<TabModel> models_;
  std::size_t active_ = 0;
  int scrollOffset_ = 0;
  std::vector<TabRect> rects_;

  // Hover state for repaint deltas
  int hoveredTab_ = -1;
  int hoveredCloseX_ = -1;

  // Drag state
  bool dragging_ = false;
  bool dragArmed_ = false;
  int  dragStartX_ = 0;
  std::size_t dragFromIdx_ = 0;
  int  dropIndicatorX_ = -1;
};

}  // namespace fast_explorer::ui
