#include "YnabAccountsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <YnabAccountCache.h>
#include <YnabAccountLabel.h>
#include <YnabStore.h>

#include <memory>
#include <string>
#include <utility>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"

void YnabAccountsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  state = State::LOADING;
  requestUpdate();

  // Past this point every path uses WiFi, so onExit() owes a teardown. Free
  // NimBLE's ~55KB init-time heap reservation before WiFi/TLS need their own
  // headroom -- no matching resume(): onExit() below now reboots once
  // wifiActivated is set, and BleNotifyRelay::begin() re-advertises fresh on
  // the next boot. This screen previously never rebooted after using WiFi at
  // all (a pre-existing gap independent of BLE).
  wifiActivated = true;
  BleNotifyRelay::pause();

  if (WiFi.status() == WL_CONNECTED) {
    fetchAccounts();
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             {
                               RenderLock lock(*this);
                               state = State::FAILED;
                               statusMessage = tr(STR_WIFI_CONN_FAILED);
                             }
                             requestUpdate(true);
                             return;
                           }
                           fetchAccounts();
                         });
}

void YnabAccountsActivity::onExit() {
  // Persist once on the way out rather than on every edit: SD writes are the
  // expensive part and the user may relabel several accounts in a row.
  if (dirty) {
    YNAB_STORE.saveToFile();
    LOG_DBG("YAA", "Saved account labels");
  }
  Activity::onExit();

  // Reclaim WiFi/TLS heap fragmentation the same way every other WiFi-using
  // screen does, now that onEnter() pauses BLE first.
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void YnabAccountsActivity::fetchAccounts() {
  freeink::SecureHttpClient http;
  http.setInsecure();
  const YnabClient::Error error = YnabClient::fetchAccounts(http, accounts);
  if (error != YnabClient::OK) {
    LOG_ERR("YAA", "Account list failed: %s", YnabClient::errorString(error));
    RenderLock lock(*this);
    state = State::FAILED;
    switch (error) {
      case YnabClient::AUTH_FAILED:
        statusMessage = tr(STR_YNAB_INVALID_TOKEN);
        break;
      case YnabClient::NOT_FOUND:
        statusMessage = tr(STR_YNAB_BUDGET_NOT_FOUND);
        break;
      case YnabClient::RATE_LIMITED:
        statusMessage = tr(STR_YNAB_RATE_LIMITED);
        break;
      case YnabClient::LOW_MEMORY:
        statusMessage = tr(STR_MEMORY_ERROR);
        break;
      default:
        statusMessage = tr(STR_NETWORK_ERROR);
        break;
    }
    requestUpdate(true);
    return;
  }

  // Cached here rather than on exit: this is the only screen that fetches the
  // list, and the Budget screen needs it to know which tabs exist. A copy,
  // because the rows are still drawn from `accounts`. The cache carries over any
  // transactions it already held for these accounts.
  {
    RenderLock lock(*this);
    std::vector<YnabAccount> forCache = accounts;
    YNAB_ACCOUNTS.setAccounts(std::move(forCache));
    state = State::LIST;
    selectedIndex = 0;
  }
  YNAB_ACCOUNTS.saveToFile();
  requestUpdate(true);
}

void YnabAccountsActivity::editSelectedLabel() {
  if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= accounts.size()) return;
  const std::string accountId = accounts[selectedIndex].id;
  // Opens on whatever is shown, derived first word included, so accepting it
  // unchanged records the guess rather than leaving the row unlabelled.
  const std::string current = ynabAccountLabel(accounts[selectedIndex]);

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_YNAB_ENTER_ACCOUNT_LABEL), current,
                                              YnabStore::MAX_NICKNAME_LEN),
      [this, accountId](const ActivityResult& result) {
        if (result.isCancelled) return;
        YNAB_STORE.setAccountNickname(accountId, std::get<KeyboardResult>(result.data).text);
        dirty = true;
        requestUpdate(true);
      });
}

void YnabAccountsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    finish();
    return;
  }
  if (state == State::LOADING) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (state == State::FAILED) {
      finish();
      return;
    }
    editSelectedLabel();
    return;
  }

  const int itemCount = static_cast<int>(accounts.size());
  if (state != State::LIST || itemCount == 0) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      editSelectedLabel();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(contentHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void YnabAccountsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_YNAB_ACCOUNTS),
                 nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;
  const int itemCount = static_cast<int>(accounts.size());

  if (state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_YNAB_LOADING_ACCOUNTS));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_YNAB_NO_ACCOUNTS));
  } else {
    const auto& rows = accounts;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        // The account's full YNAB name leads: it is what identifies the row,
        // where the label is what the tab will read.
        [&rows](int index) -> std::string { return rows[index].name; }, nullptr, nullptr,
        // The label is the row's value, so what each tab will say reads down the
        // right-hand side at a glance.
        [&rows](int index) -> std::string { return ynabAccountLabel(rows[index]); }, true);
  }

  const bool navigable = state == State::LIST && itemCount > 0;
  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK), navigable ? tr(STR_YNAB_EDIT_LABEL) : (state == State::FAILED ? tr(STR_OK_BUTTON) : ""),
      navigable ? tr(STR_DIR_UP) : "", navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
