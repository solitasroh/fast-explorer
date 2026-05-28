// noop-adapters.h — stub implementations of the side-effect ports
// (clipboard, drag-drop, change-notify, context-menu, activator,
// settings) so the demo can register a full port surface without
// pulling in shell wiring.

#pragma once

#include "winui_lite/ports/change-notifier.h"
#include "winui_lite/ports/clipboard-backend.h"
#include "winui_lite/ports/context-menu.h"
#include "winui_lite/ports/drag-drop-backend.h"
#include "winui_lite/ports/item-activator.h"
#include "winui_lite/ports/settings-store.h"

namespace winui_lite_demo {

class NoopClipboard final
    : public fast_explorer::ui::ports::ClipboardBackend {
 public:
  bool copyItems(const std::vector<fast_explorer::ui::ports::ItemId>&,
                 bool) override {
    return false;
  }
  fast_explorer::ui::ports::PasteOutcome pasteInto(
      const std::wstring&) override {
    return fast_explorer::ui::ports::PasteOutcome::NoData;
  }
};

class NoopDragDrop final
    : public fast_explorer::ui::ports::DragDropBackend {
 public:
  bool beginDrag(
      const std::vector<fast_explorer::ui::ports::ItemId>&) override {
    return false;
  }
};

class NoopChangeNotifier final
    : public fast_explorer::ui::ports::ChangeNotifier {
 public:
  bool watch(const std::wstring&) override { return true; }
  void stop() override {}
};

class NoopContextMenu final
    : public fast_explorer::ui::ports::ContextMenu {
 public:
  void show(const std::vector<fast_explorer::ui::ports::ItemId>&,
            POINT) override {}
};

class NoopItemActivator final
    : public fast_explorer::ui::ports::ItemActivator {
 public:
  fast_explorer::ui::ports::ActivationResult activate(
      fast_explorer::ui::ports::ItemId) override {
    return {};
  }
};

class MemorySettingsStore final
    : public fast_explorer::ui::ports::SettingsStore {
 public:
  bool load() override { return false; }   // nothing persistent
  bool save() override { return true; }    // pretend we saved
};

}  // namespace winui_lite_demo
