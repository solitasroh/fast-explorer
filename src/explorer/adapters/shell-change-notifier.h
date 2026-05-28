// shell-change-notifier.h — explorer-side adapter for ChangeNotifier.
//
// Wraps fast_explorer::core::FsWatcher. The adapter posts a host-
// supplied message to a target HWND on every change event — that is
// how MainWindow's existing fs-change pipeline already shapes things,
// so adopters can drop the adapter into the same coalescing /
// dispatch logic without rewiring.
//
// What this adapter does NOT do today:
//   * Replace PaneController's internal FsWatcher. Each controller
//     still owns its own watcher because change notification is tied
//     to navigation lifetime (start on openFolder, stop on
//     destruction). This adapter exists for OUT-OF-PANE callers that
//     want to watch a location independently — the demo app in
//     step 13 is the first real consumer.

#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

#include "core/fs-watcher.h"
#include "winui_lite/ports/change-notifier.h"

namespace fast_explorer::ui::adapters {

class ShellChangeNotifier final : public ports::ChangeNotifier {
 public:
  // `target` receives `message` (as WPARAM=paneIdx, LPARAM=0) for
  // every coalescable change event. Pass paneIdx=0 for non-pane
  // consumers; the field is forwarded verbatim and ignored if the
  // host does not multiplex panes.
  ShellChangeNotifier(HWND target, UINT message,
                      std::size_t paneIdx = 0) noexcept;
  ~ShellChangeNotifier() override = default;

  ShellChangeNotifier(const ShellChangeNotifier&) = delete;
  ShellChangeNotifier& operator=(const ShellChangeNotifier&) = delete;

  bool watch(const std::wstring& location) override;
  void stop() override;

 private:
  HWND target_;
  UINT message_;
  std::size_t paneIdx_;
  fast_explorer::core::FsWatcher watcher_;
};

}  // namespace fast_explorer::ui::adapters
