#pragma once

#include <cstdint>

// Tasks-and-habits-driven mood model for the companion. Deliberately free of
// Arduino, FreeRTOS, and HAL includes so the whole decay/credit policy is
// exercised by host unit tests before it ever reaches the device.
namespace companion {

// Milestone, Sleeping, Focus and Break are not ladder outcomes -- evaluate()
// never returns any of them. All four are external overrides callers apply
// ahead of the ladder: Milestone for one paint when the best-ever single-day
// tasks+habits total is beaten (see CompanionState::milestoneDay), the same
// way the old milestone quote used to override which line was said rather
// than which mood earned it; Sleeping for as long as the wall clock sits
// inside the user's configured sleep window, taking priority even over
// Milestone since a sleeping companion should not also be shown celebrating.
// Focus and Break (see src/companion/sprites/pancake.grid for their art) are
// overrides too. Focus is drawn directly by FocusSessionActivity for the
// whole of a focus session, never via CompanionTracker::currentMood().
// Break is what currentMood() returns while the synced task list is empty --
// everything done, nothing left to do -- ahead of Milestone but behind
// Sleeping.
enum class Mood : uint8_t {
  Happy = 0,
  Satisfied = 1,
  Cranky = 2,
  Neglected = 3,
  Milestone = 4,
  Sleeping = 5,
  Focus = 6,
  Break = 7
};

// Tunables kept in one struct so tests can pin behaviour without rebuilding the
// firmware defaults. Tasks and habits are weighted identically -- each
// completed task or completed habit is worth one point -- so either can carry
// the ladder to Happy on its own, or the two can mix freely.
struct MoodThresholds {
  uint16_t happyPoints = 3;      // combined tasks+habits completed today for Happy
  uint16_t satisfiedPoints = 1;  // combined tasks+habits completed today to count as "did something"
  uint8_t neglectedDays = 3;     // quiet days at or above which the mood bottoms out
};

struct MoodInput {
  uint16_t tasksCompletedToday = 0;
  uint16_t habitsCompletedToday = 0;
  // Whole calendar days between the last day that qualified (cleared
  // satisfiedPoints) and today. 0 = qualified today, no qualifying day yet
  // (brand new companion), or a clock correction; 1 = one full day with zero
  // activity; 2 = two, and so on.
  uint16_t daysSinceLastActive = 0;
  // False when the RTC is absent or was never set (see HalClock::getDate).
  // Day arithmetic is meaningless then, so the decay ladder is skipped.
  bool clockValid = false;
};

// Maps today's organizing activity onto one of the four drawn poses.
//
// With a valid clock the ladder is: enough combined points today -> Happy,
// some activity today -> Satisfied, otherwise Cranky immediately and Neglected
// once neglectedDays quiet days have passed. Mood reflects only today's own
// effort -- a streak from a previous day earns nothing today it did not also
// earn today, and there is no same-day grace either: today's live counts are
// re-read on every call, so un-completing something (in the Todoist/Habitify
// app, synced back down) drops the mood the same way it would if it had
// never been done, even if today already cleared the bar earlier. A brand
// new companion (nothing ever qualified) starts at Cranky rather than
// Satisfied: it has done nothing yet either, so there is nothing to grace.
//
// Without a clock, elapsed days cannot be measured, so neglect is unknowable
// and the result never falls below Satisfied. Today's task/habit counts are
// live reads regardless of clock validity, so Happy stays reachable.
Mood evaluate(const MoodInput& in, const MoodThresholds& t = {});

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Integer-only: no <ctime>, no 64-bit division, and valid
// well past any date an e-reader RTC will report.
int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day);

// Whole days from the first date to the second. Negative when the second date
// is earlier, which the caller should treat as a clock that moved backwards.
int32_t daysBetween(int32_t fromYear, uint32_t fromMonth, uint32_t fromDay, int32_t toYear, uint32_t toMonth,
                    uint32_t toDay);

