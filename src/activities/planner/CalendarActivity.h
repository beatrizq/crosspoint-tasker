#pragma once
#include <GCalClient.h>

#include <cstddef>

#include "PlannerScreenActivity.h"

/**
 * The Calendar screen: the synced Google Calendar window under a single All tab.
 *
 * Calendar was one tab of the Planner screen and is now its own, and it keeps
 * the one tab so it reads as a sibling of Tasks and Budget rather than as a bare
 * list.
 *
 * It briefly drew a tab per selected calendar. Google returns a calendar's own
 * summary as its display name, and for the primary calendar that summary is the
 * account's email address - so the first tab came out as a wrapped email while
 * the rest were ordinary words, and the bar read as broken rather than as a
 * choice. The events cannot be split per calendar anyway: the fetch merges every
 * calendar into one sorted list and GCalEvent carries no attribution. Doing this
 * properly needs both a label worth reading and an event that knows where it came
 * from, so the tabs are gone until it has them.
 *
 * A single tab has nothing to cycle to, so the base class lets a plain Select on
 * the tab bar sync here instead of asking for a hold.
 */
class CalendarActivity final : public PlannerScreenActivity {
 public:
  enum class Tab : uint8_t { ALL = 0 };
  static constexpr int TAB_COUNT = 1;

  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : PlannerScreenActivity("Calendar", renderer, mappedInput, static_cast<int>(Tab::ALL)) {}

 protected:
  const char* screenTitle() const override;
  homeAppOrder::AppId appId() const override { return homeAppOrder::AppId::Calendar; }
  int tabCount() const override { return TAB_COUNT; }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // Every event carries its date and time on a second line.
  bool rowsHaveSubtitle() const override { return true; }
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::CALENDAR; }

 private:
  void performCalendarSync();
  void formatEventWhen(int index, char* out, size_t outSize) const;
};
