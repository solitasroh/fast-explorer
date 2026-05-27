// item-source.h — "give me the items at this location" port.
//
// First of the three port interfaces that let chrome operate without
// knowing what it's listing. An ItemSource:
//   * accepts a location string (opaque to chrome — adapters decide
//     whether it's a filesystem path, a registry key, a memory id, …)
//   * advertises a current count for owner-data list-view sizing
//   * hands out opaque ItemIds for the rows the chrome wants to query
//
// Whether enumeration is sync or async, and how progress is reported,
// is the adapter's choice. Chrome polls count() / idAt() and trusts
// the adapter to invalidate the list-view when its content changes.
//
// What is intentionally NOT here:
//   * Per-item display text — that belongs to ItemDispatcher so the
//     two ports can be replaced independently (e.g. a single source
//     with two display projections).
//   * Sort / filter / group — those are chrome concerns that operate
//     on already-fetched ids; the source just exposes order it has.
//   * Selection state — owned by the list-view widget, not the data.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fast_explorer::ui::ports {

// Opaque identifier for an item within an ItemSource's current
// location. Adapters define the meaning — chrome only echoes
// values back through other port calls. 0 is reserved for "no
// item" so dispatcher / activator implementations can early-out
// without ambiguity.
using ItemId = std::uint64_t;

inline constexpr ItemId kInvalidItemId = 0;

class ItemSource {
 public:
  virtual ~ItemSource() = default;

  // Switch the source to enumerate items at `location`. Replaces
  // any previous content; sources keep at most one active location.
  // Sync or async is up to the adapter.
  virtual void navigateTo(const std::wstring& location) = 0;

  // The location currently being enumerated. Empty before any
  // navigateTo() call. Returned by reference so chrome can avoid
  // copying the path on every check.
  virtual const std::wstring& currentLocation() const = 0;

  // Number of items currently known at the active location. Feeds
  // ListView_SetItemCountEx for LVS_OWNERDATA virtualization.
  virtual std::size_t count() const = 0;

  // Stable id for the item at `index`. Returns kInvalidItemId when
  // `index >= count()` at call time (race with concurrent enum is
  // expected for async sources). Callers should not stash the id
  // long-term — adapters may renumber on navigateTo().
  virtual ItemId idAt(std::size_t index) const = 0;
};

}  // namespace fast_explorer::ui::ports
