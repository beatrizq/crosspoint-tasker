#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

/**
 * The locked countdown phase of a focus session: companion (shown in its
 * Focus pose for the whole session), speech bubble holding the task
 * text, and "Focus session until hh:mm" underneath.
 * Stays awake (preventAutoSleep()) and swallows Back and the Home gesture for
 * as long as it is locked -- a deliberate commitment device, not an oversight,
 * so there is no early-exit gesture to wire up.
 *
 * Reached either fresh (a duration just picked from the Options menu -- see
 * plannerActions::beginFocusSession()) or reconstructed at boot from
 * CrossPointState when a session was still running when the device last
 * turned off, so a reboot resumes the lock instead of losing it. Either way
 * the constructor takes the same already-resolved end time; onEnter()
 * re-reads the wall clock itself to work out how much of it is actually left
 * to wait through, which is what makes both paths the same code.
 *
 * Once the countdown elapses -- noticed live in loop(), or immediately in
 * onEnter() when reconstructed after the end time already passed -- this
 * replaces itself with QuickPickActivity for the same item, which is the
 * "wakes up on the companion screen, same speech bubble" unlocked behaviour:
 * Options, Complete/Log, reroll-on-completion all come from there rather than
 * being duplicated here.
 */
class FocusSessionActivity final : public Activity {
 public:
  FocusSessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text, std::string itemId,
                       const int32_t endAbsMinutes, const uint8_t endHourUtc, const uint8_t endMinuteUtc)
      : Activity("FocusSession", renderer, mappedInput),
        text(std::move(text)),
        itemId(std::move(itemId)),
        endAbsMinutes(endAbsMinutes),
        endHourUtc(endHourUtc),
        endMinuteUtc(endMinuteUtc) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool handleHomeGesture() override { return true; }

 private:
  std::string text;
  std::string itemId;
  // See computeFocusSessionEnd()/HalClock::formatHourMinute() for what these
  // mean; both are UTC.
  int32_t endAbsMinutes;
  uint8_t endHourUtc;
  uint8_t endMinuteUtc;

  // Set in onEnter() from endAbsMinutes and the wall clock read at that
  // moment, then never touched again: the countdown runs on device uptime
  // from there, so no further wall-clock reads (and no day-rollover
  // bookkeeping) are needed while it's ticking.
  unsigned long sessionEndMillis = 0;
  bool locked = false;
};
