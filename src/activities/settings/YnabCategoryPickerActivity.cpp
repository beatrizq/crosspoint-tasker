#include "YnabCategoryPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <YnabStore.h>

#include <memory>
#include <string>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"

void YnabCategoryPickerActivity::onEnter() {
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
    fetchCategories();
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
                           fetchCategories();
                         });
}

void YnabCategoryPickerActivity::onExit() {
  // Persist once on the way out rather than on every toggle: SD writes are the
  // expensive part and the user may tick several boxes in a row.
  if (dirty) {
    YNAB_STORE.saveToFile();
    LOG_DBG("YCP", "Saved %zu selected categories", YNAB_STORE.getSelectedCategories().size());
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

void YnabCategoryPickerActivity::fetchCategories() {
  freeink::SecureHttpClient http;
  http.setInsecure();
  const YnabClient::Error error = YnabClient::fetchCategoryList(http, categories);
  RenderLock lock(*this);
  if (error != YnabClient::OK) {
    LOG_ERR("YCP", "Category list failed: %s", YnabClient::errorString(error));
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
  state = State::LIST;
  selectedIndex = 0;
  requestUpdate(true);
}

void YnabCategoryPickerActivity::toggleSelected() {
  if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= categories.size()) return;
  YNAB_STORE.toggleCategory(categories[selectedIndex].id);
  dirty = true;
  requestUpdate(true);
}

void YnabCategoryPickerActivity::loop() {
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
    toggleSelected();
    return;
  }

  const int itemCount = static_cast<int>(categories.size());
  if (state != State::LIST || itemCount == 0) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      toggleSelected();
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

void YnabCategoryPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_YNAB_CATEGORIES),
                 nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;
  const int itemCount = static_cast<int>(categories.size());

  if (state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_YNAB_LOADING_CATEGORIES));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_YNAB_NONE_AVAILABLE));
  } else {
    const auto& cats = categories;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        [&cats](int index) -> std::string { return cats[index].name; }, nullptr, nullptr,
        // The tick is the row's value, so selection reads at a glance without
        // needing a checkbox glyph the themes do not all provide.
        [&cats](int index) -> std::string {
          return std::string(YNAB_STORE.isCategorySelected(cats[index].id) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
        },
        true);
  }

  const bool navigable = state == State::LIST && itemCount > 0;
  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK), navigable ? tr(STR_SELECT) : (state == State::FAILED ? tr(STR_OK_BUTTON) : ""),
      navigable ? tr(STR_DIR_UP) : "", navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
