#include "HabitifySettingsActivity.h"

#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <I18n.h>
#include <Logging.h>

#include <memory>
#include <string>

#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"

namespace {
constexpr int ROW_NICKNAME = 0;
constexpr int ROW_KEY = 1;
constexpr int ROW_HIDE_COMPLETED = 2;
constexpr int ROW_CLEAR = 3;
constexpr int ROW_HINT = 4;
}  // namespace

void HabitifySettingsActivity::onEnter() {
  Activity::onEnter();
  HABITIFY_STORE.loadFromFile();
  selectedIndex = 0;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

void HabitifySettingsActivity::onExit() { Activity::onExit(); }

void HabitifySettingsActivity::handleSelection() {
  if (selectedIndex == ROW_NICKNAME) {
    size_t size = 0;
    char* field = homeAppOrder::nicknameField(homeAppOrder::AppId::Habits, size);
    editSettingsText(tr(STR_NICKNAME_ENTER), field, size);
    return;
  }

  if (selectedIndex == ROW_KEY) {
    // Entered as a password: the key is a long-lived credential, and it is only
    // ever pasted or typed once.
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_HABITIFY_ENTER_KEY),
                                                                   HABITIFY_STORE.getApiKey(),
                                                                   HabitifyStore::MAX_KEY_LEN, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             HABITIFY_STORE.setApiKey(std::get<KeyboardResult>(result.data).text);
                             HABITIFY_STORE.saveToFile();
                             LOG_DBG("HBS", "API key %s", HABITIFY_STORE.hasApiKey() ? "set" : "cleared");
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == ROW_HIDE_COMPLETED) {
    HABITIFY_STORE.setHideCompleted(!HABITIFY_STORE.getHideCompleted());
    HABITIFY_STORE.saveToFile();
    requestUpdate(true);
    return;
  }

  if (selectedIndex == ROW_CLEAR) {
    // Clearing the credential drops the cached journal with it: it came from an
    // account this device can no longer reach, so leaving it would show habits
    // that can never update - and unpushed progress that can never be sent.
    HABITIFY_STORE.clearApiKey();
    HABITIFY_STORE.saveToFile();
    HABITIFY_HABITS.clear();
    HABITIFY_HABITS.saveToFile();
    LOG_DBG("HBS", "API key cleared");
    requestUpdate(true);
  }
  // ROW_HINT is a footnote, not an action.
}

void HabitifySettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  switch (handleListTouch(selectedIndex, MENU_ITEMS, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(contentHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, MENU_ITEMS, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, MENU_ITEMS, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEMS);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEMS);
    requestUpdate();
  });
}

void HabitifySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HABITIFY), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  // Live values; the key itself is never shown, only whether one is stored.
  const std::string keyValue = HABITIFY_STORE.hasApiKey() ? std::string("******") : std::string(tr(STR_NOT_SET));
  const std::string hideValue = std::string(HABITIFY_STORE.getHideCompleted() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case ROW_NICKNAME:
            return std::string(tr(STR_NICKNAME));
          case ROW_KEY:
            return std::string(tr(STR_HABITIFY_API_KEY));
          case ROW_HIDE_COMPLETED:
            return std::string(tr(STR_HABITIFY_HIDE_COMPLETED));
          case ROW_CLEAR:
            return std::string(tr(STR_CLEAR_BUTTON));
          default:
            return std::string(tr(STR_ORGANIZER_HOLD_TO_SYNC));
        }
      },
      nullptr, nullptr,
      [&keyValue, &hideValue](int index) -> std::string {
        if (index == ROW_NICKNAME) return std::string(homeAppOrder::displayName(homeAppOrder::AppId::Habits));
        if (index == ROW_KEY) return keyValue;
        if (index == ROW_HIDE_COMPLETED) return hideValue;
        return std::string("");
      },
      false, [](int index) -> bool { return index == ROW_HINT; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
