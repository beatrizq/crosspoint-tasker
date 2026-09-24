#include "CompanionTracker.h"

#include <HabitifyHabitCache.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistTaskCache.h>

#include <algorithm>
#include <cstdio>

#include "CompanionState.h"
#include "CrossPointSettings.h"
#include "util/OrganizerSync.h"

namespace {
// clockUtcOffsetQ is biased by 48 so it fits in a uint8_t (48 == UTC+0).
int32_t signedUtcOffsetQuarterHours() {
  uint8_t biased = SETTINGS.clockUtcOffsetQ;
  if (biased > 104) biased = 104;  // guard a corrupted persisted value
  return static_cast<int32_t>(biased) - 48;
}

// Builds the mood ladder's thresholds from the user's settings, clamped
// defensively so evaluate() and creditQualifyingDay() never see an invalid
// configuration -- CompanionSettingsActivity already keeps happyPoints above
// satisfiedPoints and both fields >= 1 on every edit, but this is the one
// place every reader goes through, so it is also the backstop against a
// hand-edited settings.json or a write that reached CrossPointSettings some
// other way (e.g. the web settings API, which clamps each field to its own
// range independently and has no way to enforce the relationship between
// two).
companion::MoodThresholds thresholdsFromSettings() {
  companion::MoodThresholds t;
  t.satisfiedPoints = std::max<uint16_t>(1, SETTINGS.companionSatisfiedPoints);
  t.happyPoints = std::max<uint16_t>(static_cast<uint16_t>(t.satisfiedPoints + 1), SETTINGS.companionHappyPoints);
  t.neglectedDays = std::max<uint8_t>(1, SETTINGS.companionNeglectedDays);
  return t;
}
}  // namespace

companion::CompanionId CompanionTracker::activeId() {
  const uint8_t id = SETTINGS.companionId;
  if (id >= companion::COMPANION_COUNT) return static_cast<companion::CompanionId>(0);
  return static_cast<companion::CompanionId>(id);
}

const char* CompanionTracker::displayName() {
  if (SETTINGS.companionNickname[0] != '\0') return SETTINGS.companionNickname;
  return companion::COMPANION_NAMES[static_cast<size_t>(activeId())];
}

bool CompanionTracker::resolveLocalDayAndMinute(int32_t& outDay, uint16_t& outMinuteOfDay) {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;

  if (!halClock.getUtcDateTime(year, month, day, hour, minute)) return false;

  // Same day-rollover check main.cpp runs once at boot (see its own comment),
  // repeated here so it isn't a one-shot: if the clock wasn't valid yet at
  // that exact moment (still unsynced, or a full power loss reset it), the
  // boot-time check silently skips and nothing else ever retries it, leaving
  // a previous day's completions showing as "today's" until the user happens
  // to open Tasks/Habits/Sync All. This runs on every mood/day resolution
  // instead - i.e. every time Home or the companion's own screen is viewed -
  // so a clock that only becomes valid later still gets the stale data
  // cleared the next time either is looked at.
  if (HABITIFY_HABITS.rolloverIfStale(civil::packDate(year, month, day))) HABITIFY_HABITS.saveToFile();
  TODOIST_TASKS.clearCompletedIfStale(
      civil::dateFromIso(organizerSync::localIsoDateFromUtc(year, month, day, hour, minute).c_str()));

  const int32_t offset = signedUtcOffsetQuarterHours();
  outDay = companion::localDayNumber(year, month, day, hour, minute, offset);
  outMinuteOfDay = companion::localMinuteOfDay(hour, minute, offset);
  return true;
}

bool CompanionTracker::resolveLocalDay(int32_t& outDay) {
  uint16_t ignoredMinute = 0;
  return resolveLocalDayAndMinute(outDay, ignoredMinute);
}

void CompanionTracker::refreshDay() {
  int32_t day = 0;
  uint16_t minuteOfDay = 0;
  if (!resolveLocalDayAndMinute(day, minuteOfDay)) {
    clockValid = false;
    return;
  }
  clockValid = true;
  localDay = day;
  localMinuteOfDay = minuteOfDay;
}

void CompanionTracker::refreshForDisplay() { refreshDay(); }

uint16_t CompanionTracker::liveHabitsCompletedToday() {
  const auto& habits = HABITIFY_HABITS.getHabits();
  return static_cast<uint16_t>(
      std::count_if(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return h.isComplete(); }));
}

void CompanionTracker::recordActivity() {
  // The day can have rolled over since this screen was entered (reading past
  // midnight is a reader concern, but a completion right after waking the
  // device is not), so re-resolve it before crediting.
  refreshDay();

  const uint16_t tasksToday = TODOIST_TASKS.getCompletedToday();
  const uint16_t habitsToday = liveHabitsCompletedToday();

  if (!COMPANION_STATE.recordActivity(localDay, clockValid, tasksToday, habitsToday, thresholdsFromSettings())) return;
  if (!COMPANION_STATE.saveToFile()) {
    LOG_ERR("COMP", "Failed to save companion state");
  }
}

