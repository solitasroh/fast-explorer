// main-window-commands.cpp — the WM_COMMAND wiring for MainWindow.
//
// Holds MainWindow::registerAccelHandlers in its own TU so the bulky
// router-registration block does not balloon main-window.cpp. The
// definition is still a member function — it accesses MainWindow
// private state through `this` — so no friend declarations or
// public accessors are needed. Splitting only the .cpp is a pure
// code-locality move.

#include "ui/main-window.h"

#include <windows.h>

#include <winsparkle.h>

#include "core/file-grouping.h"
#include "ui/address-bar-popup.h"
#include "ui/label-edit-controller.h"
#include "ui/messages.h"
#include "ui/pane-controller.h"
#include "ui/search-popup.h"
#include "ui/shell-actions.h"
#include "winui_lite/chrome/command-router.h"
#include "winui_lite/chrome/layout-preset.h"
#include "winui_lite/chrome/pane-manager.h"

namespace fast_explorer::ui {

void MainWindow::registerAccelHandlers() {
  // Wired against accelRouter_ at create() time. Two parallel
  // registrations:
  //   * registerCommand(id, ...)        for accelerators (HIWORD == 1)
  //                                     + the host's group-by submenu
  //                                     (non-packed 0x8000+ ids)
  //   * registerPackedCommand(btn, ...) for toolbar buttons + per-pane
  //                                     hamburger menu items. Handler
  //                                     receives the unpacked pane idx.
  // onCommand fires a single dispatch(LOWORD(wParam)) call per WM_
  // COMMAND for both paths — see the dispatch() comment in the router
  // for the by_id_ / packed_ resolution order.
  using fast_explorer::core::LayoutPreset;

  auto activeIndex = [this]() noexcept -> std::size_t {
    return paneManager_ ? paneManager_->activeIndex() : 0;
  };

  accelRouter_.registerCommand(kAccelFocusAddress, [this, activeIndex] {
    const auto idx = activeIndex();
    if (idx < addressBars_.size() && addressBars_[idx]) {
      HWND bar = addressBars_[idx];
      SetFocus(bar);
      SendMessageW(bar, EM_SETSEL, 0, -1);
    }
  });

  accelRouter_.registerCommand(kAccelDelete, [this] {
    deleteFocusedItem();
  });

  accelRouter_.registerCommand(kAccelRename, [this] {
    if (auto* le = activeLabelEdit()) le->beginRenameFocused();
  });

  accelRouter_.registerCommand(kAccelCreateFolder, [this] {
    if (auto* le = activeLabelEdit()) le->beginCreateSubfolder();
  });

  accelRouter_.registerCommand(kAccelCopyPath, [this, activeIndex] {
    const auto idx = activeIndex();
    if (paneManager_ && idx < paneManager_->count()) {
      copyPathToClipboard(paneManager_->at(idx).currentPath(), hwnd_);
    }
  });

  accelRouter_.registerCommand(kAccelProperties, [this, activeIndex] {
    const auto idx = activeIndex();
    if (paneManager_ && idx < paneManager_->count()) {
      showFolderProperties(paneManager_->at(idx).currentPath(), hwnd_);
    }
  });

  accelRouter_.registerCommand(kAccelToolMenu, [this, activeIndex] {
    showToolMenuForPane(activeIndex());
  });

  accelRouter_.registerCommand(kAccelLayoutSingle, [this] {
    enterLayout(LayoutPreset::Single);
  });

  accelRouter_.registerCommand(kAccelLayoutTri, [this] {
    using fast_explorer::core::nextPresetInCycle;
    enterLayout(nextPresetInCycle(preset_, 3));
  });

  accelRouter_.registerCommand(kAccelLayoutQuad, [this] {
    using fast_explorer::core::nextPresetInCycle;
    enterLayout(nextPresetInCycle(preset_, 4));
  });

  accelRouter_.registerCommand(kAccelFilter, [this] {
    if (searchPopup_ && paneManager_) {
      searchPopup_->show(paneManager_->activeIndex());
    }
  });

  accelRouter_.registerCommand(kAccelPaneSwitch, [this] {
    if (paneManager_ && paneManager_->count() > 1) {
      const std::size_t next =
          (paneManager_->activeIndex() + 1) % paneManager_->count();
      setActivePane(next);
    }
  });

  // -----------------------------------------------------------------
  // Group-by submenu — raw (unpacked) ids in the 0x8000 range, fired
  // from the empty-area context menu's '분류 방법' submenu. Active
  // pane is the target since the submenu is shown over the active
  // list-view.
  // -----------------------------------------------------------------
  auto applyGroupBy = [this](fast_explorer::core::GroupKey gk) {
    if (!paneManager_) return;
    const std::size_t idx = paneManager_->activeIndex();
    if (idx >= paneManager_->count()) return;
    // setGroupBy → requestSort. Sync sorts (under the worker threshold)
    // need an explicit finalizeSortApply so the group-header visual
    // lands immediately; the async path posts kWmFeSortComplete which
    // routes through onSortComplete → finalizeSortApply on its own.
    const auto disp = paneManager_->at(idx).setGroupBy(gk);
    if (disp == fast_explorer::ui::SortDispatch::AppliedSync) {
      finalizeSortApply(idx);
    }
  };
  accelRouter_.registerCommand(kMenuGroupByNone, [applyGroupBy] {
    applyGroupBy(fast_explorer::core::GroupKey::None);
  });
  accelRouter_.registerCommand(kMenuGroupByName, [applyGroupBy] {
    applyGroupBy(fast_explorer::core::GroupKey::Name);
  });
  accelRouter_.registerCommand(kMenuGroupByModified, [applyGroupBy] {
    applyGroupBy(fast_explorer::core::GroupKey::Modified);
  });
  accelRouter_.registerCommand(kMenuGroupByType, [applyGroupBy] {
    applyGroupBy(fast_explorer::core::GroupKey::Type);
  });

  // -----------------------------------------------------------------
  // Toolbar buttons + hamburger menu items — all packed (button id +
  // pane idx). Each handler receives the unpacked pane from the
  // router so the menu item / toolbar click acts on the pane that
  // owns it, not necessarily the active pane.
  // -----------------------------------------------------------------
  accelRouter_.registerPackedCommand(
      kTbHamburger, [this](std::size_t pane) { showToolMenuForPane(pane); });

  accelRouter_.registerPackedCommand(
      kTbAddressDropdown, [this](std::size_t pane) {
        if (!addressBarPopup_ || !paneManager_ ||
            pane >= paneManager_->count() ||
            pane >= addressBars_.size() ||
            addressBars_[pane] == nullptr) {
          return;
        }
        // Toggle — second click on the same anchor dismisses the
        // popup instead of redundantly showing it. The mouse hook
        // excludes the anchor from auto-hide so this path is the
        // only thing that decides visibility on anchor clicks.
        if (addressBarPopup_->isVisible()) {
          addressBarPopup_->hide();
        } else {
          addressBarPopup_->setActivePane(pane);
          addressBarPopup_->showFor(addressBars_[pane],
                                     paneManager_->at(pane).currentPath());
        }
      });

  // Navigation toolbar group shares the activate-and-refresh pattern.
  auto navAction = [this](std::size_t pane, bool changed) {
    if (paneManager_ && pane < paneManager_->count()) {
      setActivePane(pane);
      if (changed) {
        clearListViewForNavigation(pane);
        syncAddressBar(pane);
      }
      // Reflect the new history boundary immediately so a rapid second
      // click sees the correct enabled state without waiting for the
      // async enum complete.
      updateNavButtonStates(pane);
    }
  };
  accelRouter_.registerPackedCommand(kTbBack, [this, navAction](std::size_t p) {
    if (paneManager_ && p < paneManager_->count()) {
      navAction(p, paneManager_->at(p).back());
    }
  });
  accelRouter_.registerPackedCommand(
      kTbForward, [this, navAction](std::size_t p) {
        if (paneManager_ && p < paneManager_->count()) {
          navAction(p, paneManager_->at(p).forward());
        }
      });
  accelRouter_.registerPackedCommand(kTbUp, [this, navAction](std::size_t p) {
    if (paneManager_ && p < paneManager_->count()) {
      navAction(p, paneManager_->at(p).up());
    }
  });
  accelRouter_.registerPackedCommand(
      kTbRefresh, [this, navAction](std::size_t p) {
        if (paneManager_ && p < paneManager_->count()) {
          navAction(p, paneManager_->at(p).refresh());
        }
      });

  // Hamburger menu items. Most map to a free helper; the toggles
  // (show-hidden / show-extensions / check-updates) keep their own
  // logic inline since the host owns the state.
  accelRouter_.registerPackedCommand(
      kMenuNewFolder, [this](std::size_t pane) {
        if (paneManager_ && pane < paneManager_->count()) {
          setActivePane(pane);
          if (auto* le = activeLabelEdit()) le->beginCreateSubfolder();
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuRefresh, [this](std::size_t pane) {
        if (paneManager_ && pane < paneManager_->count() &&
            paneManager_->at(pane).refresh()) {
          clearListViewForNavigation(pane);
          syncAddressBar(pane);
          updateNavButtonStates(pane);
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuOpenExplorer, [this](std::size_t pane) {
        if (paneManager_ && pane < paneManager_->count()) {
          openInExplorer(paneManager_->at(pane).currentPath());
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuOpenTerminal, [this](std::size_t pane) {
        if (paneManager_ && pane < paneManager_->count()) {
          launchTerminalInFolder(paneManager_->at(pane).currentPath(), hwnd_);
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuCopyPath, [this](std::size_t pane) {
        if (paneManager_ && pane < paneManager_->count()) {
          copyPathToClipboard(paneManager_->at(pane).currentPath(), hwnd_);
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuProperties, [this](std::size_t pane) {
        if (paneManager_ && pane < paneManager_->count()) {
          showFolderProperties(paneManager_->at(pane).currentPath(), hwnd_);
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuShowHidden, [this](std::size_t /*pane*/) {
        // Show-hidden is process-wide, not per-pane — propagate to
        // every live pane, then refresh so the hidden-attribute
        // filter takes effect against the same path immediately. The
        // enumerator snapshots the flag at navigate start, so the
        // order is set → refresh.
        showHidden_ = !showHidden_;
        if (!paneManager_) return;
        for (std::size_t i = 0; i < paneManager_->count(); ++i) {
          paneManager_->at(i).setIncludeHidden(showHidden_);
          if (paneManager_->at(i).refresh()) {
            clearListViewForNavigation(i);
          }
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuShowExt, [this](std::size_t /*pane*/) {
        // Extension display is a pure format change — no re-enum
        // needed (ColumnFormatter reads showExtensions_), just
        // invalidate the visible rows.
        showExtensions_ = !showExtensions_;
        for (std::size_t i = 0; i < listViews_.size(); ++i) {
          if (listViews_[i] != nullptr) {
            InvalidateRect(listViews_[i], nullptr, FALSE);
          }
        }
      });
  accelRouter_.registerPackedCommand(
      kMenuCheckUpdates, [](std::size_t /*pane*/) {
        // _with_ui forces the dialog even if the 24h debounce window
        // hasn't elapsed; the no-ui variant would skip silently.
        win_sparkle_check_update_with_ui();
      });
}

}  // namespace fast_explorer::ui
