// main.cpp — winui_lite_demo entry point.
//
// Boots a small Win32 window that uses ONLY lib/winui_lite/* and the
// in-memory adapters in this directory. Zero dependency on src/ —
// the build is parallel proof that the lib really is shell-free.
//
// What runs end-to-end:
//   * WindowBase (chrome) handles class registration + WndProc dispatch
//   * StatusBar (chrome) renders the bottom strip with dark-mode tinting
//   * CommandRouter (chrome) routes F2 -> 'rename pretend' message box
//   * TabStrip (widget) demonstrates tab activation, close, new, reorder
//     using three in-memory TabModels — no shell linkage whatsoever.
//   * InMemoryItemSource (port impl in this file's directory) feeds
//     ten fake items into a host-created LVS_OWNERDATA list-view
//   * LVN_GETDISPINFO pulls cells from the same object via
//     ItemDispatcher::textFor
//
// What is intentionally NOT here:
//   * Splitter / multi-pane chrome. The plan's step 13 mentions a
//     4-pane layout; today PaneManager + splitters need a host that
//     drives setActivePane / relayout, so the demo restricts to a
//     single list-view + status bar to stay self-contained.
//   * Icons. iconIndexFor returns -1 in InMemoryItemSource, so the
//     list-view paints rows without an LVIF_IMAGE column.

#include <windows.h>
#include <commctrl.h>

#include <memory>
#include <utility>
#include <vector>

#include "winui_lite/chrome/command-router.h"
#include "winui_lite/chrome/status-bar.h"
#include "winui_lite/chrome/theme-watcher.h"
#include "winui_lite/chrome/window-base.h"
#include "winui_lite/widgets/tab-strip.h"

#include "in-memory-item-source.h"
#include "noop-adapters.h"

namespace {

constexpr WORD kAccelDemoRename = 100;

class DemoWindow final : public fast_explorer::ui::WindowBase {
 public:
  bool create(HINSTANCE instance);

 protected:
  LRESULT handleMessage(HWND hwnd, UINT msg,
                        WPARAM wParam, LPARAM lParam) override;

 private:
  LRESULT onCreate(HWND hwnd);
  void layoutChildren(HWND hwnd);
  void handleDispInfo(NMLVDISPINFOW* disp);

