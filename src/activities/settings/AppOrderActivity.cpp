#include "AppOrderActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void AppOrderActivity::onEnter() {
  Activity::onEnter();
  homeAppOrder::parse(SETTINGS.homeAppOrder, order);
  selectedIndex = 0;
  holding = false;
  dirty = false;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

void AppOrderActivity::onExit() {
  // Written once on the way out rather than on every move: SD writes are the
  // expensive part and moving one app is several presses.
  if (dirty) {
    char stored[homeAppOrder::ORDER_MAX_LEN];
    homeAppOrder::format(order, stored, sizeof(stored));
    // Guarded by the change check CrossPointSettings expects of its writers.
    if (strncmp(SETTINGS.homeAppOrder, stored, sizeof(SETTINGS.homeAppOrder)) != 0) {
      snprintf(SETTINGS.homeAppOrder, sizeof(SETTINGS.homeAppOrder), "%s", stored);
      SETTINGS.saveToFile();
      LOG_DBG("APPORD", "Home app order saved as %s", stored);
    }
  }
  Activity::onExit();
}

void AppOrderActivity::moveHeldTo(const int target) {
  // Walked one place at a time rather than lifted out and reinserted, so a tap
  // lands on exactly the arrangement holding Up or Down to the same row would.
  while (selectedIndex < target) moveHeld(1);
  while (selectedIndex > target) moveHeld(-1);
}

void AppOrderActivity::moveHeld(const int delta) {
  const int target = selectedIndex + delta;
  // Deliberately not wrapping: an app dragged off the bottom reappearing at the
  // top is disorienting when the list is this short.
  if (target < 0 || target >= homeAppOrder::APP_COUNT) return;
  std::swap(order[selectedIndex], order[target]);
  selectedIndex = target;
  dirty = true;
  requestUpdate(true);
}

void AppOrderActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    if (holding) {
      // Back puts the row down rather than leaving, so there is a way out of
      // holding that is not "commit wherever it happens to be".
      holding = false;
      requestUpdate(true);
      return;
    }
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) {
      // The tail of the press that opened this screen -- see its own comment.
      swallowConfirmRelease = false;
      return;
    }
    holding = !holding;
    requestUpdate(true);
    return;
  }

  const int itemCount = homeAppOrder::APP_COUNT;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  // handleListTouch moves selectedIndex to whatever was touched, which is right
  // when browsing and wrong while holding - there the selection has to stay on
  // the row being carried. So the index is remembered and restored either way,
  // and a tap while holding is read as "carry it to here".
  const int heldFrom = selectedIndex;
  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated: {
      if (!holding) {
        holding = true;
        requestUpdate(true);
        return;
      }
      const int target = selectedIndex;
      selectedIndex = heldFrom;
      moveHeldTo(target);
      holding = false;
      requestUpdate(true);
      return;
    }
    case ListTouchResult::Consumed:
      // Touchdown highlighted a row. While carrying, that would leave the
      // highlight somewhere the held app is not.
      if (holding) selectedIndex = heldFrom;
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this, itemCount] {
    if (holding) {
      moveHeld(1);
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    if (holding) {
      moveHeld(-1);
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void AppOrderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APP_ORDER), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  const int* rows = order;
  const bool held = holding;
  const int selected = selectedIndex;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, homeAppOrder::APP_COUNT, selectedIndex,
      [rows](int index) -> std::string {
        // Numbered, because the point of the screen is the position rather than
        // the name, and the numbers are what the home grid reads left to right.
        char label[48];
        snprintf(label, sizeof(label), "%d.  %s", index + 1,
                 homeAppOrder::displayName(homeAppOrder::appAt(rows[index]).id));
        return std::string(label);
      },
      // No icon column: only Read's (UIIcon::Book) actually has art at this
      // list's icon size, so showing icons here meant Read alone got one and
      // every other app's row looked broken beside it. The position number
      // in the label already identifies each row without one.
      nullptr, nullptr,
      // The held row says so in its value column: the selection highlight alone
      // cannot distinguish "here" from "moving".
      [held, selected](int index) -> std::string {
        return (held && index == selected) ? std::string(tr(STR_APP_ORDER_MOVING)) : std::string("");
      },
      true);

  // Up and Down change meaning while a row is held, so the hints say which.
  const auto labels = mappedInput.mapLabels(holding ? tr(STR_CANCEL) : tr(STR_BACK),
                                            holding ? tr(STR_APP_ORDER_DROP) : tr(STR_APP_ORDER_MOVE), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
