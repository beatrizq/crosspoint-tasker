#include "CompanionSettingsActivity.h"

#include <CompanionMood.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/settings/CompanionSleepTimeActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "companion/CompanionSprites.generated.h"
#include "companion/CompanionState.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_SHOW_COMPANION = 0;
constexpr int ROW_NICKNAME = 1;
constexpr int ROW_SLEEP_START = 2;
constexpr int ROW_SLEEP_END = 3;
constexpr int ROW_AMAZED_POINTS = 4;
constexpr int ROW_HAPPY_POINTS = 5;
constexpr int ROW_SATISFIED_POINTS = 6;
constexpr int ROW_NEGLECTED_DAYS = 7;
constexpr int ROW_RESET = 8;

// "HH:MM" for a Sleep start/end row's value column -- digits need no
// translation.
std::string sleepTimeValue(const uint8_t hour, const uint8_t minute) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
  return std::string(buf);
}

// "N pts" / "N days" for a threshold row's value column, sharing the same
// format strings IntervalSelectionActivity uses inside the picker itself.
std::string thresholdValue(const StrId formatId, const uint8_t n) {
  char buf[16];
  snprintf(buf, sizeof(buf), I18N.get(formatId), static_cast<unsigned>(n));
  return std::string(buf);
}

}  // namespace

void CompanionSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  stampActivationIfNeeded();
  requestUpdate();
}

void CompanionSettingsActivity::onExit() { Activity::onExit(); }

void CompanionSettingsActivity::stampActivationIfNeeded() {
  if (COMPANION_STATE.activatedDay != companion::DayLedger::NEVER) return;
  int32_t today = 0;
  if (!CompanionTracker::resolveLocalDay(today)) return;
  COMPANION_STATE.activatedDay = today;
  COMPANION_STATE.saveToFile();
}

void CompanionSettingsActivity::handleSelection() {
  switch (selectedIndex) {
    case ROW_SHOW_COMPANION:
      SETTINGS.showCompanion = SETTINGS.showCompanion ? 0 : 1;
      break;
    case ROW_NICKNAME:
      editSettingsText(tr(STR_NICKNAME_ENTER), SETTINGS.companionNickname, sizeof(SETTINGS.companionNickname));
      return;
    case ROW_SLEEP_START:
    case ROW_SLEEP_END:
      startActivityForResult(
          std::make_unique<CompanionSleepTimeActivity>(renderer, mappedInput, selectedIndex == ROW_SLEEP_START),
          [this](const ActivityResult&) {
            // This sub-screen answers Back/Confirm on the button going down
            // in places, so a release can still land here once it's closed
            // -- same reasoning as swallowConfirmRelease in onEnter().
            if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
              swallowConfirmRelease = true;
            }
            if (mappedInput.isPressed(MappedInputManager::Button::Right1)) {
              swallowBackRelease = true;
            }
            requestUpdate();
          });
      return;
    case ROW_AMAZED_POINTS:
    case ROW_HAPPY_POINTS:
    case ROW_SATISFIED_POINTS:
    case ROW_NEGLECTED_DAYS:
      offerThresholdPicker(selectedIndex);
      return;
    case ROW_RESET:
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_COMPANION_RESET_CONFIRM), ""),
          [this](const ActivityResult& result) {
            // The popup answers on the button going down, so its release
            // lands back here once this screen is active again -- same
            // reasoning as swallowConfirmRelease in onEnter().
            if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
              swallowConfirmRelease = true;
            }
            if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
              swallowBackRelease = true;
            }
            if (result.isCancelled) return;
            COMPANION_STATE.reset();
            // Immediately re-stamped rather than left at "Not yet" -- a
            // reset should read as freshly active, not as if the companion
            // had never been active at all.
            stampActivationIfNeeded();
            COMPANION_STATE.saveToFile();
            requestUpdate();
          });
      return;
  }
  SETTINGS.saveToFile();
}

