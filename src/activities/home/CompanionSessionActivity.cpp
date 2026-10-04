#include "CompanionSessionActivity.h"

#include <CompanionMood.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <cstdio>
#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/home/QuickPickActivity.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Same layout constants FocusSessionActivity's own bubble+companion screen
// uses -- this is that same locked lead-in shape, just for a moodless,
// itemless session (see this file's own header comment).
constexpr int MAX_SCALE = 6;
constexpr int PAD = 14;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;
// Between the character's feet and the "Until hh:mm" line.
constexpr int UNTIL_GAP = 4;
// Floor on the bubble's text column -- "Focus"/"Break" are both short, but
// this keeps the bubble from shrinking to an awkward sliver either way.
constexpr int MIN_BUBBLE_TEXT_WIDTH = 80;
}  // namespace

void CompanionSessionActivity::onEnter() {
  Activity::onEnter();

  uint16_t year;
  uint8_t month, day, hour, minute;
  const bool haveClock = halClock.getUtcDateTime(year, month, day, hour, minute);
  int32_t remainingMinutes = 0;
  if (haveClock) {
    // Offset 0: a plain UTC day number is all the comparison below needs --
    // the user's timezone only matters for what gets displayed, not for
    // measuring elapsed time.
    const int32_t dayNumber = companion::localDayNumber(year, month, day, hour, minute, 0);
    const int32_t nowAbsMinutes = dayNumber * 1440 + hour * 60 + minute;
    remainingMinutes = endAbsMinutes - nowAbsMinutes;
  }

  locked = haveClock && remainingMinutes > 0;
  if (!locked) {
    // Nothing to time against, or the session's end time already passed --
    // including a boot resume that landed after it elapsed while the device
    // was off. Clear the persisted session so a later reboot doesn't think
    // one is still running; loop() hands off to the Companion screen on its
    // very next call, before any of this screen ever paints.
    APP_STATE.companionSessionActive = false;
    APP_STATE.saveToFile();
    return;
  }

  sessionEndMillis = millis() + static_cast<unsigned long>(remainingMinutes) * 60000UL;

  APP_STATE.companionSessionActive = true;
  APP_STATE.companionSessionMood = static_cast<uint8_t>(mood);
  APP_STATE.companionSessionEndAbsMinutes = endAbsMinutes;
  APP_STATE.companionSessionEndHour = endHourUtc;
  APP_STATE.companionSessionEndMinute = endMinuteUtc;
  APP_STATE.saveToFile();

  requestUpdate(true);
}

void CompanionSessionActivity::loop() {
  if (locked) {
    if (millis() < sessionEndMillis)
      return;  // still counting down -- Back/Home are swallowed above, nothing else to do
    locked = false;
    APP_STATE.companionSessionActive = false;
    APP_STATE.saveToFile();
  }
  // Unlocked, either just now or already at onEnter(): hand off to the
  // companion's own screen -- never auto-chains into the other kind of
  // session (see this file's own header comment).
  activityManager.replaceActivity(std::make_unique<QuickPickActivity>(renderer, mappedInput));
}

void CompanionSessionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 CompanionTracker::displayName(), nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int contentWidth = pageWidth - MARGIN * 2;
  const int maxTextWidth = contentWidth - PAD * 2;

  const auto id = CompanionTracker::activeId();
  const std::string bubbleText =
      mood == companion::Mood::Focus ? tr(STR_COMPANION_SIDE_FOCUS) : tr(STR_COMPANION_SIDE_BREAK);

  const auto textFit =
      companion::fitBubbleText(renderer, UI_10_FONT_ID, bubbleText, maxTextWidth, MIN_BUBBLE_TEXT_WIDTH, 4);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bubbleH = static_cast<int>(textFit.lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = bubbleH + TAIL_LENGTH + BUBBLE_GAP;
  const int bubbleWidth = textFit.textWidth + PAD * 2;

  char untilTime[16] = "";
  HalClock::formatHourMinute(untilTime, sizeof(untilTime), endHourUtc, endMinuteUtc, SETTINGS.clockUtcOffsetQ,
                             SETTINGS.clockFormat == 1);
  char untilLine[48];
  snprintf(untilLine, sizeof(untilLine), tr(STR_COMPANION_SESSION_UNTIL), untilTime);
  // Same size as the header's own title, so this reads as a second line of
  // header rather than incidental caption text.
  const int untilLineH = renderer.getLineHeight(UI_12_FONT_ID);

  // Whole-pixel scales only: fractional scaling would smear the baked dither.
  int scale = 1;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) > contentWidth) continue;
    if (bubbleBlock + companion::poseHeight(candidate) + UNTIL_GAP + untilLineH <= contentHeight) {
      scale = candidate;
      break;
    }
  }

  const int spriteW = companion::poseWidth(scale);
  const int spriteH = companion::poseHeight(scale);
  const int blockH = bubbleBlock + spriteH + UNTIL_GAP + untilLineH;
  const int blockTop = contentTop + (contentHeight - blockH) / 2;
  const int centreX = pageWidth / 2;
  const int bubbleX = centreX - bubbleWidth / 2;

  companion::drawSpeechBubble(renderer, bubbleX, blockTop, bubbleWidth, bubbleH, TAIL_LENGTH,
                              companion::TailSide::Bottom);
  const Rect textBounds{bubbleX + PAD, blockTop, textFit.textWidth, bubbleH};
  int textY = blockTop + PAD;
  for (const auto& line : textFit.lines) {
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str());
    textY += lineH;
  }

  const int spriteTop = blockTop + bubbleBlock;
  companion::drawPose(renderer, id, mood, centreX - spriteW / 2, spriteTop, scale);

  const int untilW = renderer.getTextWidth(UI_12_FONT_ID, untilLine, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, centreX - untilW / 2, spriteTop + spriteH + UNTIL_GAP, untilLine, true,
                    EpdFontFamily::BOLD);

  // Nothing is navigable while locked -- Back and Home are both swallowed
  // (see preventAutoSleep()/handleHomeGesture()), so there is nothing to hint.
  GUI.drawButtonHints(renderer, "", "", "", "");

  renderer.displayBuffer();
}
