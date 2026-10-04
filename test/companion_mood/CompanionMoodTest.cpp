#include <gtest/gtest.h>

#include <set>

#include "Companion/CompanionMood.h"

using companion::Mood;
using companion::MoodInput;
using companion::MoodThresholds;

namespace {

MoodInput withClock(uint16_t tasksToday, uint16_t daysSince) {
  MoodInput in;
  in.tasksCompletedToday = tasksToday;
  in.daysSinceLastActive = daysSince;
  in.clockValid = true;
  return in;
}

}  // namespace

// ---------------------------------------------------------------- mood ladder

TEST(CompanionMood, EnoughTasksIsHappy) {
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.happyPoints, 0)), Mood::Happy);
  EXPECT_EQ(companion::evaluate(withClock(t.amazedPoints - 1, 0)), Mood::Happy);
}

TEST(CompanionMood, ManyMoreTasksIsAmazed) {
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.amazedPoints, 0)), Mood::Amazed);
  EXPECT_EQ(companion::evaluate(withClock(t.amazedPoints + 10, 0)), Mood::Amazed);
}

TEST(CompanionMood, AmazedSitsAboveHappyByDefault) {
  EXPECT_GT(MoodThresholds{}.amazedPoints, MoodThresholds{}.happyPoints);
}

TEST(CompanionMood, AmazedIsReachableWithoutAClock) {
  const MoodThresholds t;
  MoodInput in;
  in.clockValid = false;
  in.tasksCompletedToday = t.amazedPoints;
  EXPECT_EQ(companion::evaluate(in), Mood::Amazed);
}

TEST(CompanionMood, AmazedThresholdIsConfigurable) {
  MoodThresholds t;
  t.amazedPoints = 12;
  EXPECT_EQ(companion::evaluate(withClock(11, 0), t), Mood::Happy);
  EXPECT_EQ(companion::evaluate(withClock(12, 0), t), Mood::Amazed);
}

TEST(CompanionMood, JustUnderHappyIsStillSatisfied) {
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.happyPoints - 1, 0)), Mood::Satisfied);
  EXPECT_EQ(companion::evaluate(withClock(t.happyPoints, 0)), Mood::Happy);
}

TEST(CompanionMood, AnyCompletionTodayIsSatisfied) {
  EXPECT_EQ(companion::evaluate(withClock(1, 0)), Mood::Satisfied);
}

TEST(CompanionMood, NoGraceForYesterdaysActivity) {
  // Nothing done today: mood decays immediately, with no carry-over credit
  // for whatever happened on a previous day.
  EXPECT_EQ(companion::evaluate(withClock(0, 1)), Mood::Cranky);
}

TEST(CompanionMood, StaysCrankyUntilNeglectedDaysIsReached) {
  EXPECT_EQ(companion::evaluate(withClock(0, 2)), Mood::Cranky);
}

TEST(CompanionMood, ThreeQuietDaysIsNeglected) {
  EXPECT_EQ(companion::evaluate(withClock(0, 3)), Mood::Neglected);
  EXPECT_EQ(companion::evaluate(withClock(0, 90)), Mood::Neglected);
}

TEST(CompanionMood, NeglectIsRecoverableTheSameDay) {
  // The chosen design has no death state: enough activity from the floor goes
  // straight back to the top.
  EXPECT_EQ(companion::evaluate(withClock(0, 400)), Mood::Neglected);
  const MoodThresholds t;
  EXPECT_EQ(companion::evaluate(withClock(t.happyPoints, 400)), Mood::Happy);
}

TEST(CompanionMood, ThresholdsAreConfigurable) {
  MoodThresholds relaxed;
  relaxed.happyPoints = 1;
  relaxed.neglectedDays = 7;
  EXPECT_EQ(companion::evaluate(withClock(1, 0), relaxed), Mood::Happy);
  EXPECT_EQ(companion::evaluate(withClock(0, 5), relaxed), Mood::Cranky);
  EXPECT_EQ(companion::evaluate(withClock(0, 7), relaxed), Mood::Neglected);
}

// -------------------------------------------------------- clockless fallback

TEST(CompanionMood, WithoutClockNeverFallsBelowSatisfied) {
  MoodInput in;
  in.clockValid = false;
  in.tasksCompletedToday = 0;
  in.daysSinceLastActive = 999;  // garbage without a clock; must be ignored
  EXPECT_EQ(companion::evaluate(in), Mood::Satisfied);
}

