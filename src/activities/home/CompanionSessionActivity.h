#pragma once

#include <CompanionMood.h>

#include <cstdint>

#include "activities/Activity.h"

/**
 * NOTE: nothing starts this screen any more. It was built for Focus/Break
 * sessions on the Companion screen's Up/Down side buttons, which now switch
 * Tasks tabs instead; Focus is instead shown by FocusSessionActivity and
 * Break by CompanionTracker::currentMood() when no tasks are left. Kept
 * (with its persisted state and boot-resume branch) rather than deleted, in
 * case a timed Focus/Break screen is wanted again -- delete it if not.
 *
 * The locked countdown phase of a timed Focus or Break session, as opposed
 * to a task-linked one (see FocusSessionActivity for that). No item
 * is attached here: the companion just shows a fixed mood
 * (companion::Mood::Focus or ::Break) for the picked duration, with a short
 * speech-bubble label ("Focus"/"Break") and "Until hh:mm" underneath -- same
 * locked-screen shape as FocusSessionActivity (stays awake, swallows
 * Back/Home, hands off to QuickPickActivity once the countdown elapses), but
 * the mood shown is fixed for the whole session rather than read live from
 * COMPANION.currentMood(). Ending never auto-chains into the other kind of
 * session -- it just returns to the Companion screen, same as
 * FocusSessionActivity's own unlock behaviour.
 *
 * Reached either fresh (a duration just picked from the popup Up/Down opens)
 * or reconstructed at boot from CrossPointState when a session was still
 * running when the device last turned off, same resume story as
 * FocusSessionActivity.
 */
class CompanionSessionActivity final : public Activity {
 public:
  CompanionSessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const companion::Mood mood,
                           const int32_t endAbsMinutes, const uint8_t endHourUtc, const uint8_t endMinuteUtc)
      : Activity("CompanionSession", renderer, mappedInput),
        mood(mood),
        endAbsMinutes(endAbsMinutes),
        endHourUtc(endHourUtc),
        endMinuteUtc(endMinuteUtc) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool handleHomeGesture() override { return true; }

 private:
  // companion::Mood::Focus or ::Break -- fixed for the whole session, shown
  // instead of COMPANION.currentMood() (see this file's own header comment).
  companion::Mood mood;
  // See computeFocusSessionEnd()/HalClock::formatHourMinute() for what these
  // mean; both are UTC.
  int32_t endAbsMinutes;
  uint8_t endHourUtc;
  uint8_t endMinuteUtc;

  // Set in onEnter() from endAbsMinutes and the wall clock read at that
  // moment, then never touched again: the countdown runs on device uptime
  // from there, so no further wall-clock reads are needed while it's ticking.
  unsigned long sessionEndMillis = 0;
  bool locked = false;
};
