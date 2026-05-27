// item-dispatcher.h — "give me display text + icon for this item" port.
//
// Driven by LVN_GETDISPINFO under LVS_OWNERDATA. The list-view fires
// one call per (visible row, column) pair, so dispatchers must be
// cache-friendly. Adapters that fetch from the shell typically do
// the heavy lookup on a worker thread and answer chrome out of a
// memory-resident store.
//
// Why a separate port from ItemSource:
//   * Lets one source feed several display projections (e.g. a "name
//     only" preview pane alongside the main detail view).
//   * Lets the demo example provide a 5-line dispatcher that just
//     stringifies its in-memory items without re-implementing the
//     enumeration plumbing.

#pragma once

#include <string>

#include "winui_lite/ports/item-source.h"

namespace fast_explorer::ui::ports {

// Display fields produced for a single row. New values can be
// appended at the end — adapters that don't understand a newer
// field should return an empty string so the list-view falls back
// to a blank cell rather than failing the dispinfo call.
enum class ItemField {
  Name,
  SizeText,
  ModifiedText,
  TypeText,
};

class ItemDispatcher {
 public:
  virtual ~ItemDispatcher() = default;

  // Text for one (item, field) pair. Returned by value so chrome
  // may copy into the dispinfo buffer without lifetime concerns.
  // Empty string for an unknown id or unsupported field.
  virtual std::wstring textFor(ItemId id, ItemField field) const = 0;

  // System imagelist index for `id`'s icon, or -1 when the item
  // has no icon (or the adapter has none to offer). Chrome forwards
  // the value into LVIF_IMAGE as-is.
  virtual int iconIndexFor(ItemId id) const = 0;
};

}  // namespace fast_explorer::ui::ports
