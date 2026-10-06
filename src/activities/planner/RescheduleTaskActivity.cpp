#include "RescheduleTaskActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Side buttons step the active field by this much per press (front buttons
// still step by 1); see IntervalSelectionActivity's own front/side split for
// the same convention elsewhere.
constexpr int LARGE_STEP = 5;

uint32_t daysInMonth(const int32_t year, const uint32_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leap) return 29;
  return DAYS[month - 1];
}
}  // namespace

void RescheduleTaskActivity::onEnter() {
  Activity::onEnter();
  loadInitialDate();
  activeField = FIELD_DAY;
  // Same reasoning as every other screen reached from a menu selection: the
  // Confirm press that picked "Reschedule" may still be physically down.
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

void RescheduleTaskActivity::loadInitialDate() {
  const uint16_t seed = initialDate == civil::NO_DATE ? civil::packDate(2000, 1, 1) : initialDate;
  civil::civilFromDays(static_cast<int32_t>(seed) + civil::DAYS_1970_TO_2000, year, month, day);
}

uint16_t RescheduleTaskActivity::packedDate() const { return civil::packDate(year, month, static_cast<uint32_t>(day)); }

uint32_t RescheduleTaskActivity::daysInActiveMonth() const { return daysInMonth(year, month); }

void RescheduleTaskActivity::drawStepHintLine(const int y, const StrId labelId, const int step) const {
  char line[32];
  snprintf(line, sizeof(line), "%s %d", I18N.get(labelId), step);
  renderer.drawCenteredText(SMALL_FONT_ID, y, line, true);
}

void RescheduleTaskActivity::adjustActiveField(const int delta) {
  switch (activeField) {
    case FIELD_DAY: {
      const int maxDay = static_cast<int>(daysInActiveMonth());
      int next = (static_cast<int>(day) - 1 + delta) % maxDay;
      if (next < 0) next += maxDay;
      day = static_cast<uint32_t>(next) + 1;
      break;
    }
    case FIELD_MONTH: {
      int next = (static_cast<int>(month) - 1 + delta) % 12;
      if (next < 0) next += 12;
      month = static_cast<uint32_t>(next) + 1;
      // A day valid in the old month may not exist in the new one - e.g. the
      // 31st, moving from a 31-day month into a 30-day one.
      const uint32_t maxDay = daysInActiveMonth();
      if (day > maxDay) day = maxDay;
      break;
    }
    case FIELD_YEAR: {
      // civil::packDate's representable range.
      constexpr int32_t MIN_YEAR = 2000;
      constexpr int32_t MAX_YEAR = 2179;
      year += delta;
      if (year < MIN_YEAR) year = MIN_YEAR;
      if (year > MAX_YEAR) year = MAX_YEAR;
      const uint32_t maxDay = daysInActiveMonth();
      if (day > maxDay) day = maxDay;
      break;
    }
    default:
      break;
  }
}

bool RescheduleTaskActivity::fieldFromPoint(const int x, const int y, Field& field) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int centreY = pageHeight / 2 - 40;
  auto widthOf = [&](const char* s) { return renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD); };
  constexpr int fieldPaddingX = 6;
  constexpr int dashGap = 5;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldHeight = lineHeight + 2;

  const int dayBoxW = widthOf("00") + fieldPaddingX * 2;
  const int monthBoxW = widthOf("00") + fieldPaddingX * 2;
  const int yearBoxW = widthOf("0000") + fieldPaddingX * 2;
  const int dashWidth = widthOf("-");
  const int totalWidth = dayBoxW + dashGap + dashWidth + dashGap + monthBoxW + dashGap + dashWidth + dashGap + yearBoxW;

  int boxX = (pageWidth - totalWidth) / 2;
  auto hit = [&](const int width) {
    return x >= boxX && x < boxX + width && y >= centreY && y < centreY + fieldHeight;
  };
  if (hit(dayBoxW)) {
    field = FIELD_DAY;
    return true;
  }
  boxX += dayBoxW + dashGap + dashWidth + dashGap;
  if (hit(monthBoxW)) {
    field = FIELD_MONTH;
    return true;
  }
  boxX += monthBoxW + dashGap + dashWidth + dashGap;
  if (hit(yearBoxW)) {
    field = FIELD_YEAR;
    return true;
  }
  return false;
}

void RescheduleTaskActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) {
    setResult(DateResult{packedDate()});
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    activeField = static_cast<Field>((activeField + 1) % FIELD_COUNT);
    requestUpdate();
    return;
  }

  if (mappedInput.hasTouch()) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      Field touchedField = FIELD_DAY;
      if (fieldFromPoint(tx, ty, touchedField)) {
        activeField = touchedField;
        requestUpdate();
        return;
      }
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left1}, [this] {
    adjustActiveField(-1);
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left2}, [this] {
    adjustActiveField(+1);
    requestUpdate();
  });

  // On X3 the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left
  // button and BTN_DOWN the right one. Flip the large-step direction there so
  // the left button decreases and the right button increases, matching the
  // layout -- same reasoning as IntervalSelectionActivity's own split.
  const int upDelta = gpio.deviceIsX3() ? -LARGE_STEP : LARGE_STEP;
  const int downDelta = gpio.deviceIsX3() ? LARGE_STEP : -LARGE_STEP;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] {
    adjustActiveField(upDelta);
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, downDelta] {
    adjustActiveField(downDelta);
    requestUpdate();
  });
}

void RescheduleTaskActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_RESCHEDULE_TASK));

  const int centreY = pageHeight / 2 - 40;
  auto widthOf = [&](const char* s) { return renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD); };
  constexpr int fieldPaddingX = 6;
  constexpr int dashGap = 5;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldHeight = lineHeight + 2;

  char dayStr[4];
  snprintf(dayStr, sizeof(dayStr), "%02u", static_cast<unsigned>(day));
  char monthStr[4];
  snprintf(monthStr, sizeof(monthStr), "%02u", static_cast<unsigned>(month));
  char yearStr[6];
  snprintf(yearStr, sizeof(yearStr), "%04d", static_cast<int>(year));

  const int dayBoxW = widthOf("00") + fieldPaddingX * 2;
  const int monthBoxW = widthOf("00") + fieldPaddingX * 2;
  const int yearBoxW = widthOf("0000") + fieldPaddingX * 2;
  const int dashWidth = widthOf("-");
  const int totalWidth = dayBoxW + dashGap + dashWidth + dashGap + monthBoxW + dashGap + dashWidth + dashGap + yearBoxW;

  int x = (pageWidth - totalWidth) / 2;

  auto drawField = [&](const char* text, const int boxX, const int boxWidth, const Field field) {
    const bool selected = activeField == field;
    renderer.fillRectDither(boxX, centreY, boxWidth, fieldHeight, selected ? Color::LightGray : Color::White);
    renderer.drawRect(boxX, centreY, boxWidth, fieldHeight, true);
    if (selected) {
      renderer.drawRect(boxX + 1, centreY + 1, boxWidth - 2, fieldHeight - 2, true);
    }
    const int textX = boxX + (boxWidth - widthOf(text)) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, centreY, text, true, EpdFontFamily::BOLD);
  };

  drawField(dayStr, x, dayBoxW, FIELD_DAY);
  x += dayBoxW + dashGap;
  renderer.drawText(UI_12_FONT_ID, x, centreY, "-", true, EpdFontFamily::BOLD);
  x += dashWidth + dashGap;

  drawField(monthStr, x, monthBoxW, FIELD_MONTH);
  x += monthBoxW + dashGap;
  renderer.drawText(UI_12_FONT_ID, x, centreY, "-", true, EpdFontFamily::BOLD);
  x += dashWidth + dashGap;

  drawField(yearStr, x, yearBoxW, FIELD_YEAR);

  // Two-line step hint: front buttons do the small step, side buttons the
  // large one -- same legend and wording IntervalSelectionActivity uses.
  const int hintLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int hint1Y = centreY + fieldHeight + 20;
  drawStepHintLine(hint1Y, StrId::STR_STEP_HINT_FRONT, 1);
  drawStepHintLine(hint1Y + hintLineHeight + 6, StrId::STR_STEP_HINT_SIDE, LARGE_STEP);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEXT_FIELD), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