  HINSTANCE instance_ = nullptr;
  HWND listView_ = nullptr;
  fast_explorer::ui::StatusBar statusBar_;
  fast_explorer::ui::CommandRouter router_;
  winui_lite_demo::InMemoryItemSource source_;
  std::unique_ptr<fast_explorer::ui::TabStrip> tabStrip_;
  std::vector<fast_explorer::ui::TabModel> tabModels_;
};

bool DemoWindow::create(HINSTANCE instance) {
  instance_ = instance;
  ClassSpec cs;
  cs.className = L"winui_lite.DemoWindow";
  cs.icon = LoadIconW(nullptr, IDI_APPLICATION);
  cs.iconSmall = LoadIconW(nullptr, IDI_APPLICATION);
  WindowSpec ws;
  ws.title = L"winui_lite demo";
  ws.width = 720;
  ws.height = 480;
  HWND hwnd = createWindow(instance, cs, ws);
  if (hwnd == nullptr) return false;
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  return true;
}

LRESULT DemoWindow::handleMessage(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: return onCreate(hwnd);
    case WM_SIZE: {
      statusBar_.forwardSize();
      layoutChildren(hwnd);
      return 0;
    }
    case WM_DESTROY: {
      PostQuitMessage(0);
      return 0;
    }
    case WM_NOTIFY: {
      auto* hdr = reinterpret_cast<NMHDR*>(lParam);
      if (hdr != nullptr && hdr->hwndFrom == listView_ &&
          hdr->code == LVN_GETDISPINFOW) {
        handleDispInfo(reinterpret_cast<NMLVDISPINFOW*>(lParam));
        return 0;
      }
      break;
    }
    case WM_COMMAND: {
      if (HIWORD(wParam) == 1 && router_.dispatch(LOWORD(wParam))) {
        return 0;
      }
      break;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT DemoWindow::onCreate(HWND hwnd) {
  // Side-effect ports the chrome doesn't actually call yet — the
  // demo holds them as locals so the wiring is visible at the call
  // site. They go away with the window.
  static winui_lite_demo::NoopClipboard clipboard;
  static winui_lite_demo::NoopDragDrop dragDrop;
  static winui_lite_demo::NoopChangeNotifier changeNotifier;
  static winui_lite_demo::NoopContextMenu contextMenu;
  static winui_lite_demo::NoopItemActivator activator;
  static winui_lite_demo::MemorySettingsStore settings;
  (void)clipboard; (void)dragDrop; (void)changeNotifier;
  (void)contextMenu; (void)activator;
  (void)settings.load();

  // Demo command: F2 maps to a message box so users can see the
  // CommandRouter mechanism running on top of pure winui_lite chrome.
  router_.registerCommand(kAccelDemoRename, [hwnd] {
    MessageBoxW(hwnd, L"Rename action would run here.",
                L"winui_lite demo", MB_OK | MB_ICONINFORMATION);
  });

  listView_ = CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA |
          LVS_SHAREIMAGELISTS,
      0, 0, 0, 0, hwnd, nullptr, instance_, nullptr);
  if (listView_ == nullptr) return -1;
  ListView_SetExtendedListViewStyle(
      listView_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

  struct ColumnSpec {
    const wchar_t* title;
    int width;
  };
  constexpr ColumnSpec cols[] = {
      {L"Name", 220},
      {L"Size", 100},
      {L"Modified", 160},
      {L"Type", 120},
  };
  for (int i = 0; i < static_cast<int>(std::size(cols)); ++i) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.cx = cols[i].width;
    c.pszText = const_cast<LPWSTR>(cols[i].title);
    ListView_InsertColumn(listView_, i, &c);
  }
  ListView_SetItemCountEx(listView_,
                          static_cast<int>(source_.count()), 0);

  statusBar_.create(hwnd, instance_);
  statusBar_.applySinglePart();
  std::wstring statusText = L"winui_lite demo — " +
                            std::to_wstring(source_.count()) +
                            L" fake items";
  statusBar_.setText(0, statusText.c_str());

  // TabStrip demo — three in-memory tabs, no shell linkage.
  tabModels_.push_back({L"Home", true});
  tabModels_.push_back({L"Downloads", true});
  tabModels_.push_back({L"Demo", true});

  tabStrip_ = std::make_unique<fast_explorer::ui::TabStrip>(hwnd, 0);

  tabStrip_->onActivate = [this](std::size_t i) {
    tabStrip_->setActive(i);
  };

  tabStrip_->onClose = [this](std::size_t i) {
    if (tabModels_.size() <= 1) {
      // Keep at least one tab.
      MessageBeep(MB_ICONASTERISK);
      return;
    }
    tabModels_.erase(tabModels_.begin() + static_cast<std::ptrdiff_t>(i));
    tabStrip_->setTabs(tabModels_);
    MessageBeep(MB_ICONASTERISK);
  };

  tabStrip_->onNew = [this]() {
    tabModels_.push_back({L"New tab", true});
    tabStrip_->setTabs(tabModels_);
    tabStrip_->setActive(tabModels_.size() - 1);
  };

  tabStrip_->onReorder = [this](std::size_t from, std::size_t to) {
    auto item = std::move(tabModels_[from]);
    tabModels_.erase(tabModels_.begin() + static_cast<std::ptrdiff_t>(from));
    tabModels_.insert(tabModels_.begin() + static_cast<std::ptrdiff_t>(to),
                      std::move(item));
    tabStrip_->setTabs(tabModels_);
    tabStrip_->setActive(to);
  };

  tabStrip_->setTabs(tabModels_);
  tabStrip_->setActive(0);

  layoutChildren(hwnd);
  return 0;
}

void DemoWindow::layoutChildren(HWND hwnd) {
  RECT client{};
  GetClientRect(hwnd, &client);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;
  const int statusH = statusBar_.height();

  const int stripH = tabStrip_ ? tabStrip_->preferredHeight() : 0;

  if (tabStrip_ != nullptr) {
    SetWindowPos(tabStrip_->handle(), nullptr,
                 0, 0, clientW, stripH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  if (listView_ != nullptr) {
    SetWindowPos(listView_, nullptr,
                 0, stripH,
                 clientW, clientH - stripH - statusH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void DemoWindow::handleDispInfo(NMLVDISPINFOW* disp) {
  if (disp == nullptr || (disp->item.mask & LVIF_TEXT) == 0) return;
  using fast_explorer::ui::ports::ItemField;
  const auto id = source_.idAt(static_cast<std::size_t>(disp->item.iItem));
  ItemField field = ItemField::Name;
  switch (disp->item.iSubItem) {
    case 0: field = ItemField::Name;         break;
    case 1: field = ItemField::SizeText;     break;
    case 2: field = ItemField::ModifiedText; break;
    case 3: field = ItemField::TypeText;     break;
    default: return;
  }
  const std::wstring text = source_.textFor(id, field);
  if (disp->item.pszText == nullptr || disp->item.cchTextMax <= 0) return;
  const size_t cap = static_cast<size_t>(disp->item.cchTextMax) - 1;
  const size_t n = (text.size() < cap) ? text.size() : cap;
  if (n > 0) {
    std::wmemcpy(disp->item.pszText, text.data(), n);
  }
  disp->item.pszText[n] = L'\0';
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  // Allow the dark-mode tinting in StatusBar to take effect when the
  // user has the system theme set to dark. Chrome's theme-watcher
  // also drives this for FastExplorer in production.
  fast_explorer::ui::enableProcessDarkMode();

  DemoWindow window;
  if (!window.create(instance)) {
    MessageBoxW(nullptr, L"DemoWindow::create failed",
                L"winui_lite demo", MB_OK | MB_ICONERROR);
    return 1;
  }

  ACCEL accels[] = {
      {static_cast<BYTE>(FVIRTKEY), VK_F2, kAccelDemoRename},
  };
  HACCEL hAccel = CreateAcceleratorTableW(
      accels, static_cast<int>(std::size(accels)));

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (hAccel != nullptr &&
        TranslateAcceleratorW(window.handle(), hAccel, &msg)) {
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (hAccel != nullptr) DestroyAcceleratorTable(hAccel);
  return static_cast<int>(msg.wParam);
}