TEST(CompanionMood, WithoutClockHappyStillReachable) {
  // Today's task count is a live read independent of the RTC, so
  // Happy must stay reachable even when the clock is invalid.
  const MoodThresholds t;
  MoodInput in;
  in.clockValid = false;
  in.tasksCompletedToday = t.happyPoints;
  EXPECT_EQ(companion::evaluate(in), Mood::Happy);
}

// ------------------------------------------------------------ calendar maths

TEST(CompanionCalendar, EpochIsDayZero) { EXPECT_EQ(companion::daysFromCivil(1970, 1, 1), 0); }

TEST(CompanionCalendar, KnownDates) {
  EXPECT_EQ(companion::daysFromCivil(1969, 12, 31), -1);
  EXPECT_EQ(companion::daysFromCivil(1970, 1, 2), 1);
  EXPECT_EQ(companion::daysFromCivil(2000, 3, 1), 11017);
  EXPECT_EQ(companion::daysFromCivil(2026, 8, 18), 20683);
}

TEST(CompanionCalendar, ConsecutiveDaysAlwaysDifferByOne) {
  // Walks several years day by day, which catches month-length and leap-year
  // errors that spot checks miss.
  static constexpr uint8_t kLengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int32_t previous = companion::daysFromCivil(2023, 1, 1);
  for (int32_t y = 2023; y <= 2029; ++y) {
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    for (uint32_t m = 1; m <= 12; ++m) {
      const uint32_t last = kLengths[m - 1] + (leap && m == 2 ? 1u : 0u);
      for (uint32_t d = 1; d <= last; ++d) {
        if (y == 2023 && m == 1 && d == 1) continue;
        const int32_t current = companion::daysFromCivil(y, m, d);
        EXPECT_EQ(current - previous, 1) << y << "-" << m << "-" << d;
        previous = current;
      }
    }
  }
}

TEST(CompanionCalendar, LeapDayIsHandled) {
  EXPECT_EQ(companion::daysBetween(2024, 2, 28, 2024, 3, 1), 2);  // 2024 is a leap year
  EXPECT_EQ(companion::daysBetween(2023, 2, 28, 2023, 3, 1), 1);
  EXPECT_EQ(companion::daysBetween(2100, 2, 28, 2100, 3, 1), 1);  // century, not a leap year
}

TEST(CompanionCalendar, SpansMonthAndYearBoundaries) {
  EXPECT_EQ(companion::daysBetween(2025, 12, 31, 2026, 1, 1), 1);
  EXPECT_EQ(companion::daysBetween(2026, 1, 31, 2026, 2, 1), 1);
  EXPECT_EQ(companion::daysBetween(2026, 1, 1, 2027, 1, 1), 365);
}

TEST(CompanionCalendar, BackwardsClockYieldsNegative) {
  // Caller treats a negative span as "the clock moved backwards", not as neglect.
  EXPECT_LT(companion::daysBetween(2026, 8, 18, 2026, 8, 17), 0);
}

// ------------------------------------------------------------ local day key

TEST(CompanionLocalDay, MatchesUtcAtZeroOffset) {
  EXPECT_EQ(companion::localDayNumber(1970, 1, 1, 0, 0, 0), 0);
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 12, 0, 0), companion::daysFromCivil(2026, 8, 18));
}

TEST(CompanionLocalDay, EveningWestOfGreenwichStaysOnTheSameLocalDay) {
  // 02:30 UTC on the 19th is 21:30 on the 18th in UTC-5. Activity then must
  // still count as the 18th, or the streak breaks at the wrong moment.
  const int32_t offsetMinus5 = -20;  // quarter-hours
  EXPECT_EQ(companion::localDayNumber(2026, 8, 19, 2, 30, offsetMinus5), companion::daysFromCivil(2026, 8, 18));
}

TEST(CompanionLocalDay, MorningEastOfGreenwichRollsForward) {
  // 22:00 UTC on the 18th is 07:00 on the 19th in UTC+9.
  const int32_t offsetPlus9 = 36;
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 22, 0, offsetPlus9), companion::daysFromCivil(2026, 8, 19));
}

TEST(CompanionLocalDay, HandlesExtremeOffsets) {
  const int32_t offsetPlus14 = 56;
  const int32_t offsetMinus12 = -48;
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 12, 0, offsetPlus14), companion::daysFromCivil(2026, 8, 19));
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 6, 0, offsetMinus12), companion::daysFromCivil(2026, 8, 17));
}

TEST(CompanionLocalDay, QuarterHourOffsetsWork) {
  // Nepal is UTC+05:45.
  const int32_t offsetPlus545 = 23;
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 18, 20, offsetPlus545), companion::daysFromCivil(2026, 8, 19));
  EXPECT_EQ(companion::localDayNumber(2026, 8, 18, 18, 10, offsetPlus545), companion::daysFromCivil(2026, 8, 18));
}

