#include "CalendarActivity.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>
#include <vector>

#include "PlannerLabels.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/PlannerSync.h"
#include "util/TaskWatchdog.h"

void CalendarActivity::loadCaches() { GCAL_EVENTS.loadFromFile(); }

const char* CalendarActivity::screenTitle() const { return homeAppOrder::displayName(homeAppOrder::AppId::Calendar); }

const char* CalendarActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::ALL:
      return tr(STR_CALENDAR_TAB_ALL);
  }
  return "";
}

// -- rows -------------------------------------------------------------------

int CalendarActivity::rowCount() const { return static_cast<int>(GCAL_EVENTS.getEvents().size()); }

void CalendarActivity::drawRow(const RowLayout& layout) const {
  const auto& events = GCAL_EVENTS.getEvents();
  if (layout.index < 0 || static_cast<size_t>(layout.index) >= events.size()) return;

  const auto shownTitle =
      renderer.truncatedText(layout.titleFont, events[static_cast<size_t>(layout.index)].summary.c_str(), layout.width);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shownTitle.c_str(), layout.ink);

  char when[48];
  formatEventWhen(layout.index, when, sizeof(when));
  const auto shownWhen = renderer.truncatedText(layout.subtitleFont, when, layout.width);
  const int whenY = layout.textY + renderer.getLineHeight(layout.titleFont);
  renderer.drawText(layout.subtitleFont, layout.x, whenY, shownWhen.c_str(), layout.ink);
  // Greyed, so the date stays subordinate to the event.
  dimText(layout.x, whenY, layout.subtitleFont, shownWhen.c_str(), layout.ink);
}

void CalendarActivity::formatStatus(char* out, const size_t outSize) const {
  if (!GCAL_EVENTS.hasSynced()) {
    snprintf(out, outSize, "%s", tr(STR_GCAL_NEVER_SYNCED));
    return;
  }
  char date[16];
  planner::formatDayLabel(GCAL_EVENTS.getSyncDate(), date, sizeof(date));
  snprintf(out, outSize, "%s", date);
}

const char* CalendarActivity::emptyMessage() const {
  return GCAL_EVENTS.hasSynced() ? tr(STR_GCAL_NO_EVENTS) : tr(STR_GCAL_NEVER_SYNCED);
}

const char* CalendarActivity::syncingMessage() const { return tr(STR_GCAL_SYNCING); }

// -- sync -------------------------------------------------------------------

void CalendarActivity::startSync() {
  if (!GCAL_STORE.hasClientCredentials() || !GCAL_STORE.isLinked()) {
    failSync(tr(STR_GCAL_NOT_LINKED));
    return;
  }
  if (GCAL_STORE.getSelectedCalendars().empty()) {
    failSync(tr(STR_GCAL_NO_CALENDARS));
    return;
  }
  runSync([this] { performCalendarSync(); });
}

void CalendarActivity::performCalendarSync() {
  // The requests and the cache update live in plannerSync so the home screen's
  // sync-everything can drive the same sequence over one Wi-Fi association.
  const char* failure = plannerSync::run(plannerSync::Service::Calendar);

  // Drop the radio before repainting; the full teardown happens on the silent
  // reboot in onExit().
  tearDownRadio();
  finishSync(failure);
}

void CalendarActivity::formatEventWhen(const int index, char* out, const size_t outSize) const {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  const auto& events = GCAL_EVENTS.getEvents();
  if (index < 0 || static_cast<size_t>(index) >= events.size()) return;
  const GCalEvent& event = events[static_cast<size_t>(index)];

  if (civil::monthFromDate(event.date) == 0) return;

  char when[16];
  planner::formatDayLabel(event.date, when, sizeof(when));

  if (event.isAllDay()) {
    snprintf(out, outSize, "%s  %s", when, tr(STR_GCAL_ALL_DAY));
    return;
  }
  // An end time is not guaranteed, and a zero-length event reads better without
  // a range than as "09:30-09:30".
  if (event.endMin == civil::NO_TIME || event.endMin == event.startMin) {
    snprintf(out, outSize, "%s  %02u:%02u", when, static_cast<unsigned>(event.startMin / 60),
             static_cast<unsigned>(event.startMin % 60));
    return;
  }
  snprintf(out, outSize, "%s  %02u:%02u-%02u:%02u", when, static_cast<unsigned>(event.startMin / 60),
           static_cast<unsigned>(event.startMin % 60), static_cast<unsigned>(event.endMin / 60),
           static_cast<unsigned>(event.endMin % 60));
}