void CompanionSettingsActivity::offerThresholdPicker(const int row) {
  StrId titleId = StrId::STR_NONE_OPT;
  StrId formatId = StrId::STR_NONE_OPT;
  int initialValue = 0;
  int minValue = 1;
  int maxValue = 1;

  switch (row) {
    case ROW_AMAZED_POINTS:
      titleId = StrId::STR_COMPANION_AMAZED_AT;
      formatId = StrId::STR_COMPANION_POINTS_FORMAT;
      initialValue = SETTINGS.companionAmazedPoints;
      // Must stay above Happy's own bar, or the Amazed tier is unreachable.
      minValue = static_cast<int>(SETTINGS.companionHappyPoints) + 1;
      maxValue = 30;
      break;
    case ROW_HAPPY_POINTS:
      titleId = StrId::STR_COMPANION_HAPPY_AT;
      formatId = StrId::STR_COMPANION_POINTS_FORMAT;
      initialValue = SETTINGS.companionHappyPoints;
      // Must stay above Satisfied's own bar, or the Happy tier is unreachable,
      // and below Amazed's, or Amazed swallows it.
      minValue = static_cast<int>(SETTINGS.companionSatisfiedPoints) + 1;
      maxValue = std::max(minValue, static_cast<int>(SETTINGS.companionAmazedPoints) - 1);
      break;
    case ROW_SATISFIED_POINTS:
      titleId = StrId::STR_COMPANION_SATISFIED_AT;
      formatId = StrId::STR_COMPANION_POINTS_FORMAT;
      initialValue = SETTINGS.companionSatisfiedPoints;
      minValue = 1;
      // Must stay below Happy's own bar, for the same reason in reverse.
      maxValue = static_cast<int>(SETTINGS.companionHappyPoints) - 1;
      break;
    case ROW_NEGLECTED_DAYS:
      titleId = StrId::STR_COMPANION_NEGLECTED_AFTER;
      formatId = StrId::STR_COMPANION_DAYS_FORMAT;
      initialValue = SETTINGS.companionNeglectedDays;
      minValue = 1;
      maxValue = 14;
      break;
    default:
      return;
  }

  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(renderer, mappedInput, "CompanionThreshold", titleId, initialValue,
                                                  minValue, maxValue, /*smallStep=*/1, /*largeStep=*/5, formatId,
                                                  /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true),
      [this, row](const ActivityResult& result) {
        // Same reasoning as ROW_RESET's popup above.
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int v = std::get<IntervalResult>(result.data).value;
        // The picker's own min/max already enforced these bounds, so this is
        // a defensive clamp rather than the primary guard -- kept in case the
        // paired field's value changed some other way while this was open.
        switch (row) {
          case ROW_AMAZED_POINTS:
            SETTINGS.companionAmazedPoints =
                static_cast<uint8_t>(std::max(v, static_cast<int>(SETTINGS.companionHappyPoints) + 1));
            break;
          case ROW_HAPPY_POINTS:
            SETTINGS.companionHappyPoints =
                static_cast<uint8_t>(std::max(v, static_cast<int>(SETTINGS.companionSatisfiedPoints) + 1));
            break;
          case ROW_SATISFIED_POINTS:
            SETTINGS.companionSatisfiedPoints =
                static_cast<uint8_t>(std::min(v, static_cast<int>(SETTINGS.companionHappyPoints) - 1));
            break;
          case ROW_NEGLECTED_DAYS:
            SETTINGS.companionNeglectedDays = static_cast<uint8_t>(std::max(v, 1));
            break;
          default:
            break;
        }
        SETTINGS.saveToFile();
        requestUpdate();
      });
}

void CompanionSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) swallowBackRelease = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    if (swallowBackRelease) {
      swallowBackRelease = false;
      return;
    }
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
    requestUpdate();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  switch (handleListTouch(selectedIndex, MENU_ITEMS, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
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

void CompanionSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_COMPANION), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.buttonHintsGap;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case ROW_SHOW_COMPANION:
            return std::string(tr(STR_SHOW_COMPANION));
          case ROW_NICKNAME:
            return std::string(tr(STR_NICKNAME));
          case ROW_SLEEP_START:
            return std::string(tr(STR_COMPANION_SLEEP_START));
          case ROW_SLEEP_END:
            return std::string(tr(STR_COMPANION_SLEEP_END));
          case ROW_AMAZED_POINTS:
            return std::string(tr(STR_COMPANION_AMAZED_AT));
          case ROW_HAPPY_POINTS:
            return std::string(tr(STR_COMPANION_HAPPY_AT));
          case ROW_SATISFIED_POINTS:
            return std::string(tr(STR_COMPANION_SATISFIED_AT));
          case ROW_NEGLECTED_DAYS:
            return std::string(tr(STR_COMPANION_NEGLECTED_AFTER));
          default:
            return std::string(tr(STR_COMPANION_RESET));
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        switch (index) {
          case ROW_SHOW_COMPANION:
            return std::string(SETTINGS.showCompanion ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
          case ROW_NICKNAME:
            return std::string(CompanionTracker::displayName());
          case ROW_SLEEP_START:
            return sleepTimeValue(SETTINGS.companionSleepStartHour, SETTINGS.companionSleepStartMinute);
          case ROW_SLEEP_END:
            return sleepTimeValue(SETTINGS.companionSleepEndHour, SETTINGS.companionSleepEndMinute);
          case ROW_AMAZED_POINTS:
            return thresholdValue(StrId::STR_COMPANION_POINTS_FORMAT, SETTINGS.companionAmazedPoints);
          case ROW_HAPPY_POINTS:
            return thresholdValue(StrId::STR_COMPANION_POINTS_FORMAT, SETTINGS.companionHappyPoints);
          case ROW_SATISFIED_POINTS:
            return thresholdValue(StrId::STR_COMPANION_POINTS_FORMAT, SETTINGS.companionSatisfiedPoints);
          case ROW_NEGLECTED_DAYS:
            return thresholdValue(StrId::STR_COMPANION_DAYS_FORMAT, SETTINGS.companionNeglectedDays);
          default:
            // ROW_RESET is an action row with nothing to show in this column.
            return std::string();
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