TEST(CompanionLocalDay, ConsecutiveLocalDaysDifferByOne) {
  const int32_t offset = -20;
  const int32_t d1 = companion::localDayNumber(2026, 12, 31, 20, 0, offset);
  const int32_t d2 = companion::localDayNumber(2027, 1, 1, 20, 0, offset);
  EXPECT_EQ(d2 - d1, 1);
}

// ------------------------------------------------------------- day ledger

using companion::DayLedger;

namespace {
constexpr int32_t kDay = 20000;  // arbitrary local day number
}  // namespace

TEST(CompanionLedger, FirstQualifyingDaySetsLastQualifyingDay) {
  DayLedger led;
  EXPECT_TRUE(companion::creditQualifyingDay(led, kDay, 1));
  EXPECT_EQ(led.lastQualifyingDay, kDay);
}

TEST(CompanionLedger, MoreTasksLaterTheSameDayReportsNoChange) {
  // The ledger only remembers which day qualified, so a higher total the same
  // day changes nothing and the caller should not persist.
  DayLedger led;
  EXPECT_TRUE(companion::creditQualifyingDay(led, kDay, 1));
  EXPECT_FALSE(companion::creditQualifyingDay(led, kDay, 5));
  EXPECT_EQ(led.lastQualifyingDay, kDay);
}

TEST(CompanionLedger, ZeroActivityDoesNotQualifyTheDay) {
  DayLedger led;
  EXPECT_FALSE(companion::creditQualifyingDay(led, kDay, 0));
  EXPECT_EQ(led.lastQualifyingDay, DayLedger::NEVER);
}

TEST(CompanionLedger, MoodInputReportsTodaysCounts) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  const auto in = companion::moodInputFor(led, kDay, true, 3);
  EXPECT_EQ(in.tasksCompletedToday, 3);
  EXPECT_EQ(in.daysSinceLastActive, 0);
  EXPECT_EQ(companion::evaluate(in), companion::Mood::Happy);
}

TEST(CompanionLedger, MoodInputReflectsLiveCountsOnANewDay) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  // A new day: the caller passes 0 (yesterday's completions do not carry),
  // and the ledger reports one day since the last qualifying day. With no
  // activity credited today, that is already enough to read as Cranky -
  // yesterday's streak buys no grace today.
  const auto in = companion::moodInputFor(led, kDay + 1, true, 0);
  EXPECT_EQ(in.tasksCompletedToday, 0);
  EXPECT_EQ(in.daysSinceLastActive, 1);
  EXPECT_EQ(companion::evaluate(in), companion::Mood::Cranky);
}

TEST(CompanionLedger, MoodDecaysAcrossQuietDays) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 1, true, 0)), companion::Mood::Cranky);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 2, true, 0)), companion::Mood::Cranky);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 3, true, 0)), companion::Mood::Neglected);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 60, true, 0)), companion::Mood::Neglected);
}

TEST(CompanionLedger, FreshCompanionStartsCrankyNotSatisfied) {
  // Nothing has ever qualified, so there is nothing earned to grace: a brand
  // new companion starts the ladder at Cranky, the same as any other day
  // with zero activity, rather than getting a free Satisfied on day one.
  const DayLedger led;
  const auto in = companion::moodInputFor(led, kDay, true, 0);
  EXPECT_EQ(in.daysSinceLastActive, 0);
  EXPECT_EQ(companion::evaluate(in), companion::Mood::Cranky);
}

TEST(CompanionLedger, BackwardsClockDoesNotReadAsNeglect) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  const auto in = companion::moodInputFor(led, kDay - 5, true, 0);
  EXPECT_EQ(in.daysSinceLastActive, 0);
  EXPECT_NE(companion::evaluate(in), companion::Mood::Neglected);
}

TEST(CompanionLedger, ClocklessModeUsesLiveCountsOnly) {
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  const auto idle = companion::moodInputFor(led, kDay + 99, false, 0);
  EXPECT_FALSE(idle.clockValid);
  EXPECT_EQ(companion::evaluate(idle), companion::Mood::Satisfied);

  const MoodThresholds t;
  const auto active = companion::moodInputFor(led, kDay + 99, false, t.happyPoints);
  EXPECT_EQ(active.tasksCompletedToday, t.happyPoints);
  EXPECT_EQ(companion::evaluate(active), companion::Mood::Happy);
}

// ------------------------------------------------------------ reachability
// Every tier must be reachable by a plausible sequence of real behaviour, and
// each must be exited again. A mood nobody can reach is dead art.

