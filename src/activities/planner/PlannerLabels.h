#pragma once
#include <CivilTime.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

/**
 * Date labels shared by the Tasks, Calendar and Budget screens.
 *
 * They lived in PlannerActivity.cpp while those three were tabs of one
 * screen. Splitting them apart did not make the labels screen-specific: all
 * three date their header the same way on purpose, and three formats across
 * three sibling screens would read as an inconsistency rather than as a
 * distinction.
 */
namespace planner {

// Abbreviated weekday and month names, indexed by civil::weekdayFromDate (0 =
// Sunday) and month-1. Deliberately not translated: they are drawn in a narrow
// header where a long localised name would push the rest of the line off, and
// three-letter forms read the same across the Latin-script languages the device
// ships. Static const so they live in flash, not DRAM.
static const char* const WEEKDAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTH_NAMES[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// "Mon 17 Aug", or "--" when the date is unknown.
inline void formatDayLabel(const uint16_t date, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  const uint8_t month = civil::monthFromDate(date);
  if (date == civil::NO_DATE || month == 0) {
    snprintf(out, outSize, "--");
    return;
  }
  snprintf(out, outSize, "%s %u %s", WEEKDAY_NAMES[civil::weekdayFromDate(date) % 7],
           static_cast<unsigned>(civil::dayOfMonthFromDate(date)), MONTH_NAMES[(month - 1) % 12]);
}

// "Aug 2026", or "--" when the month is unknown. The Budget screen's amounts are
// month-scoped, so the month is what dates that list the way a day dates the
// other two.
inline void formatMonthLabel(const uint16_t date, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  const uint8_t month = civil::monthFromDate(date);
  if (date == civil::NO_DATE || month == 0) {
    snprintf(out, outSize, "--");
    return;
  }
  // The year is taken off the ISO form rather than unpacked separately; the
  // civil helpers expose the month and day but not the year on its own.
  char iso[11];
  civil::isoFromDate(date, iso, sizeof(iso));
  snprintf(out, outSize, "%s %.4s", MONTH_NAMES[(month - 1) % 12], iso);
}

}  // namespace planner
