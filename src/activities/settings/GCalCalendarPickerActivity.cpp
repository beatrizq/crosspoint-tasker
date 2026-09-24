#include "GCalCalendarPickerActivity.h"

#include <GCalAuth.h>
#include <GCalStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <memory>
#include <string>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"

void GCalCalendarPickerActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  state = State::LOADING;
  requestUpdate();

  // Past this point every path uses WiFi, so onExit() owes a teardown. Free
  // NimBLE's ~55KB init-time heap reservation before WiFi/TLS need their own
  // headroom -- no matching resume(): onExit() below reboots once
  // wifiActivated is set, and BleNotifyRelay::begin() re-advertises fresh on
  // the next boot. wifiActivated itself was already declared here but never
  // actually checked anywhere -- onExit() never rebooted after WiFi use at
  // all, a pre-existing gap independent of BLE.
  wifiActivated = true;
  BleNotifyRelay::pause();

  if (WiFi.status() == WL_CONNECTED) {
    fetchCalendars();
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
                           fetchCalendars();
                         });
}

void GCalCalendarPickerActivity::onExit() {
  // Persist once on the way out rather than on every toggle: SD writes are the
  // expensive part and the user may tick several boxes in a row.
  if (dirty) {
    GCAL_STORE.saveToFile();
    LOG_DBG("GCP", "Saved %zu selected calendars", GCAL_STORE.getSelectedCalendars().size());
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

void GCalCalendarPickerActivity::fetchCalendars() {
  std::string accessToken;
  uint16_t serverDate = civil::NO_DATE;
  const GCalAuth::Error authError = GCalAuth::refreshAccessToken(accessToken, serverDate);
  if (authError != GCalAuth::OK) {
    RenderLock lock(*this);
    LOG_ERR("GCP", "Token refresh failed: %s", GCalAuth::errorString(authError));
    state = State::FAILED;
    statusMessage = authError == GCalAuth::INVALID_GRANT ? tr(STR_GCAL_RELINK_NEEDED) : tr(STR_NETWORK_ERROR);
    requestUpdate(true);
    return;
  }

  const GCalClient::Error error = GCalClient::fetchCalendars(accessToken, calendars);
  RenderLock lock(*this);
  if (error != GCalClient::OK) {
    LOG_ERR("GCP", "Calendar list failed: %s", GCalClient::errorString(error));
    state = State::FAILED;
    statusMessage = error == GCalClient::AUTH_FAILED ? tr(STR_GCAL_RELINK_NEEDED) : tr(STR_NETWORK_ERROR);
    requestUpdate(true);
    return;
  }
  state = State::LIST;
  selectedIndex = 0;
  requestUpdate(true);
}

void GCalCalendarPickerActivity::toggleSelected() {
  if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= calendars.size()) return;
  GCAL_STORE.toggleCalendar(calendars[selectedIndex].id);
  dirty = true;
  requestUpdate(true);
}

void GCalCalendarPickerActivity::loop() {
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

  const int itemCount = static_cast<int>(calendars.size());
  if (state != State::LIST || itemCount == 0) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;
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

void GCalCalendarPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GCAL_CALENDARS),
                 nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;
  const int itemCount = static_cast<int>(calendars.size());

  if (state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GCAL_LOADING_CALENDARS));
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_GCAL_NO_CALENDARS));
  } else {
    const auto& cals = calendars;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        [&cals](int index) -> std::string { return cals[index].summary; }, nullptr, nullptr,
        // The tick is the row's value, so selection reads at a glance without
        // needing a checkbox glyph the themes do not all provide.
        [&cals](int index) -> std::string {
          return std::string(GCAL_STORE.isCalendarSelected(cals[index].id) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
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
