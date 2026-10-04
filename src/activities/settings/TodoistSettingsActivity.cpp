#include "TodoistSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistStore.h>

#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"

namespace {
constexpr int ROW_NICKNAME = 0;
constexpr int ROW_TOKEN = 1;
constexpr int ROW_FILTER_NAME = 2;
constexpr int ROW_FILTER = 3;
constexpr int ROW_FILTER2_NAME = 4;
constexpr int ROW_FILTER2 = 5;
constexpr int ROW_HINT = 6;
}  // namespace

void TodoistSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

void TodoistSettingsActivity::onExit() { Activity::onExit(); }

void TodoistSettingsActivity::loop() {
  auto activateSelected = [this] { handleSelection(); };

  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) {
    finish();
    return;
  }

  // This screen's own activation trigger is the press, not the release (see
  // below), so the guard mirrors that: cleared on the release of whatever was
  // already down at entry, rather than on the next press.
  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) return;
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  switch (handleListTouch(selectedIndex, MENU_ITEMS, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      activateSelected();
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

void TodoistSettingsActivity::handleSelection() {
  if (selectedIndex == ROW_NICKNAME) {
    size_t size = 0;
    char* field = homeAppOrder::nicknameField(homeAppOrder::AppId::Tasks, size);
    editSettingsText(tr(STR_NICKNAME_ENTER), field, size);
    return;
  }

  if (selectedIndex == ROW_TOKEN) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODOIST_ENTER_TOKEN),
                                                                   TODOIST_STORE.getToken(),
                                                                   TodoistStore::MAX_TOKEN_LEN, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             TODOIST_STORE.setToken(kb.text);
                             TODOIST_STORE.saveToFile();
                             LOG_DBG("TDS", "API token %s", TODOIST_STORE.hasToken() ? "set" : "cleared");
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == ROW_FILTER_NAME || selectedIndex == ROW_FILTER2_NAME) {
    const uint8_t which = selectedIndex == ROW_FILTER2_NAME ? 1 : 0;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                               renderer, mappedInput, tr(STR_TODOIST_ENTER_FILTER_NAME),
                               TODOIST_STORE.getFilterName(which), TodoistStore::MAX_FILTER_NAME_CHARS),
                           [this, which](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             TODOIST_STORE.setFilterName(which, std::get<KeyboardResult>(result.data).text);
                             TODOIST_STORE.saveToFile();
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == ROW_FILTER || selectedIndex == ROW_FILTER2) {
    const bool second = selectedIndex == ROW_FILTER2;
    // Plain text, not a password: a filter is not a secret, and it is typed by
    // hand so a mistyped one has to be visible to be fixable.
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODOIST_ENTER_FILTER),
                                                second ? TODOIST_STORE.getFilter2() : TODOIST_STORE.getFilter(),
                                                TodoistStore::MAX_FILTER_LEN),
        [this, second](const ActivityResult& result) {
          if (result.isCancelled) return;
          const std::string& text = std::get<KeyboardResult>(result.data).text;
          if (second) {
            TODOIST_STORE.setFilter2(text);
          } else {
            TODOIST_STORE.setFilter(text);
          }
          TODOIST_STORE.saveToFile();
          // Only the next sync acts on this: the cached list is whatever the old
          // filter matched, and clearing it here would leave the screen blank
          // until the user found their way to a sync.
          LOG_DBG("TDS", "Filter %d set to %s", second ? 2 : 1,
                  (second ? TODOIST_STORE.getFilter2() : TODOIST_STORE.getFilter()).c_str());
          requestUpdate();
        });
    return;
  }

  // ROW_HINT is a footnote, not an action.
}

void TodoistSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TODOIST));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  // Live values; the token itself is never shown, only whether one is stored.
  const std::string tokenValue =
      TODOIST_STORE.hasToken() ? std::string("******") : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case ROW_NICKNAME:
            return std::string(I18n::getInstance().get(StrId::STR_NICKNAME));
          case ROW_TOKEN:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_API_TOKEN));
          case ROW_FILTER_NAME:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_FILTER_NAME)) + " 1";
          case ROW_FILTER:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_FILTER)) + " 1";
          case ROW_FILTER2_NAME:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_FILTER_NAME)) + " 2";
          case ROW_FILTER2:
            return std::string(I18n::getInstance().get(StrId::STR_TODOIST_FILTER)) + " 2";
          default:
            return std::string(I18n::getInstance().get(StrId::STR_ORGANIZER_HOLD_TO_SYNC));
        }
      },
      nullptr, nullptr,
      [&tokenValue](int index) -> std::string {
        if (index == ROW_NICKNAME) return std::string(homeAppOrder::displayName(homeAppOrder::AppId::Tasks));
        if (index == ROW_TOKEN) return tokenValue;
        // Shown rather than masked, and in full: the theme truncates it to the
        // value column, which is the only hint that a long filter is longer than
        // it looks.
        if (index == ROW_FILTER_NAME || index == ROW_FILTER2_NAME) {
          const std::string& name = TODOIST_STORE.getFilterName(index == ROW_FILTER2_NAME ? 1 : 0);
          return name.empty() ? std::string(I18n::getInstance().get(StrId::STR_NOT_SET)) : name;
        }
        if (index == ROW_FILTER) return TODOIST_STORE.getFilter();
        if (index == ROW_FILTER2) return TODOIST_STORE.getFilter2();
        return std::string("");
      },
      false, [](int index) -> bool { return index == ROW_HINT; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
