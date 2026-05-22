#include "ui/pane-sort-coordinator.h"

#include <algorithm>
#include <stop_token>
#include <utility>

#include "ui/jthread-utils.h"
#include "ui/messages.h"

namespace fast_explorer::ui {

PaneSortCoordinator::PaneSortCoordinator(
    fast_explorer::core::FileModelStore& store, HWND hostWindow,
    std::size_t paneIndex)
    : store_(store), host_(hostWindow), paneIndex_(paneIndex) {}

PaneSortCoordinator::~PaneSortCoordinator() = default;

SortDispatch PaneSortCoordinator::requestSort(
    fast_explorer::core::SortKey key, bool enumerationActive,
    fast_explorer::core::GroupKey groupBy, uint64_t nowFiletime) {
  using fast_explorer::core::SortDirection;
  if (enumerationActive) {
    return SortDispatch::Rejected;
  }
  groupBy_ = groupBy;
  nowFiletime_ = nowFiletime;
  stopAndJoin(sortWorker_);
  // Any leftover pending order from a prior background sort that
  // never made it through applyPendingSort (e.g. its kWmFeSortComplete
  // is still in the message queue) must be discarded here. Without
  // this, a sync sort followed by the stale dispatched-sort's
  // PostMessage arriving would call applySortedOrder with the wrong
  // permutation and silently overwrite the sync result.
  pendingSortedOrder_.clear();
  pendingSortGen_ = 0;
  const auto count = store_.publishedCount();
  if (count == 0) {
    return SortDispatch::Rejected;
  }
  if (sorted_ && sortSpec_.key == key) {
    sortSpec_.direction =
        (sortSpec_.direction == SortDirection::Ascending)
            ? SortDirection::Descending
            : SortDirection::Ascending;
  } else {
    sortSpec_.key = key;
    sortSpec_.direction = SortDirection::Ascending;
  }
  if (count < sortThresholdRows_) {
    if (groupBy_ == fast_explorer::core::GroupKey::None) {
      // Fast path preserved bit-for-bit for the non-grouped case:
      // delegate to store_.sort which sorts the visibleOrder buffer
      // in place. compareWithGroup would short-circuit to the same
      // result, but going through the store keeps the existing
      // codepath untouched and avoids an extra vector allocation.
      store_.sort(sortSpec_);
    } else {
      std::vector<std::uint32_t> order;
      order.resize(count);
      for (std::uint32_t i = 0; i < count; ++i) {
        order[i] = i;
      }
      const auto spec = sortSpec_;
      const auto gk = groupBy_;
      const auto now = nowFiletime_;
      std::sort(order.begin(), order.end(),
                [this, spec, gk, now](std::uint32_t a, std::uint32_t b) {
                  return fast_explorer::core::compareWithGroup(
                             store_.entryAt(a), store_.entryAt(b), spec,
                             gk, now) < 0;
                });
      store_.applySortedOrder(std::move(order));
    }
    sorted_ = true;
    return SortDispatch::AppliedSync;
  }

  const auto spec = sortSpec_;
  const auto gk = groupBy_;
  const auto now = nowFiletime_;
  const HWND host = host_;
  const auto gen = store_.generation();
  const std::size_t paneIdx = paneIndex_;
  pendingSortGen_ = gen;
  sortWorker_ = std::jthread([this, host, spec, gk, now, count, gen, paneIdx](
                                 std::stop_token tok) {
    std::vector<std::uint32_t> order;
    order.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      order[i] = i;
    }
    if (tok.stop_requested()) {
      return;
    }
    std::sort(order.begin(), order.end(),
              [this, spec, gk, now](std::uint32_t a, std::uint32_t b) {
                return fast_explorer::core::compareWithGroup(
                           store_.entryAt(a), store_.entryAt(b), spec,
                           gk, now) < 0;
              });
    if (tok.stop_requested()) {
      return;
    }
    pendingSortedOrder_ = std::move(order);
    if (host) {
      PostMessageW(host, kWmFeSortComplete,
                   makePaneWParam(paneIdx, gen), 0);
    }
  });
  return SortDispatch::Dispatched;
}

void PaneSortCoordinator::applyPendingSort(std::uint32_t gen) {
  // Wait for natural completion — request_stop here would cancel the
  // worker before it had a chance to fill pendingSortedOrder_, which
  // would then trip the size-mismatch branch and drop the result.
  if (sortWorker_.joinable()) {
    sortWorker_.join();
  }
  if (gen != pendingSortGen_ || gen != store_.generation()) {
    pendingSortedOrder_.clear();
    return;
  }
  if (pendingSortedOrder_.size() != store_.publishedCount()) {
    pendingSortedOrder_.clear();
    return;
  }
  store_.applySortedOrder(std::move(pendingSortedOrder_));
  pendingSortedOrder_.clear();
  sorted_ = true;
}

void PaneSortCoordinator::cancel() noexcept {
  stopAndJoin(sortWorker_);
  pendingSortedOrder_.clear();
  // Drop the "applied to current store" flag, but keep sortSpec_
  // intact so the next enumeration can re-apply the user's chosen
  // column + direction (see reapplyAfterEnumeration).
  sorted_ = false;
}

void PaneSortCoordinator::reapplyAfterEnumeration() {
  if (sortSpec_.key == fast_explorer::core::SortKey::None) {
    return;
  }
  if (store_.publishedCount() == 0) {
    return;
  }
  store_.sort(sortSpec_);
  sorted_ = true;
}

}  // namespace fast_explorer::ui
