#include "SyncAllActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"

void SyncAllActivity::onEnter() {
  Activity::onEnter();

  // Which services are worth a request. Decided once, up front, so the list on
  // screen does not change shape while the run is going.
  int configured = 0;
  for (int i = 0; i < plannerSync::SERVICE_COUNT; i++) {
    const bool ready = plannerSync::isConfigured(plannerSync::serviceAt(i));
    states[i] = ready ? RowState::Waiting : RowState::Skipped;
    if (ready) configured++;
  }

  if (configured == 0) {
    nothingToDo = true;
    finished = true;
    requestUpdate(true);
    return;
  }
  requestUpdate(true);

  // Past this point every path uses WiFi, so onExit() owes a teardown.
  wifiActivated = true;

  // Free NimBLE's ~55KB init-time heap reservation before WiFi/TLS need their
  // own headroom. No matching resume() call: onExit() below always reboots
  // once wifiActivated is set (see its own comment), and BleNotifyRelay::begin()
  // re-advertises fresh on the next boot -- deliberately not exercising
  // NimBLEDevice::init()'s deinit()-then-reinit path here (see
  // BleNotifyRelay::resume()'s own doc comment for the vendored library bug
  // that path hits).
  BleNotifyRelay::pause();

  if (WiFi.status() == WL_CONNECTED) {
    runAll();
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             {
                               RenderLock lock;
                               // Nothing was reached, so every row says so rather
                               // than one of them owning the Wi-Fi failure.
                               for (int i = 0; i < plannerSync::SERVICE_COUNT; i++) {
                                 if (states[i] == RowState::Waiting) {
                                   states[i] = RowState::Failed;
                                   messages[i] = tr(STR_WIFI_CONN_FAILED);
                                 }
                               }
                               finished = true;
                             }
                             requestUpdate(true);
                             return;
                           }
                           runAll();
                         });
}

void SyncAllActivity::onExit() {
  Activity::onExit();
  // Same teardown as the planner screens: drop the association, then reboot
  // silently so the WiFi/TLS heap fragmentation goes with it. The mode check
  // keeps a cancelled Wi-Fi picker (radio never brought up) from costing a
  // reboot; a run that already took the radio down reports WIFI_MODE_NULL by
  // then, so it says so itself.
  if (wifiActivated && (radioTornDown || WiFi.getMode() != WIFI_MODE_NULL)) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void SyncAllActivity::runAll() {
  // Unconditional, ahead of the per-service loop: of the four services, only
  // Tasks happens to sync the clock too (as a side effect of resolving its own
  // "today"). A run without Tasks configured -- or one where it fails -- would
  // otherwise leave the clock untouched even though WiFi is already up here.
  halClock.syncFromNTP();

  for (int i = 0; i < plannerSync::SERVICE_COUNT; i++) {
    if (states[i] != RowState::Waiting) continue;

    {
      RenderLock lock;
      states[i] = RowState::Syncing;
    }
    // Waited for, not fired and forgotten: the sync below blocks for as long as
    // the requests take, so the row has to be on screen before it starts or the
    // screen would sit on a stale list through the whole thing.
    requestUpdateAndWait();

    const char* failure = plannerSync::run(plannerSync::serviceAt(i));

    {
      RenderLock lock;
      states[i] = failure == nullptr ? RowState::Done : RowState::Failed;
      messages[i] = failure;
    }
    if (failure != nullptr) {
      LOG_ERR("SYNCALL", "%s failed: %s", plannerSync::name(plannerSync::serviceAt(i)), failure);
    }
    // Deliberately not breaking on failure: the two are independent accounts,
    // and one expired token should not cost the rest of the run.
  }

  // Once, at the end, rather than after each service - which is the whole point
  // of this screen. Through WiFi.mode(WIFI_OFF) rather than by stopping the
  // driver directly, for the reason spelled out on
  // PlannerScreenActivity::tearDownRadio.
  WiFi.mode(WIFI_OFF);
  radioTornDown = true;

  {
    RenderLock lock;
    finished = true;
  }
  // Waited for, not fired and forgotten (see the same reasoning above line 97):
  // Calendar is always the last service in plannerSync's fixed order, so its
  // terminal Done/Failed state has no later iteration to force a confirmed
  // repaint the way Tasks gets for free. An unconfirmed
  // requestUpdate(true) here left the screen stuck on Calendar's last confirmed
  // frame ("Syncing") once the idle timer downclocked the CPU right after this
  // blocking call returned.
  requestUpdateAndWait();
}

void SyncAllActivity::loop() {
  // Requests cannot be interrupted once started, so Back does nothing until the
  // run is over. Leaving mid-flight would strand the radio and the caches.
  if (!finished) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right1) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (onReturn) {
      onReturn();
    } else {
      onGoHome();
    }
  }
}

const char* SyncAllActivity::rowStatus(const int index) const {
  switch (states[index]) {
    case RowState::Skipped:
      return tr(STR_SYNC_ALL_NOT_SET_UP);
    case RowState::Waiting:
      return tr(STR_SYNC_ALL_WAITING);
    case RowState::Syncing:
      return tr(STR_SYNC_ALL_SYNCING);
    case RowState::Done:
      return tr(STR_SYNC_ALL_DONE);
    case RowState::Failed:
      // The reason, not just "failed": it is the only place the user finds out
      // which token expired.
      return messages[index] != nullptr ? messages[index] : tr(STR_SYNC_ALL_FAILED);
  }
  return "";
}

void SyncAllActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SYNC_ALL), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  if (nothingToDo) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SYNC_ALL_NOTHING));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, plannerSync::SERVICE_COUNT,
        // No selection: nothing here is navigable, so no row is highlighted.
        -1, [](int index) -> std::string { return std::string(plannerSync::name(plannerSync::serviceAt(index))); },
        nullptr, nullptr, [this](int index) -> std::string { return std::string(rowStatus(index)); }, true,
        // A service that was never set up is dimmed, so the eye goes to the ones
        // that actually ran.
        [this](int index) -> bool { return states[index] == RowState::Skipped; });
  }

  // Nothing to press until the run is over; offering Back mid-flight would only
  // look broken when it did nothing.
  const auto labels = mappedInput.mapLabels(finished ? tr(STR_HOME) : "", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
