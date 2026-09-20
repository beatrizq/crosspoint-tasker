#include "OrganizerSync.h"

#include <CivilTime.h>
#include <GCalAuth.h>
#include <GCalClient.h>
#include <GCalEventCache.h>
#include <GCalStore.h>
#include <HabitifyClient.h>
#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <TodoistClient.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>
#include <YnabAccountCache.h>
#include <YnabCategoryCache.h>
#include <YnabClient.h>
#include <YnabStore.h>
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

// TodoistCompletedCountParser::TitleSink for the completed-count fetch below:
// collects titles as the response streams in, capped the same way the cache
// itself caps them so a busy day never grows this past what setCompletedToday
// would keep anyway.
void collectCompletedTitle(void* ctx, const char* content) {
  auto* titles = static_cast<std::vector<std::string>*>(ctx);
  if (titles->size() < TodoistTaskCache::MAX_COMPLETED_TODAY_TITLES) titles->emplace_back(content);
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

const char* budgetErrorText(const YnabClient::Error error) {
  switch (error) {
    case YnabClient::NO_TOKEN:
    case YnabClient::NO_BUDGET:
      return tr(STR_YNAB_NOT_CONFIGURED);
    case YnabClient::AUTH_FAILED:
      return tr(STR_YNAB_INVALID_TOKEN);
    case YnabClient::NOT_FOUND:
      return tr(STR_YNAB_BUDGET_NOT_FOUND);
    case YnabClient::RATE_LIMITED:
      return tr(STR_YNAB_RATE_LIMITED);
    case YnabClient::SERVER_ERROR:
      return tr(STR_YNAB_SERVER_ERROR);
    case YnabClient::PARSE_ERROR:
      return tr(STR_YNAB_BAD_RESPONSE);
    case YnabClient::LOW_MEMORY:
      return tr(STR_MEMORY_ERROR);
    default:
      return tr(STR_NETWORK_ERROR);
  }
}

const char* habitErrorText(const HabitifyClient::Error error) {
  switch (error) {
    case HabitifyClient::NO_KEY:
      return tr(STR_HABITIFY_NO_KEY);
    case HabitifyClient::AUTH_FAILED:
      return tr(STR_HABITIFY_KEY_REJECTED);
    case HabitifyClient::NOT_FOUND:
      return tr(STR_HABITIFY_HABIT_NOT_FOUND);
    case HabitifyClient::RATE_LIMITED:
      return tr(STR_HABITIFY_RATE_LIMITED);
    case HabitifyClient::SERVER_ERROR:
      return tr(STR_HABITIFY_SERVER_ERROR);
    case HabitifyClient::PARSE_ERROR:
      return tr(STR_HABITIFY_BAD_RESPONSE);
    case HabitifyClient::LOW_MEMORY:
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
        // future sync behind it, the same way an orphaned Habitify push once
        // did (see runHabits()'s own NOT_FOUND handling).
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
    error = TodoistClient::fetchTasks(http, fetched, serverDate);
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

  // A completed task never shows up in the open-task fetch above, so a task
  // finished in the Todoist app - not on this device - would otherwise never
  // be counted. This asks Todoist directly for today's completions matching
  // the same Filter setting, so app-side completions credit the companion the
  // same way Habitify's already do. Best-effort: the open-task list above is
  // this sync's real job, so a failure here does not fail the sync itself -
  // it just leaves today's completed count and the companion's credit stale
  // until the next attempt.
  if (error == TodoistClient::OK && !today.empty()) {
    resetTaskWatchdogIfSubscribed();
    uint16_t completedCount = 0;
    std::vector<std::string> completedTitles;
    completedTitles.reserve(TodoistTaskCache::MAX_COMPLETED_TODAY_TITLES);
    const TodoistClient::Error countError =
        TodoistClient::fetchCompletedCountForDay(http, today, completedCount, collectCompletedTitle, &completedTitles);
    resetTaskWatchdogIfSubscribed();
    if (countError == TodoistClient::OK) {
      RenderLock lock;
      TODOIST_TASKS.setCompletedToday(completedCount, today, std::move(completedTitles));
      // Gated on success: a failed fetch means nothing here actually changed,
      // so there is nothing new to credit.
      COMPANION.recordActivity();
    } else {
      LOG_ERR("OSYNC", "Completed-count fetch failed: %s", TodoistClient::errorString(countError));
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

const char* runBudget() {
  // One request, and it carries its own month: the balances are month-scoped and
  // YNAB says which month it answered for, so nothing here needs a clock. That
  // matters on boards with no RTC, and it keeps this to a single call against a
  // token allowed 200 requests an hour.
  std::vector<YnabCategory> fetched;
  uint16_t month = civil::NO_DATE;
  resetTaskWatchdogIfSubscribed();
  // Shared across every YNAB call below (all the same host, api.ynab.com) so
  // SecureHttpClient's own keep-alive can actually take effect -- see
  // YnabClient.h's own parameter doc.
  freeink::SecureHttpClient http;
  http.setInsecure();
  const YnabClient::Error error = YnabClient::fetchSelectedCategories(http, fetched, month);
  resetTaskWatchdogIfSubscribed();
  if (error != YnabClient::OK) {
    LOG_ERR("OSYNC", "Plan fetch failed: %s", YnabClient::errorString(error));
  }

  if (error == YnabClient::OK) {
    RenderLock lock;
    YNAB_CATEGORIES.setCategories(std::move(fetched), month);
  }
  YNAB_CATEGORIES.saveToFile();

  // Sync All is the one place "sync everything" should actually mean
  // everything, so every already-known account also gets its transactions
  // refreshed here - each is its own request against the same 200/hour
  // allowance BudgetActivity's own per-tab sync is deliberately stingy with
  // (see YnabAccountCache's own comment), but a user who asked to sync
  // everything should not have part of one already-configured integration
  // silently skipped. The account list itself still is not refetched here:
  // discovering accounts stays a Settings-only action, since that costs its
  // own request for something that changes about once a year.
  //
  // A copy of the ids: setTransactions() mutates the account list as we go,
  // and the fetch loop should walk the accounts as they stood when it started.
  std::vector<std::string> accountIds;
  accountIds.reserve(YNAB_ACCOUNTS.getAccounts().size());
  for (const auto& account : YNAB_ACCOUNTS.getAccounts()) accountIds.push_back(account.id);

  std::vector<YnabTransaction> transactionsFetched;
  for (const auto& accountId : accountIds) {
    resetTaskWatchdogIfSubscribed();
    uint16_t date = civil::NO_DATE;
    const YnabClient::Error txError = YnabClient::fetchTransactions(http, accountId, transactionsFetched, date);
    resetTaskWatchdogIfSubscribed();
    if (txError == YnabClient::RATE_LIMITED) {
      LOG_ERR("OSYNC", "Account transaction fetch rate-limited; skipping the rest");
      break;
    }
    if (txError != YnabClient::OK) {
      LOG_ERR("OSYNC", "Account transaction fetch failed for %s: %s", accountId.c_str(),
              YnabClient::errorString(txError));
      continue;
    }
    RenderLock lock;
    YNAB_ACCOUNTS.setTransactions(accountId, std::move(transactionsFetched), date);
  }
  if (!accountIds.empty()) YNAB_ACCOUNTS.saveToFile();

  return error == YnabClient::OK ? nullptr : budgetErrorText(error);
}

const char* runHabits() {
  // Unlike Tasks, nothing else in this function resolves the clock -- the
  // journal fetch below trusts Habitify's own server date entirely (see
  // fetchJournal()'s own comment) and never asks the device what day it
  // thinks it is. That leaves no chance to notice a stale cache before a
  // sync, so it is done explicitly here: a real resync (not gated on
  // halClock.isAvailable() -- configTzTime() inside it already updates the
  // system clock on every board, RTC chip or not), then a day-rollover check
  // independent of whether the fetch below succeeds, the same reasoning as
  // runTasks()'s own clearCompletedIfStale() call.
  halClock.syncFromNTP();
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (halClock.getUtcDateTime(year, month, day, hour, minute)) {
    HABITIFY_HABITS.rolloverIfStale(civil::packDate(year, month, day));
  }

  // Push what is owed before fetching, so the journal that comes back already
  // reflects it. A copy of the ids and amounts: clearPending() mutates the cache
  // as we go, and the fetch replaces the list wholesale.
  struct Owed {
    std::string id;
    std::string unit;
    float amount;
  };
  std::vector<Owed> owed;
  // Reserved against the habit count rather than the number owing: one growth
  // event is three heap operations, and this runs with a TLS session about to be
  // opened, where DRAM fragmentation is what breaks the handshake.
  owed.reserve(HABITIFY_HABITS.getHabits().size());
  for (const auto& habit : HABITIFY_HABITS.getHabits()) {
    if (habit.hasPending() && !habit.unitSymbol.empty()) {
      owed.push_back(Owed{habit.id, habit.unitSymbol, habit.pending});
    }
  }

  // Shared across every Habitify call below (all the same host,
  // api.habitify.me) so SecureHttpClient's own keep-alive can actually take
  // effect -- see HabitifyClient.h's own parameter doc.
  freeink::SecureHttpClient http;
  http.setInsecure();

  HabitifyClient::Error error = HabitifyClient::OK;
  for (const auto& entry : owed) {
    resetTaskWatchdogIfSubscribed();
    const HabitifyClient::Error pushError = HabitifyClient::addLog(http, entry.id, entry.unit, entry.amount);
    if (pushError == HabitifyClient::NOT_FOUND) {
      // The habit no longer exists server-side - deleted, or replaced with a new
      // one in the app. There is nowhere left to push this progress, and unlike
      // Todoist's closeTask() (a 404 there means "already done", so it counts as
      // success) a missing habit can never accept a log. Holding it "owed"
      // forever would wedge every future sync behind it: the fetch below only
      // runs once every entry here has pushed clean, so the same dead id would
      // fail again next time, and again after that.
      LOG_ERR("OSYNC", "Habit %s no longer exists; dropping its unpushed progress", entry.id.c_str());
      HABITIFY_HABITS.clearPending(entry.id, entry.amount);
      continue;
    }
    if (pushError != HabitifyClient::OK) {
      error = pushError;
      LOG_ERR("OSYNC", "Habit push failed for %s: %s", entry.id.c_str(), HabitifyClient::errorString(pushError));
      break;  // keep the rest owed for the next attempt
    }
    HABITIFY_HABITS.clearPending(entry.id, entry.amount);
  }

  // Complete taps are pushed the same way, via Habitify's dedicated endpoint -
  // independent of the numeric progress queue above, since completing is "mark
  // done" rather than "add N". A copy, for the same reason as owed above.
  if (error == HabitifyClient::OK) {
    std::vector<std::string> completions;
    completions.reserve(HABITIFY_HABITS.getHabits().size());
    for (const auto& habit : HABITIFY_HABITS.getHabits()) {
      if (habit.pendingComplete) completions.push_back(habit.id);
    }
    for (const auto& habitId : completions) {
      resetTaskWatchdogIfSubscribed();
      const HabitifyClient::Error completeError = HabitifyClient::completeHabit(http, habitId);
      if (completeError == HabitifyClient::NOT_FOUND) {
        // Same reasoning as the progress queue's own NOT_FOUND handling above:
        // a gone habit can never accept a complete, so holding it queued
        // forever would wedge every future sync behind it.
        LOG_ERR("OSYNC", "Habit %s no longer exists; dropping its pending complete", habitId.c_str());
        HABITIFY_HABITS.clearPendingComplete(habitId);
        continue;
      }
      if (completeError != HabitifyClient::OK) {
        error = completeError;
        LOG_ERR("OSYNC", "Habit complete push failed for %s: %s", habitId.c_str(),
                HabitifyClient::errorString(completeError));
        break;  // keep the rest queued for the next attempt
      }
      HABITIFY_HABITS.clearPendingComplete(habitId);
    }
    resetTaskWatchdogIfSubscribed();
  }

  std::vector<HabitifyHabit> fetched;
  uint16_t date = civil::NO_DATE;
  if (error == HabitifyClient::OK) {
    resetTaskWatchdogIfSubscribed();
    error = HabitifyClient::fetchJournal(http, fetched, date);
    resetTaskWatchdogIfSubscribed();
  }

  // Areas: a second, best-effort fetch for the Habits screen's per-area tabs
  // -- see HabitifyClient::fetchHabitAreas()'s own doc comment for why this
  // cannot just be folded into the journal fetch above. Its own failure never
  // fails the habit sync itself, the same way Todoist's completed-count fetch
  // treats its own second call: it just leaves the tab set stale until the
  // next attempt.
  bool areasFresh = false;
  std::vector<HabitifyHabitAreaAssignment> areaAssignments;
  if (error == HabitifyClient::OK) {
    resetTaskWatchdogIfSubscribed();
    const HabitifyClient::Error areasError = HabitifyClient::fetchHabitAreas(http, areaAssignments);
    resetTaskWatchdogIfSubscribed();
    if (areasError == HabitifyClient::OK) {
      areasFresh = true;
    } else {
      LOG_ERR("OSYNC", "Habit areas fetch failed: %s", HabitifyClient::errorString(areasError));
    }
  }

  if (error == HabitifyClient::OK) {
    RenderLock lock;
    std::vector<HabitifyArea> areasList;
    if (areasFresh) {
      // Join by id: each habit gets its first area (see
      // HabitifyHabitAreaAssignment's own "first area only" comment), and the
      // deduped set of areas becomes the tab list.
      for (auto& fetchedHabit : fetched) {
        for (const auto& assignment : areaAssignments) {
          if (assignment.habitId == fetchedHabit.id) {
            fetchedHabit.areaId = assignment.areaId;
            break;
          }
        }
      }
      areasList.reserve(areaAssignments.size());
      for (const auto& assignment : areaAssignments) {
        const bool alreadyKnown = std::any_of(areasList.begin(), areasList.end(), [&assignment](const HabitifyArea& a) {
          return a.id == assignment.areaId;
        });
        if (!alreadyKnown) areasList.push_back(HabitifyArea{assignment.areaId, assignment.areaName});
      }
    }
    // Carries over anything still owed - a press that landed between the push
    // above and this fetch.
    HABITIFY_HABITS.setHabits(std::move(fetched), date, areasFresh, std::move(areasList));
    // Catches a habit completed elsewhere (the Habitify app itself, say) that
    // this device never saw a local press for. Gated on success: a failed
    // fetch means the cache did not change, so there is nothing new to credit,
    // and running it anyway would just cost a needless SD write on a sync that
    // already failed.
    COMPANION.recordActivity();
  }
  HABITIFY_HABITS.saveToFile();
  return error == HabitifyClient::OK ? nullptr : habitErrorText(error);
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
    case Service::Budget:
      return homeAppOrder::displayName(homeAppOrder::AppId::Budget);
    case Service::Habits:
      return homeAppOrder::displayName(homeAppOrder::AppId::Habits);
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
    case Service::Budget:
      return YNAB_STORE.isConfigured() && !YNAB_STORE.getSelectedCategories().empty();
    case Service::Habits:
      return HABITIFY_STORE.hasApiKey();
  }
  return false;
}

const char* run(const Service service) {
  switch (service) {
    case Service::Tasks:
      return runTasks();
    case Service::Calendar:
      return runCalendar();
    case Service::Budget:
      return runBudget();
    case Service::Habits:
      return runHabits();
  }
  return nullptr;
}

}  // namespace organizerSync
