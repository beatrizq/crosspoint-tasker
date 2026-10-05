#include "OrganizerSync.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalClient.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <TodoistClient.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>
#include <esp_sntp.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "companion/CompanionTracker.h"
#include "util/HomeAppOrder.h"
#include "util/TaskWatchdog.h"

namespace organizerSync {
namespace {

// SNTP poll: 100ms x 50 = 5s, matching HalClock::syncFromNTP().
constexpr int NTP_POLL_ATTEMPTS = 50;

// Later of two "YYYY-MM-DD" dates. Today only ever moves forward, so picking the
// newest of the available sources is what keeps a partial one from regressing.
// ISO dates order correctly as plain strings, and "" loses to any real date.
const std::string& laterDate(const std::string& a, const std::string& b) { return b > a ? b : a; }

// TodoistCompletedCountParser::ItemSink for the completed-tasks fetch below:
// collects each item as the response streams in, capped the same way the cache
// caps its own entries so a busy day never grows this past what
// setCompletedToday would keep anyway.
struct CompletedCollector {
  std::vector<TodoistCompletedLogEntry>* out;
};

void collectCompletedItem(void* ctx, const char* id, const char* content) {
  auto* collector = static_cast<CompletedCollector*>(ctx);
  if (collector->out->size() >= TodoistTaskCache::MAX_COMPLETED_TODAY_TITLES) return;
  TodoistCompletedLogEntry entry;
  entry.title = content;
  entry.taskId = id;
  entry.pending = false;
  collector->out->push_back(std::move(entry));
}

/**
 * Today from NTP, in the device's configured timezone.
 *
 * Most boards (X3/X4 included) have no RTC. NTP is the only source that knows
 * SETTINGS.clockUtcOffsetQ, so it is the only one that yields the device's own
 * local date; the response Date header is the fallback, and is GMT.
 */
bool resolveTodayDate(std::string& outDate) {
  outDate.clear();
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < NTP_POLL_ATTEMPTS && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED; i++) {
    delay(100);
    resetTaskWatchdogIfSubscribed();
  }
  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    // Not an error on its own; the response header usually supplies the date.
    LOG_DBG("OSYNC", "NTP sync timed out; relying on the response date");
    return false;
  }

  // Boards that do have an RTC get it set from the same SNTP result.
  if (halClock.isAvailable()) halClock.syncFromNTP();

  uint8_t offsetQ = SETTINGS.clockUtcOffsetQ;
  if (offsetQ > 104) offsetQ = 104;  // clamp a corrupt persisted value to UTC+14
  const time_t local = time(nullptr) + (static_cast<int>(offsetQ) - 48) * 15 * 60;
  struct tm timeinfo;
  gmtime_r(&local, &timeinfo);

  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  outDate = buf;
  return true;
}

// -- per-service error text -------------------------------------------------

