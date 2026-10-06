#include "GCalSettingsActivity.h"

#include <GCalEventCache.h>
#include <GCalStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <memory>
#include <string>

#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/GCalCalendarPickerActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"
#include "util/HomeAppOrder.h"

namespace {
constexpr int ROW_NICKNAME = 0;
constexpr int ROW_CLIENT_ID = 1;
constexpr int ROW_CLIENT_SECRET = 2;
constexpr int ROW_LINK = 3;
constexpr int ROW_CALENDARS = 4;
constexpr int ROW_HINT = 5;
}  // namespace

void GCalSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  state = State::MENU;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

void GCalSettingsActivity::onExit() {
  Activity::onExit();

  // Reclaim WiFi/TLS heap fragmentation the same way every other WiFi-using
  // screen does, now that beginLinking() pauses BLE first.
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void GCalSettingsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    if (state != State::MENU) {
      // Abandon the pairing attempt rather than leaving the screen: the code is
      // useless once we stop polling, and the user may want to retry.
      RenderLock lock(*this);
      state = State::MENU;
      statusMessage = nullptr;
      requestUpdate(true);
      return;
    }
    finish();
    return;
  }

  if (state == State::PAIRING) {
    pollPairing();
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
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;
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

void GCalSettingsActivity::handleSelection() {
  if (selectedIndex == ROW_NICKNAME) {
    size_t size = 0;
    char* field = homeAppOrder::nicknameField(homeAppOrder::AppId::Calendar, size);
    editSettingsText(tr(STR_NICKNAME_ENTER), field, size);
    return;
  }

  if (selectedIndex == ROW_CLIENT_ID) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_GCAL_ENTER_CLIENT_ID),
                                                GCAL_STORE.getClientId(), GCalStore::MAX_CLIENT_ID_LEN),
        [this](const ActivityResult& result) {
          if (result.isCancelled) return;
          GCAL_STORE.setClientId(std::get<KeyboardResult>(result.data).text);
          GCAL_STORE.saveToFile();
          requestUpdate();
        });
    return;
  }

  if (selectedIndex == ROW_CLIENT_SECRET) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                               renderer, mappedInput, tr(STR_GCAL_ENTER_CLIENT_SECRET), GCAL_STORE.getClientSecret(),
                               GCalStore::MAX_SECRET_LEN, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             GCAL_STORE.setClientSecret(std::get<KeyboardResult>(result.data).text);
                             GCAL_STORE.saveToFile();
                             requestUpdate();
                           });
    return;
  }

  if (selectedIndex == ROW_LINK) {
    if (GCAL_STORE.isLinked()) {
      // Unlinking drops the cached events too: they came from an account this
      // device can no longer refresh, so leaving them would show a list that can
      // never update.
      GCAL_STORE.unlink();
      GCAL_STORE.saveToFile();
      GCAL_EVENTS.clear();
      GCAL_EVENTS.saveToFile();
      LOG_INF("GCS", "Account unlinked");
      requestUpdate(true);
      return;
    }
    beginLinking();
    return;
  }

  if (selectedIndex == ROW_CALENDARS) {
    if (!GCAL_STORE.isLinked()) {
      RenderLock lock(*this);
      state = State::FAILED;
      statusMessage = tr(STR_GCAL_NOT_LINKED);
      requestUpdate(true);
      return;
    }
    startActivityForResult(std::make_unique<GCalCalendarPickerActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(true); });
  }
  // ROW_HINT is a footnote, not an action.
}

void GCalSettingsActivity::beginLinking() {
  if (!GCAL_STORE.hasClientCredentials()) {
    RenderLock lock(*this);
    state = State::FAILED;
    statusMessage = tr(STR_GCAL_NEED_CLIENT);
    requestUpdate(true);
    return;
  }

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
    requestPairingCode();
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
                           requestPairingCode();
                         });
}

void GCalSettingsActivity::requestPairingCode() {
  const GCalAuth::Error error = GCalAuth::requestDeviceCode(pairing);
  RenderLock lock(*this);
  if (error != GCalAuth::OK) {
    LOG_ERR("GCS", "Device code request failed: %s", GCalAuth::errorString(error));
    state = State::FAILED;
    statusMessage = error == GCalAuth::NO_CLIENT ? tr(STR_GCAL_NEED_CLIENT) : tr(STR_NETWORK_ERROR);
    requestUpdate(true);
    return;
  }
  // Google rejects polls faster than `interval`; obey it from the first tick.
  nextPollMs = millis() + pairing.intervalSec * 1000UL;
  pairingDeadlineMs = millis() + pairing.expiresInSec * 1000UL;
  state = State::PAIRING;
  requestUpdate(true);
}

