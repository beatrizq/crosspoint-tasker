#pragma once
#include <string>
#include <vector>

#include "TodoistCompletedCountParser.h"
#include "TodoistTask.h"

namespace freeink {
class SecureHttpClient;
}

/**
 * HTTPS client for the Todoist unified API (v1).
 *
 * Base URL: https://api.todoist.com/api/v1
 *
 * Endpoints used:
 *   GET  /tasks/filter?query=<user filter>              - the Tasks screen's list
 *   POST /tasks/{id}/close                              - mark a task complete
 *   POST /tasks/{id}                                    - reschedule (due_date only)
 *   GET  /tasks/completed/by_completion_date?filter_query=<user filter>
 *                                                        - today's completions, for the companion
 *
 * The query is the Filter setting verbatim, in Todoist's own filter syntax (the
 * same strings the app's filter view takes: "view all", "today | overdue",
 * "#Work & !subtask", "no date", ...). Todoist caps it at 1,024 characters and
 * answers anything it cannot parse with a 400, surfaced as INVALID_FILTER.
 *
 * Authentication: Authorization: Bearer <personal API token>
 *
 * Runs over wolfSSL (SecureHttpClient) like the KOReader sync client: Todoist
 * terminates TLS 1.3, which the precompiled system mbedTLS cannot negotiate.
 */
class TodoistClient {
 public:
  enum Error {
    OK = 0,
    NO_TOKEN,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    PARSE_ERROR,
    LOW_MEMORY,
    INVALID_FILTER,  // 400: Todoist could not parse the Filter setting
    NOT_FOUND,       // 404: no such task (deleted, or already completed)
  };

  /**
   * Fetch whatever `filter` (one of the two Filter settings) matches, in one
   * filter query. The Tasks
   * screen splits the result into Overdue, Today, Upcoming and No date against
   * the date it settles on; it does not narrow it further.
   *
   * @param outTasks Output: parsed tasks, capped at TODOIST_MAX_TASKS. Each
   *                 carries its due date packed as TodoistTask::dueDays; the
   *                 overdue flag is left for the cache to set once the caller
   *                 has settled on today's date. A filter can match more tasks
   *                 than the cap, so the soonest are kept, with undated tasks
   *                 ranked behind dated ones - see collectTask.
   * @param outServerDate Output: today, as "YYYY-MM-DD", taken from the
   *                 response's HTTP Date header, or empty when the header was
   *                 missing or unparseable. The header is used rather than the
   *                 newest due date in the body: this filter reaches into the
   *                 future, so the body's newest date is a month out, and a
   *                 header that cannot fail when the request succeeded is the
   *                 more reliable clock on boards with no RTC. It is GMT, so a
   *                 caller with a working NTP result and a configured UTC offset
   *                 should prefer that and keep this as the fallback.
   *
   * `http`: caller-owned connection, shared across a sync's calls to this same
   * host so SecureHttpClient's own keep-alive can actually take effect (see
   * organizerSync::runTasks()) - this function neither constructs nor ends it.
   */
  static Error fetchTasks(freeink::SecureHttpClient& http, const std::string& filter,
                          std::vector<TodoistTask>& outTasks, std::string& outServerDate);

  /**
   * Complete a task. A 404 is reported as OK: the task is already gone from the
   * server (completed elsewhere), which is the state the caller wants.
   *
   * `http`: caller-owned connection -- see fetchTasks()'s own parameter doc.
   */
  static Error closeTask(freeink::SecureHttpClient& http, const std::string& taskId);

  /**
   * Reschedule a task to `isoDueDate` ("YYYY-MM-DD"), or clear its due date
   * entirely when `isoDueDate` is empty (sent as due_string: "no date" -
   * Todoist's REST API has no dedicated "clear the due date" field, but its
   * due_string goes through the same natural-language parser quick-add uses,
   * where that phrase is what clears a date there too). Unlike closeTask(), a
   * 404 here is reported as NOT_FOUND, not OK: a task that no longer exists
   * cannot usefully be rescheduled the way it can usefully be "already
   * complete", so the caller should drop the attempt rather than treat it as
   * done.
   *
   * Todoist has no way to reschedule a single occurrence of a recurring task
   * without replacing its recurrence entirely - this call does exactly that
   * for any task, recurring or not. The caller is responsible for warning
   * before rescheduling a recurring one; see TodoistTask::isRecurring.
   *
   * `http`: caller-owned connection -- see fetchTasks()'s own parameter doc.
   */
  static Error rescheduleTask(freeink::SecureHttpClient& http, const std::string& taskId,
                              const std::string& isoDueDate);

  /**
   * Counts tasks matching `filter` that Todoist recorded as completed
   * on `isoDate` ("YYYY-MM-DD") - on this device, in the Todoist app, or on the
   * web. Unlike fetchTasks(), which only ever sees tasks still open, this asks
   * Todoist's own completed-tasks record directly, so a task finished anywhere
   * but this device is still counted once this call succeeds.
   *
   * Counts a single page, capped at the endpoint's own page limit (50 - see
   * COMPLETED_PAGE_LIMIT in the .cpp): the companion's mood ladder tops out at
   * 3 combined completions for the day, so an exact count past a few dozen buys
   * nothing worth a second round trip.
   *
   * The since/until window is `isoDate`'s UTC day (00:00:00 to 23:59:59) - the
   * same level of day-boundary precision every other syncDate-derived figure in
   * this codebase already accepts; a completion made right around midnight in
   * the user's own timezone can land on the wrong side of it.
   *
   * @param outCount Output: 0 on any error.
   * @param itemSink Optional: invoked once per completed item (with its id and
   * title) as the response streams in, for a caller that wants the actual
   * list rather than just outCount. See TodoistCompletedCountParser::ItemSink.
   *
   * `http`: caller-owned connection -- see fetchTasks()'s own parameter doc.
   */
  static Error fetchCompletedCountForDay(freeink::SecureHttpClient& http, const std::string& filter,
                                         const std::string& isoDate, uint16_t& outCount,
                                         TodoistCompletedCountParser::ItemSink itemSink = nullptr,
                                         void* itemSinkCtx = nullptr);

  /** Diagnostic message for logs. User-facing text is translated by the caller. */
  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
