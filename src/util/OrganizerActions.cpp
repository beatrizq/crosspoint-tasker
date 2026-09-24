#include "OrganizerActions.h"

#include <CompanionMood.h>
#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistTaskCache.h>

#include <cstdio>
#include <memory>

#include "activities/ActivityManager.h"
#include "activities/home/FocusSessionActivity.h"
#include "companion/CompanionTracker.h"

namespace organizerActions {

void completeTask(const size_t cacheIndex) {
  if (cacheIndex >= TODOIST_TASKS.getTasks().size()) return;

  LOG_DBG("ORGACT", "Completing task: %s", TODOIST_TASKS.getTasks()[cacheIndex].content.c_str());
  // Queued locally and pushed on the next sync, so completing works with the
  // radio off; the row leaves the list immediately either way.
  TODOIST_TASKS.completeTaskAt(cacheIndex);
  TODOIST_TASKS.saveToFile();
  // A completion is one of the things the companion's mood tracks; credit it
  // immediately rather than waiting for the next sync or Home visit.
  COMPANION.recordActivity();
}

void rescheduleTask(const size_t cacheIndex, const uint16_t newDueDays) {
  if (cacheIndex >= TODOIST_TASKS.getTasks().size()) return;

  LOG_DBG("ORGACT", "Rescheduling task: %s", TODOIST_TASKS.getTasks()[cacheIndex].content.c_str());
  // Queued locally and pushed on the next sync, so rescheduling works with
  // the radio off; the row shows the new date immediately either way.
  TODOIST_TASKS.rescheduleTaskAt(cacheIndex, newDueDays);
  TODOIST_TASKS.saveToFile();
}

void logHabit(const size_t cacheIndex, const float amount) {
  if (cacheIndex >= HABITIFY_HABITS.getHabits().size()) return;
  if (amount <= 0.0f) return;
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (habits[cacheIndex].unitSymbol.empty()) return;

  LOG_DBG("ORGACT", "+%g to %s", static_cast<double>(amount), habits[cacheIndex].name.c_str());
  HABITIFY_HABITS.addPending(cacheIndex, amount);
  HABITIFY_HABITS.saveToFile();
  // A completion is one of the things the companion's mood tracks; credit it
  // immediately in case this press is what pushed a habit to isComplete().
  COMPANION.recordActivity();
}

void completeHabit(const size_t cacheIndex) {
  if (cacheIndex >= HABITIFY_HABITS.getHabits().size()) return;
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (habits[cacheIndex].isComplete()) return;

  LOG_DBG("ORGACT", "Completing habit: %s", habits[cacheIndex].name.c_str());
  HABITIFY_HABITS.completeHabitAt(cacheIndex);
  HABITIFY_HABITS.saveToFile();
  // A completion is one of the things the companion's mood tracks; credit it
  // immediately rather than waiting for the next sync or Home visit.
  COMPANION.recordActivity();
}

bool computeFocusSessionEnd(const int durationMinutes, int32_t& endAbsMinutes, uint8_t& endHourUtc,
                            uint8_t& endMinuteUtc) {
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getUtcDateTime(year, month, day, hour, minute)) return false;

  // Offset 0: a plain UTC day number, since this value only ever gets
  // compared against another one computed the same way (see
  // FocusSessionActivity::onEnter()) -- the user's timezone plays no part in
  // measuring elapsed time, only in how endHourUtc/endMinuteUtc get displayed.
  const int32_t dayNumber = companion::localDayNumber(year, month, day, hour, minute, 0);
  endAbsMinutes = dayNumber * 1440 + hour * 60 + minute + durationMinutes;

  const int32_t endOfDayMinutes = ((hour * 60 + minute + durationMinutes) % 1440 + 1440) % 1440;
  endHourUtc = static_cast<uint8_t>(endOfDayMinutes / 60);
  endMinuteUtc = static_cast<uint8_t>(endOfDayMinutes % 60);
  return true;
}

void beginFocusSession(const std::string& text, const std::string& itemId, const bool isHabit,
                       const int durationMinutes, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  int32_t endAbsMinutes = 0;
  uint8_t endHourUtc = 0;
  uint8_t endMinuteUtc = 0;
  if (!computeFocusSessionEnd(durationMinutes, endAbsMinutes, endHourUtc, endMinuteUtc)) return;

  activityManager.replaceActivity(std::make_unique<FocusSessionActivity>(renderer, mappedInput, text, itemId, isHabit,
                                                                         endAbsMinutes, endHourUtc, endMinuteUtc));
}

std::vector<std::string> focusSessionDurationOptions() {
  std::vector<std::string> options;
  options.reserve(FOCUS_SESSION_DURATIONS_COUNT);
  for (const int minutes : FOCUS_SESSION_DURATIONS_MINUTES) {
    char buf[16];
    snprintf(buf, sizeof(buf), tr(STR_SLEEP_TIMER_VALUE_FORMAT), static_cast<unsigned>(minutes));
    options.emplace_back(buf);
  }
  return options;
}

}  // namespace organizerActions