TEST(CompanionReachability, EveryMoodOccursOverALivedTimeline) {
  DayLedger led;
  std::set<Mood> seen;
  const auto observe = [&](int32_t day, uint16_t tasksToday) {
    seen.insert(companion::evaluate(companion::moodInputFor(led, day, true, tasksToday)));
  };

  int32_t day = kDay;
  // Two solid days of activity.
  companion::creditQualifyingDay(led, day, 3);
  observe(day, 3);      // enough today
  observe(day, 10);     // a lot today
  observe(day + 1, 0);  // nothing done yet today: no grace from yesterday
  companion::creditQualifyingDay(led, day + 1, 1);
  observe(day + 1, 1);

  // Then it goes quiet.
  observe(day + 2, 0);  // one day after the last qualifying day
  observe(day + 3, 0);  // a full day skipped
  observe(day + 4, 0);  // and another
  observe(day + 9, 0);  // long gone

  EXPECT_EQ(seen.count(Mood::Amazed), 1u) << "Amazed unreachable";
  EXPECT_EQ(seen.count(Mood::Happy), 1u) << "Happy unreachable";
  EXPECT_EQ(seen.count(Mood::Satisfied), 1u) << "Satisfied unreachable";
  EXPECT_EQ(seen.count(Mood::Cranky), 1u) << "Cranky unreachable";
  EXPECT_EQ(seen.count(Mood::Neglected), 1u) << "Neglected unreachable";
  EXPECT_EQ(seen.size(), 5u);
}

TEST(CompanionReachability, NoGraceDayAfterQualifying) {
  // Zero activity the day right after a qualifying day must already read as
  // Cranky, not Satisfied - there is no one-day grace carried over. Cranky then
  // holds through the rest of the neglectedDays window before bottoming out.
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 0, true, 3)), Mood::Happy);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 1, true, 0)), Mood::Cranky);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 2, true, 0)), Mood::Cranky);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay + 3, true, 0)), Mood::Neglected);
}

TEST(CompanionReachability, SameDayRegressionDropsBackToTheLadder) {
  // Today already cleared the bar (crediting a task completion), but
  // live points have since dropped -- the only way that happens is
  // un-completing something and syncing the lower count back down. The mood
  // must reflect that live drop, not hold at Satisfied on the strength of a
  // bar that was cleared earlier the same day and no longer is.
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);
  ASSERT_EQ(companion::evaluate(companion::moodInputFor(led, kDay, true, 3)), Mood::Happy);
  EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, kDay, true, 0)), Mood::Cranky)
      << "live points back to zero the same day must not stay graced at Satisfied";
}

TEST(CompanionReachability, SameDayRegressionFallsBackToThePreviousGenuineDay) {
  // Genuinely qualified several days ago, quiet since, then briefly
  // qualified today before undoing it. daysSinceLastActive must reflect the
  // older genuine day (via DayLedger::previousQualifyingDay), not read as
  // "0 days since active" just because today's now-undone credit is still
  // the most recently recorded lastQualifyingDay.
  DayLedger led;
  companion::creditQualifyingDay(led, kDay, 3);                     // genuine activity, several days ago
  companion::creditQualifyingDay(led, kDay + 5, 2);                 // briefly qualifies today...
  const auto in = companion::moodInputFor(led, kDay + 5, true, 0);  // ...then undone, synced back to 0
  EXPECT_EQ(in.daysSinceLastActive, 5) << "should fall back to the day before today's now-invalid credit";
  EXPECT_EQ(companion::evaluate(in), Mood::Neglected);
}

TEST(CompanionReachability, EveryTierIsExitableBackToTheTop) {
  // Recovery must work from the floor, with no penalty box. Starts at one quiet
  // day: with zero the companion is already Happy on day 0's credit, so
  // there is nothing to recover from.
  const MoodThresholds t;
  for (int32_t quietDays : {1, 2, 3, 50, 5000}) {
    DayLedger led;
    companion::creditQualifyingDay(led, kDay, t.happyPoints);
    const int32_t today = kDay + quietDays;
    ASSERT_NE(companion::evaluate(companion::moodInputFor(led, today, true, 0)), Mood::Happy) << quietDays;

    companion::creditQualifyingDay(led, today, t.happyPoints);
    EXPECT_EQ(companion::evaluate(companion::moodInputFor(led, today, true, t.happyPoints)), Mood::Happy)
        << "could not recover after " << quietDays << " quiet days";
  }
}

