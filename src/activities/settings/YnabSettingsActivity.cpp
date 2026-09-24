#include "YnabSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <YnabAccountCache.h>
#include <YnabCategoryCache.h>
#include <YnabStore.h>

#include <cstdio>
#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "activities/settings/YnabAccountsActivity.h"
#include "activities/settings/YnabCategoryPickerActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"

namespace {
constexpr int ROW_NICKNAME = 0;
constexpr int ROW_TOKEN = 1;
constexpr int ROW_BUDGET_ID = 2;
constexpr int ROW_CATEGORIES = 3;
constexpr int ROW_ACCOUNTS = 4;
constexpr int ROW_CLEAR = 5;
constexpr int ROW_HINT = 6;
}  // namespace

void YnabSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  state = State::MENU;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

void YnabSettingsActivity::onExit() { Activity::onExit(); }

void YnabSettingsActivity::loop() {
  auto activateSelected = [this] { handleSelection(); };

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
    if (state == State::FAILED) {
      RenderLock lock(*this);
      state = State::MENU;
      statusMessage = nullptr;
      requestUpdate(true);
      return;
    }
    activateSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  if (state != State::MENU) return;

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

void YnabSettingsActivity::handleSelection() {
  if (selectedIndex == ROW_NICKNAME) {
    size_t size = 0;
    char* field = homeAppOrder::nicknameField(homeAppOrder::AppId::Budget, size);
    editSettingsText(tr(STR_NICKNAME_ENTER), field, size);
    return;
  }

  if (selectedIndex == ROW_TOKEN) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_YNAB_ENTER_TOKEN),
                                                                   YNAB_STORE.getAccessToken(),
                                                                   YnabStore::MAX_TOKEN_LEN, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             YNAB_STORE.setAccessToken(std::get<KeyboardResult>(result.data).text);
                             YNAB_STORE.saveToFile();
                             LOG_DBG("YNS", "Access token %s", YNAB_STORE.hasToken() ? "set" : "cleared");
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == ROW_BUDGET_ID) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_YNAB_ENTER_BUDGET_ID),
                                                YNAB_STORE.getBudgetId(), YnabStore::MAX_BUDGET_ID_LEN),
        [this](const ActivityResult& result) {
          if (result.isCancelled) return;
          YNAB_STORE.setBudgetId(std::get<KeyboardResult>(result.data).text);
          YNAB_STORE.saveToFile();
          requestUpdate();
        });
    return;
  }

  if (selectedIndex == ROW_CATEGORIES) {
    if (!YNAB_STORE.isConfigured()) {
      // The picker fetches live, so it has nothing to show until both halves
      // are set; saying so here beats a network error there.
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_YNAB_NEED_TOKEN);
      requestUpdate(true);
      return;
    }
    startActivityForResult(std::make_unique<YnabCategoryPickerActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(true); });
    return;
  }

  if (selectedIndex == ROW_ACCOUNTS) {
    if (!YNAB_STORE.isConfigured()) {
      // Same as the categories row: the screen fetches live, so it has nothing to
      // show until both halves are set.
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_YNAB_NEED_TOKEN);
      requestUpdate(true);
      return;
    }
    startActivityForResult(std::make_unique<YnabAccountsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(true); });
    return;
  }

  if (selectedIndex == ROW_CLEAR) {
    // Clearing the credential drops the cached balances with it: they came from
    // an account this device can no longer reach, so leaving them would show a
    // list that can never update. The budget id survives - it is not a secret,
    // and retyping a UUID by hand is the worst part of this setup.
    YNAB_STORE.clearToken();
    YNAB_STORE.saveToFile();
    YNAB_CATEGORIES.clear();
    YNAB_CATEGORIES.saveToFile();
    // The accounts and their transactions go the same way, and for the same
    // reason: they came from a plan this device can no longer reach.
    YNAB_ACCOUNTS.clear();
    YNAB_ACCOUNTS.saveToFile();
    LOG_DBG("YNS", "Access token cleared");
    requestUpdate(true);
  }
  // ROW_HINT is a footnote, not an action.
}

void YnabSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_YNAB), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else {
    // Live values; the token itself is never shown, only whether one is stored.
    const std::string tokenValue = YNAB_STORE.hasToken() ? std::string("******") : std::string(tr(STR_NOT_SET));
    // The budget id is not a secret, so it is shown rather than masked: it is
    // typed by hand and a transposed character is otherwise invisible. The
    // theme truncates it to the value column's width.
    const std::string budgetValue = YNAB_STORE.hasBudgetId() ? YNAB_STORE.getBudgetId() : std::string(tr(STR_NOT_SET));
    char categoryValue[24];
    snprintf(categoryValue, sizeof(categoryValue), "%zu", YNAB_STORE.getSelectedCategories().size());
    char accountValue[24];
    snprintf(accountValue, sizeof(accountValue), "%zu", YNAB_ACCOUNTS.getAccounts().size());

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
        [](int index) -> std::string {
          switch (index) {
            case ROW_NICKNAME:
              return std::string(tr(STR_NICKNAME));
            case ROW_TOKEN:
              return std::string(tr(STR_YNAB_ACCESS_TOKEN));
            case ROW_BUDGET_ID:
              return std::string(tr(STR_YNAB_BUDGET_ID));
            case ROW_CATEGORIES:
              return std::string(tr(STR_YNAB_CATEGORIES));
            case ROW_ACCOUNTS:
              return std::string(tr(STR_YNAB_ACCOUNTS));
            case ROW_CLEAR:
              return std::string(tr(STR_CLEAR_BUTTON));
            default:
              return std::string(tr(STR_ORGANIZER_HOLD_TO_SYNC));
          }
        },
        nullptr, nullptr,
        [&tokenValue, &budgetValue, &categoryValue, &accountValue](int index) -> std::string {
          if (index == ROW_NICKNAME) return std::string(homeAppOrder::displayName(homeAppOrder::AppId::Budget));
          if (index == ROW_TOKEN) return tokenValue;
          if (index == ROW_BUDGET_ID) return budgetValue;
          if (index == ROW_CATEGORIES) return std::string(categoryValue);
          if (index == ROW_ACCOUNTS) return std::string(accountValue);
          return std::string("");
        },
        false, [](int index) -> bool { return index == ROW_HINT; });
  }

  const char* confirmLabel = state == State::FAILED ? tr(STR_OK_BUTTON) : tr(STR_SELECT);
  const bool navigable = state == State::MENU;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, navigable ? tr(STR_DIR_UP) : "",
                                            navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