void GCalSettingsActivity::pollPairing() {
  const unsigned long now = millis();
  if (static_cast<long>(now - pairingDeadlineMs) >= 0) {
    RenderLock lock(*this);
    state = State::FAILED;
    statusMessage = tr(STR_GCAL_CODE_EXPIRED);
    requestUpdate(true);
    return;
  }
  if (static_cast<long>(now - nextPollMs) < 0) return;

  std::string refreshToken;
  std::string accessToken;
  const GCalAuth::Error error = GCalAuth::pollForTokens(pairing.deviceCode, refreshToken, accessToken);

  if (error == GCalAuth::AUTH_PENDING || error == GCalAuth::SLOW_DOWN) {
    // slow_down means the interval was too aggressive; RFC 8628 says add 5s.
    if (error == GCalAuth::SLOW_DOWN) pairing.intervalSec += 5;
    nextPollMs = millis() + pairing.intervalSec * 1000UL;
    return;
  }

  RenderLock lock(*this);
  if (error != GCalAuth::OK) {
    LOG_ERR("GCS", "Pairing failed: %s", GCalAuth::errorString(error));
    state = State::FAILED;
    statusMessage = error == GCalAuth::ACCESS_DENIED ? tr(STR_GCAL_DENIED) : tr(STR_GCAL_CODE_EXPIRED);
    requestUpdate(true);
    return;
  }

  GCAL_STORE.setRefreshToken(refreshToken);
  GCAL_STORE.saveToFile();
  LOG_INF("GCS", "Account linked; pick calendars next");
  state = State::MENU;
  statusMessage = nullptr;
  // Land on the calendar row: linking is useless until something is selected.
  selectedIndex = ROW_CALENDARS;
  requestUpdate(true);
}

void GCalSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CALENDAR), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (state == State::PAIRING) {
    // The pairing display: the URL to visit and the code to type there. Drawn
    // large and centered because it is read off the screen and typed elsewhere.
    const int centre = pageHeight / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, centre - metrics.headerHeight, tr(STR_GCAL_PAIR_INSTRUCTIONS));
    renderer.drawCenteredText(UI_12_FONT_ID, centre, pairing.verificationUrl.c_str());
    renderer.drawCenteredText(UI_12_FONT_ID, centre + metrics.headerHeight, pairing.userCode.c_str());
    renderer.drawCenteredText(UI_10_FONT_ID, centre + metrics.headerHeight * 2, tr(STR_GCAL_PAIR_WAITING));
  } else {
    const std::string clientIdValue =
        GCAL_STORE.getClientId().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
    const std::string secretValue =
        GCAL_STORE.getClientSecret().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
    char calendarValue[24];
    snprintf(calendarValue, sizeof(calendarValue), "%zu", GCAL_STORE.getSelectedCalendars().size());

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
        [](int index) -> std::string {
          switch (index) {
            case ROW_NICKNAME:
              return std::string(tr(STR_NICKNAME));
            case ROW_CLIENT_ID:
              return std::string(tr(STR_GCAL_CLIENT_ID));
            case ROW_CLIENT_SECRET:
              return std::string(tr(STR_GCAL_CLIENT_SECRET));
            case ROW_LINK:
              return std::string(GCAL_STORE.isLinked() ? tr(STR_GCAL_UNLINK) : tr(STR_GCAL_LINK));
            case ROW_CALENDARS:
              return std::string(tr(STR_GCAL_CALENDARS));
            default:
              return std::string(tr(STR_PLANNER_HOLD_TO_SYNC));
          }
        },
        nullptr, nullptr,
        [&clientIdValue, &secretValue, &calendarValue](int index) -> std::string {
          if (index == ROW_NICKNAME) return std::string(homeAppOrder::displayName(homeAppOrder::AppId::Calendar));
          if (index == ROW_CLIENT_ID) return clientIdValue;
          if (index == ROW_CLIENT_SECRET) return secretValue;
          if (index == ROW_CALENDARS) return std::string(calendarValue);
          return std::string("");
        },
        false, [](int index) -> bool { return index == ROW_HINT; });
  }

  const char* confirmLabel = state == State::PAIRING ? "" : tr(STR_SELECT);
  if (state == State::FAILED) confirmLabel = tr(STR_OK_BUTTON);
  const bool navigable = state == State::MENU;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, navigable ? tr(STR_DIR_UP) : "",
                                            navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