companion::MoodInput CompanionTracker::buildMoodInput(const companion::MoodThresholds& thresholds) const {
  const uint16_t tasksToday = TODOIST_TASKS.getCompletedToday();
  const uint16_t habitsToday = liveHabitsCompletedToday();
  return companion::moodInputFor(COMPANION_STATE.ledger, localDay, clockValid, tasksToday, habitsToday, thresholds);
}

bool CompanionTracker::isWithinSleepWindow() const {
  if (!clockValid) return false;
  const uint16_t start =
      static_cast<uint16_t>(SETTINGS.companionSleepStartHour) * 60 + SETTINGS.companionSleepStartMinute;
  const uint16_t end = static_cast<uint16_t>(SETTINGS.companionSleepEndHour) * 60 + SETTINGS.companionSleepEndMinute;
  return companion::withinSleepWindow(localMinuteOfDay, start, end);
}

companion::Mood CompanionTracker::currentMood() const {
  // Sleeping is checked first, ahead of even Milestone: a beaten record is
  // still worth celebrating, but not while the companion should visually read
  // as asleep. milestoneDay is a day-scoped flag that survives the wait --
  // nothing is lost, only deferred until the companion is awake again with
  // that same local day still current.
  if (isWithinSleepWindow()) return companion::Mood::Sleeping;

  // Nothing left to do: a task list that has been synced and is now empty
  // means everything the user's filter matched has been done, so the
  // companion takes a break. Requires a sync to have happened -- an empty
  // cache on a device that never synced is "no data yet", not "all done".
  // Checked ahead of Milestone on purpose: while the list stays empty this
  // is the mood, even on a record-beating day; the Milestone flag itself
  // (milestoneDay) is untouched and applies again once a task reappears.
  if (TODOIST_TASKS.hasSynced() && TODOIST_TASKS.getTasks().empty()) return companion::Mood::Break;

  const auto thresholds = thresholdsFromSettings();
  const auto in = buildMoodInput(thresholds);
  // A beaten best-day-points record holds the companion at the Milestone mood
  // for the rest of the day it was earned (see CompanionState::milestoneDay),
  // ahead of the usual ladder. Gated on clockValid: without a fresh reading,
  // localDay is whatever the last valid one was, and comparing a stale day
  // against milestoneDay could match (or fail to) by accident.
  if (clockValid && COMPANION_STATE.milestoneDay == localDay) {
    const uint32_t points = static_cast<uint32_t>(in.tasksCompletedToday) + in.habitsCompletedToday;
    // Re-checked against live points rather than trusted as a one-shot flag:
    // normal completions only ever add to today's total, so this is always
    // true for the rest of a normal day, but today's live count can also
    // drop if something completed earlier gets undone (in the Todoist/
    // Habitify app, synced back down) -- at that point the record it
    // claimed is no longer actually true right now, and the ladder should
    // reflect today's live count the same as any other day.
    if (points >= COMPANION_STATE.ledger.bestDayPoints) return companion::Mood::Milestone;
  }
  return companion::evaluate(in, thresholds);
}

uint16_t CompanionTracker::pointsToday() const {
  const auto in = buildMoodInput(thresholdsFromSettings());
  const uint32_t total = static_cast<uint32_t>(in.tasksCompletedToday) + in.habitsCompletedToday;
  return static_cast<uint16_t>(total > UINT16_MAX ? UINT16_MAX : total);
}

std::string CompanionTracker::formatAge(const int32_t activatedDay) {
  if (activatedDay == companion::DayLedger::NEVER) return std::string(tr(STR_COMPANION_AGE_NOT_YET));

  int32_t today = 0;
  if (!resolveLocalDay(today)) return std::string(tr(STR_COMPANION_AGE_NOT_YET));

  // A clock correction could put "today" before the stamped day; floor at 0
  // rather than showing a negative age.
  const int32_t elapsed = today > activatedDay ? today - activatedDay : 0;
  char buf[32];
  if (elapsed < 1) return std::string(tr(STR_COMPANION_AGE_TODAY));
  if (elapsed < 30) {
    snprintf(buf, sizeof(buf), elapsed == 1 ? tr(STR_COMPANION_AGE_DAY) : tr(STR_COMPANION_AGE_DAYS), elapsed);
    return std::string(buf);
  }
  if (elapsed < 365) {
    const int32_t months = elapsed / 30;
    snprintf(buf, sizeof(buf), months == 1 ? tr(STR_COMPANION_AGE_MONTH) : tr(STR_COMPANION_AGE_MONTHS), months);
    return std::string(buf);
  }
  const int32_t years = elapsed / 365;
  snprintf(buf, sizeof(buf), years == 1 ? tr(STR_COMPANION_AGE_YEAR) : tr(STR_COMPANION_AGE_YEARS), years);
  return std::string(buf);
}
