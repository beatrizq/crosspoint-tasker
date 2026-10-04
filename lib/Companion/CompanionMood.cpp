#include "CompanionMood.h"

namespace companion {

Mood evaluate(const MoodInput& in, const MoodThresholds& t) {
  const uint32_t points = in.tasksCompletedToday;
  if (points >= t.amazedPoints) return Mood::Amazed;
  if (points >= t.happyPoints) return Mood::Happy;

  // No clock: elapsed days are unknowable, so decay cannot be justified.
  // Satisfied is the floor rather than punishing a user whose RTC was never set.
  if (!in.clockValid) return Mood::Satisfied;

  if (points >= t.satisfiedPoints) return Mood::Satisfied;
  // Below the bar with a valid clock: no grace, same day or otherwise. Below
  // this point in the ladder, live points are what they are right now --
  // whether that is because nothing has been done yet today, or because
  // today did clear the bar earlier and something was since undone (synced
  // back down from the Todoist app) -- the mood reflects today's
  // current effort, not a high-water mark from earlier in the day.
  if (in.daysSinceLastActive < t.neglectedDays) return Mood::Cranky;
  return Mood::Neglected;
}

int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day) {
  // Shift the era so March starts the year, which makes the leap-day the last
  // day of the cycle and removes every February special case.
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(year - era * 400);                   // [0, 399]
  const uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                     // [0, 146096]
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

int32_t daysBetween(int32_t fromYear, uint32_t fromMonth, uint32_t fromDay, int32_t toYear, uint32_t toMonth,
                    uint32_t toDay) {
  return daysFromCivil(toYear, toMonth, toDay) - daysFromCivil(fromYear, fromMonth, fromDay);
}

int32_t localDayNumber(int32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute,
                       int32_t utcOffsetQuarterHours) {
  static constexpr int32_t MINUTES_PER_DAY = 1440;
  const int32_t utcMinutes = daysFromCivil(year, month, day) * MINUTES_PER_DAY + static_cast<int32_t>(hour) * 60 +
                             static_cast<int32_t>(minute);
  const int32_t localMinutes = utcMinutes + utcOffsetQuarterHours * 15;
  // Floor division: C++ truncates toward zero, which would put the pre-epoch
  // side of midnight on the wrong day.
  return localMinutes >= 0 ? localMinutes / MINUTES_PER_DAY
                           : -(((-localMinutes) + MINUTES_PER_DAY - 1) / MINUTES_PER_DAY);
}

uint16_t localMinuteOfDay(const uint32_t hour, const uint32_t minute, const int32_t utcOffsetQuarterHours) {
  static constexpr int32_t MINUTES_PER_DAY = 1440;
  const int32_t utcMinutes = static_cast<int32_t>(hour) * 60 + static_cast<int32_t>(minute);
  int32_t local = (utcMinutes + utcOffsetQuarterHours * 15) % MINUTES_PER_DAY;
  if (local < 0) local += MINUTES_PER_DAY;
  return static_cast<uint16_t>(local);
}

bool withinSleepWindow(const uint16_t nowMinuteOfDay, const uint16_t startMinuteOfDay, const uint16_t endMinuteOfDay) {
  if (startMinuteOfDay == endMinuteOfDay) return false;
  if (startMinuteOfDay < endMinuteOfDay) return nowMinuteOfDay >= startMinuteOfDay && nowMinuteOfDay < endMinuteOfDay;
  return nowMinuteOfDay >= startMinuteOfDay || nowMinuteOfDay < endMinuteOfDay;
}

bool creditQualifyingDay(DayLedger& ledger, const int32_t today, const uint16_t tasksCompletedToday,
                         const MoodThresholds& t) {
  bool changed = false;

  const uint32_t points = tasksCompletedToday;

  // First completion that clears the bar today: mark today as the last
  // qualifying day, and shift whatever held that title down into
  // previousQualifyingDay first -- the one level of fallback moodInputFor()
  // uses if today's own credit is later undone. A later call the same day is
  // a no-op here -- it is already today, so nothing to shift.
  if (points >= t.satisfiedPoints && ledger.lastQualifyingDay != today) {
    ledger.previousQualifyingDay = ledger.lastQualifyingDay;
    ledger.lastQualifyingDay = today;
    changed = true;
  }

  return changed;
}

MoodInput moodInputFor(const DayLedger& ledger, const int32_t today, const bool clockValid,
                       const uint16_t tasksCompletedToday, const MoodThresholds& t) {
  MoodInput in;
  in.clockValid = clockValid;
  in.tasksCompletedToday = tasksCompletedToday;

  if (!clockValid) return in;

  // today is on record as the last qualifying day, but its own live points
  // no longer clear the bar -- something completed earlier today was undone
  // (in the Todoist app, synced back down). today's credit no
  // longer counts, so fall back to whichever day held the title before it.
  const uint32_t points = tasksCompletedToday;
  const bool todayStillQualifies = points >= t.satisfiedPoints;
  const int32_t effectiveLastQualifyingDay = (!todayStillQualifies && ledger.lastQualifyingDay == today)
                                                 ? ledger.previousQualifyingDay
                                                 : ledger.lastQualifyingDay;

  if (effectiveLastQualifyingDay == DayLedger::NEVER) {
    // Never qualified (or today's only credit was just undone with nothing
    // genuine before it): a brand new companion has nothing to have
    // neglected yet either.
    in.daysSinceLastActive = 0;
    return in;
  }

  const int32_t elapsed = today - effectiveLastQualifyingDay;
  // A clock corrected backwards yields a negative span; treat it as "today"
  // rather than letting a wild value read as neglect.
  in.daysSinceLastActive = elapsed <= 0 ? 0 : static_cast<uint16_t>(elapsed > UINT16_MAX ? UINT16_MAX : elapsed);
  return in;
}

}  // namespace companion
