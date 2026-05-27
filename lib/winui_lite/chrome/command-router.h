// command-router.h — WM_COMMAND id → handler dispatch table.
//
// MainWindow's onCommand is a 600-line switch that decodes WM_COMMAND
// in three layers: packed (button+pane) toolbar buttons, packed menu
// items, and bare-id accelerators. Step 5 lays down the dispatch
// mechanism but leaves the case migration for step 12 — only a single
// accelerator is registered today as proof the wiring works.
//
// The router is intentionally tiny: a hash table keyed by WORD. It
// does not understand packing — the caller is responsible for passing
// an already-unpacked id when relevant. Handlers receive no arguments
// and rely on captured state (typically `this`).

#pragma once

#include <windows.h>

#include <cstddef>
#include <functional>
#include <unordered_map>

namespace fast_explorer::ui {

class CommandRouter {
 public:
  using Handler = std::function<void()>;

  // Registers a single command id. Overwrites any prior registration
  // for the same id so adding a new handler at runtime cannot leak
  // the old one.
  void registerCommand(WORD id, Handler handler);

  // Invokes the registered handler and returns true if `id` is known.
  // Returns false without side effects when no handler is registered;
  // the caller's own switch then runs as it did before.
  bool dispatch(WORD id) const;

  // Removes a previously registered id. Mainly for tests — production
  // wiring registers once at create() time and never unregisters.
  void unregister(WORD id);

  std::size_t size() const noexcept { return by_id_.size(); }

 private:
  std::unordered_map<WORD, Handler> by_id_;
};

}  // namespace fast_explorer::ui