TEST(CompanionReachability, ThresholdBoundariesAreExact) {
  const MoodThresholds t;
  ASSERT_GT(t.happyPoints, t.satisfiedPoints) << "Happy must sit above Satisfied or a tier is unreachable";

  EXPECT_EQ(companion::evaluate(withClock(t.happyPoints - 1, 0)), Mood::Satisfied) << "one point short of Happy";
  EXPECT_EQ(companion::evaluate(withClock(t.happyPoints, 0)), Mood::Happy) << "exactly Happy";

  DayLedger low;
  EXPECT_FALSE(companion::creditQualifyingDay(low, kDay, t.satisfiedPoints - 1))
      << "below satisfiedPoints must not qualify the day";
  EXPECT_EQ(low.lastQualifyingDay, DayLedger::NEVER);
  EXPECT_TRUE(companion::creditQualifyingDay(low, kDay, t.satisfiedPoints)) << "exactly satisfiedPoints must qualify";
  EXPECT_EQ(low.lastQualifyingDay, kDay);
}

TEST(CompanionReachability, EveryMoodHasArtAndIsIndexable) {
  // The enum is used to index the generated sprite and quote tables, so the
  // values must stay contiguous from zero with no gaps. Sleeping, Focus
  // and Break are never returned by evaluate() -- they are external overrides
  // -- but still need an index into those same tables, so they belong in this
  // contiguous run too.
  EXPECT_EQ(static_cast<int>(Mood::Happy), 0);
  EXPECT_EQ(static_cast<int>(Mood::Satisfied), 1);
  EXPECT_EQ(static_cast<int>(Mood::Cranky), 2);
  EXPECT_EQ(static_cast<int>(Mood::Neglected), 3);
  EXPECT_EQ(static_cast<int>(Mood::Amazed), 4);
  EXPECT_EQ(static_cast<int>(Mood::Sleeping), 5);
}

// -------------------------------------------------------------- sleep window

TEST(CompanionSleepWindow, LocalMinuteOfDayMatchesUtcAtZeroOffset) {
  EXPECT_EQ(companion::localMinuteOfDay(0, 0, 0), 0);
  EXPECT_EQ(companion::localMinuteOfDay(23, 59, 0), 1439);
  EXPECT_EQ(companion::localMinuteOfDay(12, 30, 0), 12 * 60 + 30);
}

TEST(CompanionSleepWindow, LocalMinuteOfDayWrapsForwardAndBackward) {
  // 23:30 UTC in UTC+1 is 00:30 the next local day.
  EXPECT_EQ(companion::localMinuteOfDay(23, 30, 4), 30);
  // 00:30 UTC in UTC-1 is 23:30 the previous local day.
  EXPECT_EQ(companion::localMinuteOfDay(0, 30, -4), 23 * 60 + 30);
}

TEST(CompanionSleepWindow, SameDayWindow) {
  // 01:00-06:00, entirely within one calendar day.
  const uint16_t start = 60, end = 360;
  EXPECT_FALSE(companion::withinSleepWindow(0, start, end)) << "before the window";
  EXPECT_TRUE(companion::withinSleepWindow(60, start, end)) << "start is inclusive";
  EXPECT_TRUE(companion::withinSleepWindow(200, start, end));
  EXPECT_FALSE(companion::withinSleepWindow(360, start, end)) << "end is exclusive";
  EXPECT_FALSE(companion::withinSleepWindow(1000, start, end)) << "after the window";
}

TEST(CompanionSleepWindow, OvernightWindowWrapsPastMidnight) {
  // 22:00-07:00.
  const uint16_t start = 22 * 60, end = 7 * 60;
  EXPECT_TRUE(companion::withinSleepWindow(22 * 60, start, end)) << "start is inclusive";
  EXPECT_TRUE(companion::withinSleepWindow(23 * 60 + 30, start, end)) << "late evening";
  EXPECT_TRUE(companion::withinSleepWindow(0, start, end)) << "midnight";
  EXPECT_TRUE(companion::withinSleepWindow(6 * 60 + 59, start, end)) << "just before end";
  EXPECT_FALSE(companion::withinSleepWindow(7 * 60, start, end)) << "end is exclusive";
  EXPECT_FALSE(companion::withinSleepWindow(12 * 60, start, end)) << "midday, clearly awake";
}

TEST(CompanionSleepWindow, EqualStartAndEndMeansNoWindow) {
  EXPECT_FALSE(companion::withinSleepWindow(0, 0, 0));
  EXPECT_FALSE(companion::withinSleepWindow(500, 500, 500));
  EXPECT_FALSE(companion::withinSleepWindow(1439, 500, 500));
}
