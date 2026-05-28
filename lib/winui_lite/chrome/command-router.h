// command-router.h — WM_COMMAND id → handler dispatch table.
//
// Two parallel storages, one dispatch path:
//   * by_id_   — exact WORD id. Used for accelerators (HIWORD == 1
//                in WM_COMMAND) and any non-packed menu IDs (e.g.
//                the explorer's group-by submenu in the 0x8000 range).
//   * packed_  — button id (already unpacked from cmd-packing). Used
//                for toolbar / pane-scoped menu items where the
//                LOWORD carries `(buttonId << kPaneIdxBits) | paneIdx`.
//
// dispatch() tries by_id_ first then falls back to unpackButton(id)
// against packed_. Existing id ranges keep the two storages disjoint
// (accels are 100..0x7FFF, packed button ids decode above
// kPaneIdxMask), but registering the same value in both storages is
// undefined and the host should not rely on the search order.

#pragma once

#include <windows.h>

#include <cstddef>
#include <functional>
#include <unordered_map>

namespace fast_explorer::ui {

class CommandRouter {
 public:
  using Handler = std::function<void()>;
  using PackedHandler = std::function<void(std::size_t paneIdx)>;

  // Registers a single command id. Overwrites any prior registration
  // for the same id so adding a new handler at runtime cannot leak
  // the old one.
  void registerCommand(WORD id, Handler handler);

  // Registers a packed-command button id. `buttonId` is the value
  // BEFORE packCmd shifts it (e.g. kTbBack, kMenuOpenExplorer).
  // Handler receives the pane index unpacked from the dispatched
  // LOWORD(wParam) at dispatch time.
  void registerPackedCommand(WORD buttonId, PackedHandler handler);

  // Invokes the registered handler and returns true if `id` resolves
  // to either storage. Returns false without side effects when
  // nothing is registered; the caller's own switch then runs as it
  // did before.
  bool dispatch(WORD id) const;

  // Removes a previously registered id from either storage. Mainly
  // for tests — production wiring registers once at create() time
  // and never unregisters.
  void unregister(WORD id);
  void unregisterPacked(WORD buttonId);

  std::size_t size() const noexcept {
    return by_id_.size() + packed_.size();
  }

 private:
  std::unordered_map<WORD, Handler> by_id_;
  std::unordered_map<WORD, PackedHandler> packed_;
};

}  // namespace fast_explorer::ui