const char* taskErrorText(const TodoistClient::Error error) {
  switch (error) {
    case TodoistClient::NO_TOKEN:
      return tr(STR_TODOIST_NO_TOKEN);
    case TodoistClient::AUTH_FAILED:
      return tr(STR_TODOIST_INVALID_TOKEN);
    case TodoistClient::SERVER_ERROR:
      return tr(STR_TODOIST_SERVER_ERROR);
    case TodoistClient::PARSE_ERROR:
      return tr(STR_TODOIST_BAD_RESPONSE);
    case TodoistClient::INVALID_FILTER:
      return tr(STR_TODOIST_INVALID_FILTER);
    case TodoistClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

const char* calendarErrorText(const GCalClient::Error error) {
  switch (error) {
    case GCalClient::NO_TOKEN:
      return tr(STR_GCAL_NOT_LINKED);
    case GCalClient::AUTH_FAILED:
      return tr(STR_GCAL_RELINK_NEEDED);
    case GCalClient::SERVER_ERROR:
      return tr(STR_GCAL_SERVER_ERROR);
    case GCalClient::PARSE_ERROR:
      return tr(STR_GCAL_BAD_RESPONSE);
    case GCalClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

// -- per-service sync -------------------------------------------------------

const char* runTasks() {
  // NTP first, only because it needs the radio while it is still up.
  std::string ntpDate;
  if (!resolveTodayDate(ntpDate)) ntpDate.clear();

  // Independent of whether the rest of this sync succeeds: if a fresh NTP
  // read says the day has moved on since the last time this cache saw a
  // completion, the log for that earlier day is retired now rather than
  // surviving a failed fetch and still reading as today's.
  if (!ntpDate.empty()) TODOIST_TASKS.clearCompletedIfStale(civil::dateFromIso(ntpDate.c_str()));

  // Shared across every Todoist call below (all the same host,
  // api.todoist.com) so SecureHttpClient's own keep-alive can actually take
  // effect instead of a fresh TLS handshake per call -- see TodoistClient.h's
  // own parameter doc on why this matters for heap fragmentation.
  freeink::SecureHttpClient http;
  http.setInsecure();

  // Push queued completions before fetching, so the fetched list already
  // reflects them. A copy: clearPending() mutates the queue as we go.
  const std::vector<std::string> pending = TODOIST_TASKS.getPendingIds();
  TodoistClient::Error error = TodoistClient::OK;
  for (const auto& id : pending) {
    // Each push is a full TLS request; the sync runs on the main task.
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::closeTask(http, id);
    if (error != TodoistClient::OK) {
      LOG_ERR("OSYNC", "Task push failed for %s: %s", id.c_str(), TodoistClient::errorString(error));
      break;  // keep the rest queued for the next attempt
    }
    TODOIST_TASKS.clearPending(id);
  }

  // Same reasoning, same queue-then-fetch ordering, for locally-made
  // reschedules: push them before fetching so the list that comes back
  // already shows the new dates. A copy, for the same reason as pending
  // above - clearPendingReschedule() mutates the queue as we go.
  if (error == TodoistClient::OK) {
    const auto reschedules = TODOIST_TASKS.getPendingReschedules();
    for (const auto& reschedule : reschedules) {
      resetTaskWatchdogIfSubscribed();
      char isoDate[11];
      todoist::isoFromDueDays(reschedule.dueDays, isoDate, sizeof(isoDate));
      error = TodoistClient::rescheduleTask(http, reschedule.taskId, isoDate);
      resetTaskWatchdogIfSubscribed();
      if (error == TodoistClient::NOT_FOUND) {
        // Gone (deleted, or already completed elsewhere) - nowhere left to
        // apply this, and holding it "pending" forever would wedge every
        // future sync behind it.
        LOG_ERR("OSYNC", "Task %s no longer exists; dropping its pending reschedule", reschedule.taskId.c_str());
        TODOIST_TASKS.clearPendingReschedule(reschedule.taskId);
        error = TodoistClient::OK;
        continue;
      }
      if (error != TodoistClient::OK) {
        LOG_ERR("OSYNC", "Reschedule push failed for %s: %s", reschedule.taskId.c_str(),
                TodoistClient::errorString(error));
        break;  // keep the rest queued for the next attempt
      }
      TODOIST_TASKS.clearPendingReschedule(reschedule.taskId);
    }
  }

  std::vector<TodoistTask> fetched;
  std::string serverDate;
  if (error == TodoistClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = TodoistClient::fetchTasks(http, TODOIST_STORE.getFilter(), fetched, serverDate);
    resetTaskWatchdogIfSubscribed();
  }

  // NTP wins when it worked: it is the only source that applies the configured
  // UTC offset. The Date header is the fallback - it cannot be blocked the way
  // NTP can, but it is GMT, so it can read a day off either side of midnight.
  // The previous sync then keeps the date from going backwards.
  std::string today = ntpDate.empty() ? serverDate : ntpDate;
  today = laterDate(today, TODOIST_TASKS.getSyncDate());
  if (today.empty()) {
    LOG_ERR("OSYNC", "Today unresolved: no NTP, no Date header, no previous sync");
  }

  if (error == TodoistClient::OK) {
    RenderLock lock;
    TODOIST_TASKS.setTasks(std::move(fetched), today);
  }

  // The project names behind the tasks' project ids, for the Companion's title
  // row. Best-effort, like the completed-tasks fetch below: the task list above
  // is this sync's real job, so a failure here only leaves the previous names in
  // place (tasks from a new project then group under a generic title until the
  // next sync that gets through).
  if (error == TodoistClient::OK) {
    std::vector<TodoistProject> projects;
    resetTaskWatchdogIfSubscribed();
    const TodoistClient::Error projectError = TodoistClient::fetchProjects(http, projects);
    resetTaskWatchdogIfSubscribed();
    if (projectError == TodoistClient::OK) {
      RenderLock lock;
      TODOIST_TASKS.setProjects(std::move(projects));
    } else {
      LOG_ERR("OSYNC", "Projects fetch failed: %s", TodoistClient::errorString(projectError));
    }
  }

  // A completed task never shows up in the open-task fetch above, so a task
  // finished in the Todoist app - not on this device - would otherwise never
  // be counted. This asks Todoist directly for today's completions matching the
  // Filter setting, so app-side completions credit the companion and show in the
  // Logs list. Best-effort: the open-task list above is this sync's real job, so
  // a failure here does not fail the sync itself - it just leaves today's
  // completed count and the companion's credit stale until the next attempt.
  if (error == TodoistClient::OK && !today.empty()) {
    std::vector<TodoistCompletedLogEntry> completed;
    completed.reserve(TodoistTaskCache::MAX_COMPLETED_TODAY_TITLES);
    CompletedCollector collector{&completed};
    uint16_t ignoredCount = 0;
    resetTaskWatchdogIfSubscribed();
    const TodoistClient::Error countError = TodoistClient::fetchCompletedCountForDay(
        http, TODOIST_STORE.getFilter(), today, ignoredCount, collectCompletedItem, &collector);
    resetTaskWatchdogIfSubscribed();
    if (countError == TodoistClient::OK) {
      {
        RenderLock lock;
        TODOIST_TASKS.setCompletedToday(std::move(completed), today);
      }
      // Gated on a fetch having landed: a failed one means nothing here actually
      // changed, so there is nothing new to credit.
      COMPANION.recordActivity();
    } else {
      LOG_ERR("OSYNC", "Completed-tasks fetch failed: %s", TodoistClient::errorString(countError));
    }
  }

  // Persists the fetched list and whatever the queue push managed to clear, so a
  // failed fetch after a successful push is still recorded.
  TODOIST_TASKS.saveToFile();
  return error == TodoistClient::OK ? nullptr : taskErrorText(error);
}

const char* runCalendar() {
  // The refresh serves two purposes: it mints the access token every request
  // needs, and its HTTP Date header is the device's clock. Most boards have no
  // RTC and SNTP can be blocked on a given network, so anchoring the window on a
  // header that cannot fail when the request succeeded is what makes "today and
  // the next 30 days" mean anything.
  std::string accessToken;
  uint16_t today = civil::NO_DATE;
  const GCalAuth::Error authError = GCalAuth::refreshAccessToken(accessToken, today);

  GCalClient::Error error = GCalClient::OK;
  std::vector<GCalEvent> fetched;

  if (authError != GCalAuth::OK) {
    LOG_ERR("OSYNC", "Token refresh failed: %s", GCalAuth::errorString(authError));
  } else if (today == civil::NO_DATE) {
    // Without a date there is no window to ask for, and guessing would silently
    // show the wrong month.
    LOG_ERR("OSYNC", "No Date header on the token response; cannot anchor the window");
    error = GCalClient::PARSE_ERROR;
  } else {
    const uint16_t lastDay = static_cast<uint16_t>(today + GCAL_WINDOW_DAYS - 1);
    fetched.reserve(GCAL_MAX_EVENTS);
    // Shared across every calendar's fetch below (all the same host,
    // www.googleapis.com) so SecureHttpClient's own keep-alive can actually
    // take effect -- see GCalClient.h's own parameter doc.
    freeink::SecureHttpClient http;
    http.setInsecure();
    for (const auto& calendarId : GCAL_STORE.getSelectedCalendars()) {
      resetTaskWatchdogIfSubscribed();
      error = GCalClient::fetchEvents(http, accessToken, calendarId, today, lastDay, fetched);
      resetTaskWatchdogIfSubscribed();
      if (error != GCalClient::OK) {
        LOG_ERR("OSYNC", "Event fetch failed for %s: %s", calendarId.c_str(), GCalClient::errorString(error));
        break;
      }
      if (fetched.size() >= GCAL_MAX_EVENTS) {
        LOG_INF("OSYNC", "Event cap (%zu) reached; later calendars not fetched", GCAL_MAX_EVENTS);
        break;
      }
    }
  }

  const char* failure = nullptr;
  if (authError != GCalAuth::OK) {
    failure = authError == GCalAuth::INVALID_GRANT ? tr(STR_GCAL_RELINK_NEEDED) : tr(STR_NETWORK_ERROR);
  } else if (error != GCalClient::OK) {
    failure = calendarErrorText(error);
  } else {
    RenderLock lock;
    GCAL_EVENTS.setEvents(std::move(fetched), today);
  }
  GCAL_EVENTS.saveToFile();
  return failure;
}

}  // namespace

std::string localIsoDateFromUtc(const uint16_t year, const uint8_t month, const uint8_t day, const uint8_t hour,
                                const uint8_t minute) {
  uint8_t offsetQ = SETTINGS.clockUtcOffsetQ;
  if (offsetQ > 104) offsetQ = 104;  // clamp a corrupt persisted value to UTC+14
  const int32_t utcDays = civil::daysFromCivil(year, month, day);
  const time_t utcEpoch = static_cast<time_t>(utcDays) * 86400 + hour * 3600 + minute * 60;
  const time_t local = utcEpoch + (static_cast<int>(offsetQ) - 48) * 15 * 60;
  struct tm timeinfo;
  gmtime_r(&local, &timeinfo);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return std::string(buf);
}

uint16_t todayLocalDate() {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!halClock.getUtcDateTime(year, month, day, hour, minute)) return civil::NO_DATE;
  return civil::dateFromIso(localIsoDateFromUtc(year, month, day, hour, minute).c_str());
}

const char* name(const Service service) {
  // The same name the home grid and the app's own screen use, nickname included:
  // a sync list that called an app something else would read as a different app.
  switch (service) {
    case Service::Tasks:
      return homeAppOrder::displayName(homeAppOrder::AppId::Tasks);
    case Service::Calendar:
      return homeAppOrder::displayName(homeAppOrder::AppId::Calendar);
  }
  return "";
}

bool isConfigured(const Service service) {
  switch (service) {
    case Service::Tasks:
      return TODOIST_STORE.hasToken();
    case Service::Calendar:
      // Linked and told which calendars to read: without a selection the sync has
      // nothing to ask for.
      return GCAL_STORE.hasClientCredentials() && GCAL_STORE.isLinked() && !GCAL_STORE.getSelectedCalendars().empty();
  }
  return false;
}

const char* run(const Service service) {
  switch (service) {
    case Service::Tasks:
      return runTasks();
    case Service::Calendar:
      return runCalendar();
  }
  return nullptr;
}

}  // namespace organizerSync