// Local day number (days since 1970-01-01 in the user's own zone) for a UTC
// wall-clock reading. The RTC stores UTC, but a day must roll over at the
// user's midnight, not UTC's -- otherwise a late-evening completion west of
// Greenwich lands on tomorrow and silently breaks a streak.
//
// This one integer is the whole day key: equality means "same day" and
// subtraction gives elapsed days, so no reverse calendar conversion is needed.
// utcOffsetQuarterHours is signed (UTC+0 = 0, UTC-5 = -20, UTC+14 = 56).
int32_t localDayNumber(int32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute,
                       int32_t utcOffsetQuarterHours);

// Local wall-clock minutes since midnight (0..1439) for a UTC wall-clock
// reading, same offset convention as localDayNumber -- that function keeps
// only the day component of this same conversion, this one keeps only the
// intraday remainder.
uint16_t localMinuteOfDay(uint32_t hour, uint32_t minute, int32_t utcOffsetQuarterHours);

// True when `nowMinuteOfDay` falls within [startMinuteOfDay, endMinuteOfDay),
// treating start > end as a window that wraps past midnight (e.g. 22:00 to
// 07:00). start == end means the window never applies -- an all-day window
// would make "awake" meaningless, so equal bounds read as "no window" rather
// than "always asleep".
bool withinSleepWindow(uint16_t nowMinuteOfDay, uint16_t startMinuteOfDay, uint16_t endMinuteOfDay);

/**
 * @brief Day-qualifying and record bookkeeping for the decay ladder and Milestone.
 *
 * Today's task and habit counts each live in their own owning cache
 * (TodoistTaskCache, HabitifyHabitCache), which already resets them daily, so
 * nothing about today's counts is duplicated here. This ledger only remembers
 * the last day that cleared satisfiedPoints (for the decay ladder's
 * daysSinceLastActive) and the highest combined tasks+habits total ever
 * completed in a single day (for Milestone) -- state neither of those caches
 * has any reason to know about.
 */
struct DayLedger {
  // Sentinel for "no day has ever qualified".
  static constexpr int32_t NEVER = INT32_MIN;

  int32_t lastQualifyingDay = NEVER;  // last local day that cleared satisfiedPoints
  // One level of fallback behind lastQualifyingDay -- the day that held that
  // title immediately before the current one took over. Only ever consulted
  // when today is lastQualifyingDay but today's *live* points have since
  // dropped back below satisfiedPoints (something completed earlier was
  // undone in the Todoist/Habitify app and synced back down): without this,
  // daysSinceLastActive would read as 0 (today still "the last qualifying
  // day" on paper) instead of reflecting whatever the last *genuine*
  // qualifying day actually was. One level only -- it does not protect
  // against undoing two qualifying days deep.
  int32_t previousQualifyingDay = NEVER;
  uint16_t bestDayPoints = 0;  // highest combined tasks+habits total ever completed in one day
};

// Re-derives whether `today` has now cleared satisfiedPoints of combined
// tasks+habits effort, updating lastQualifyingDay the first time it does (a
// no-op on a later call the same day), and separately checks whether today's
// live point total is a new all-time high for bestDayPoints. The two are
// independent: unlike lastQualifyingDay, bestDayPoints must keep being
// re-checked on every call the same day, since today's total only grows as
// more is completed and a new record can land on any one of those calls, not
// just the first. Returns true when the ledger changed and the caller should
// persist.
bool creditQualifyingDay(DayLedger& ledger, int32_t today, uint16_t tasksCompletedToday, uint16_t habitsCompletedToday,
                         const MoodThresholds& t = {});

// Derives the mood inputs for `today` from a ledger plus the live counts from
// the task and habit caches. When the clock is invalid, day arithmetic is
// skipped and daysSinceLastActive stays 0.
//
// `t` decides which day daysSinceLastActive is measured against: normally
// lastQualifyingDay, but when today is lastQualifyingDay and today's live
// points no longer clear t.satisfiedPoints (undone since), falls back to
// previousQualifyingDay instead -- see DayLedger's own comment on that field.
MoodInput moodInputFor(const DayLedger& ledger, int32_t today, bool clockValid, uint16_t tasksCompletedToday,
                       uint16_t habitsCompletedToday, const MoodThresholds& t = {});

}  // namespace companion
