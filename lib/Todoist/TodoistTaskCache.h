#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "TodoistTask.h"

// One task's due date changed on the device, awaiting push to the server.
struct TodoistPendingReschedule {
  std::string taskId;
  uint16_t dueDays = todoist::DUE_NONE;
};

// One row of the Companion's Logs screen for a completed task. `taskId` and
// `pending` exist so the Logs screen can tell a locally-completed-but-not-
// yet-pushed row (Cached, `pending=true`, safely cancellable via
// cancelCompletedLogEntry()) apart from one a sync has already confirmed
// (Synced, `pending=false`, `taskId` left empty since there is nothing left
// to cancel a push for). See completeTaskAt()/setCompletedToday() for who
// sets which.
struct TodoistCompletedLogEntry {
  std::string title;
  std::string taskId;
  bool pending = false;
  // Which filter(s) the task belonged to when it was completed, the same bits as
  // TodoistTask::filterMask -- the Logs tab shows only the active filter's. An
  // entry from before there were two filters counts for both.
  uint8_t filterMask = TodoistTask::FILTER_1_BIT | TodoistTask::FILTER_2_BIT;
};

/**
 * Singleton holding the last synced task list plus the completions and
 * reschedules that have not reached the server yet.
 *
 * The Today screen renders straight from here, so opening it needs no Wi-Fi:
 * a sync is an explicit user action (hold Select), and completing or
 * rescheduling a task only updates this cache. The queued changes are pushed
 * on the next sync, before the list is re-fetched, so the fresh list already
 * reflects them.
 */
class TodoistTaskCache : public PersistableStore<TodoistTaskCache> {
 private:
  std::vector<TodoistTask> tasks;       // Overdue first, then due today
  std::vector<std::string> pendingIds;  // Completed locally, awaiting push
  // Rescheduled locally, awaiting push. Keyed by task id rather than cache
  // index, since a task's index shifts whenever another task ahead of it is
  // completed or the list is re-sorted after a sync.
  std::vector<TodoistPendingReschedule> pendingReschedules;
  std::string syncDate;         // Local date of the last sync, "YYYY-MM-DD"
  uint16_t completedToday = 0;  // Tasks completed on this device today
  // Day completedToday belongs to. Normally keyed the same way syncDate's day
  // is (see completeTaskAt/rolloverCompletedIfNeeded), but clearCompletedIfStale()
  // can also advance it straight from the real clock, ahead of a syncDate that
  // has not synced yet today -- see rolloverCompletedIfNeeded()'s own comment
  // for why it must never walk this back down to match a stale syncDate once
  // that has happened.
  uint16_t completedDay = todoist::DUE_NONE;
  // Rows behind completedToday, for the Companion's Logs screen. See
  // getCompletedTodayEntries()'s own comment for how it relates to the count.
  std::vector<TodoistCompletedLogEntry> completedTodayEntries;

  TodoistTaskCache() = default;
  ~TodoistTaskCache() = default;

  friend class PersistableStore<TodoistTaskCache>;

 public:
  static constexpr size_t MAX_TASKS = TODOIST_MAX_CACHED_TASKS;
  static constexpr size_t MAX_PENDING = TODOIST_MAX_CACHED_TASKS;

  static const char* getFilePath() { return "/.crosspoint/todoist_tasks.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<TodoistTask>& getTasks() const { return tasks; }
  size_t getOverdueCount() const;
  // Overdue plus due exactly today, per syncDate's notion of "today" - the
  // Home screen's notification-style badge for this app.
  size_t getDueTodayOrOverdueCount() const;
  const std::string& getSyncDate() const { return syncDate; }
  bool hasSynced() const { return !syncDate.empty(); }

  // Replace the list after a successful fetch.
  //
  // Sorted by due date ascending, so the oldest overdue task leads and tasks
  // due today trail; undated tasks (DUE_NONE) sort last. Stable, so the
  // server's ordering survives within a single date.
  //
  // `date` is today as "YYYY-MM-DD" and sets the overdue threshold: anything
  // due strictly before it is flagged. An empty date leaves the stored date
  // untouched and clears no flags, so a sync that could not establish today
  // keeps showing the last date it did know.
  void setTasks(std::vector<TodoistTask>&& fetched, const std::string& date);

  // Adds the tasks Filter 2 matched to the list setTasks() stored for Filter 1: a
  // task already there gains the FILTER_2_BIT, one that is not is appended with
  // only that bit. The sync commits each filter as soon as it arrives and merges
  // the second in, rather than holding both fetches (and the old list) in RAM at
  // once -- that peak is what starved the next TLS request of heap.
  void mergeFilter2Tasks(std::vector<TodoistTask>&& fetched);

  // Drop the task locally and remember to close it on the server. No-op for an
  // unknown index.
  void completeTaskAt(size_t index);

  const std::vector<std::string>& getPendingIds() const { return pendingIds; }
  // True with a pending completion OR a pending reschedule -- either one is
  // something the next sync still needs to push.
  bool hasPending() const { return !pendingIds.empty() || !pendingReschedules.empty(); }
  // Completions plus reschedules awaiting push, for the "N to sync" status
  // line -- both are equally "something not on the server yet".
  size_t pendingSyncCount() const { return pendingIds.size() + pendingReschedules.size(); }
  // Called once the server accepted (or already knew about) the completion.
  void clearPending(const std::string& id);

  // Updates the task's due date immediately, for instant feedback, and queues
  // the change for push on the next sync - the same offline-first pattern
  // completeTaskAt() already uses. Rescheduling the same task again before a
  // sync replaces the queued date rather than adding a second entry. No-op
  // for an unknown index.
  void rescheduleTaskAt(size_t index, uint16_t newDueDays);

  const std::vector<TodoistPendingReschedule>& getPendingReschedules() const { return pendingReschedules; }
  // Called once the server accepted a queued reschedule, or the task it was
  // for turned out to already be gone.
  void clearPendingReschedule(const std::string& id);

  // Tasks completed today, per syncDate's notion of "today" - on this device,
  // or anywhere else once a sync has confirmed it (see setCompletedToday).
  // Stale until the next sync if the day rolled over with no completion yet
  // to trigger the rollover - the same staleness every other syncDate-derived
  // figure in this class already tolerates between syncs.
  uint16_t getCompletedToday() const { return completedToday; }

  // Rows behind getCompletedToday(), for the Companion's Logs screen. Not
  // necessarily one-to-one with the count: a local completion appends
  // immediately (see completeTaskAt) for instant feedback, but the fetch
  // that lands in setCompletedToday() is authoritative and replaces both
  // together, so entries can briefly outrun/undershoot the count between a
  // local press and the next sync the same way completedToday itself can be
  // stale (see its own comment). Capped at MAX_COMPLETED_STORED.
  const std::vector<TodoistCompletedLogEntry>& getCompletedTodayEntries() const { return completedTodayEntries; }
  // Entries kept per filter, and in total: two filters' logs are stored side by
  // side, so the cache holds twice one filter's worth.
  static constexpr size_t MAX_COMPLETED_TODAY_TITLES = 20;
  static constexpr size_t MAX_COMPLETED_STORED = MAX_COMPLETED_TODAY_TITLES * 2;

  // Sets today's completed count and titles directly, from a fetch that
  // already reflects the whole day: this device's own presses once pushed,
  // and anything finished in the Todoist app or on the web. Replaces rather
  // than adds - the fetch is authoritative for the day, not incremental - and
  // marks completedDay resolved so a completion pressed on-device later the
  // same day still adds on top of this baseline instead of rolling over
  // first. entries is moved from and truncated to MAX_COMPLETED_STORED; every
  // one is Synced (pending=false) -- a fetch is by definition already confirmed
  // by the server, with no push left to cancel.
  void setCompletedToday(uint16_t count, const std::string& date, std::vector<TodoistCompletedLogEntry>&& entries);

  // The same for Filter 2's completions, merged into what setCompletedToday()
  // stored for Filter 1 (by task id; see mergeFilter2Tasks). completedToday
  // becomes the merged list's size.
  void mergeCompletedFilter2(std::vector<TodoistCompletedLogEntry>&& entries);

  // Cancels one Cached (not yet pushed) Logs-screen row: removes it from
  // completedTodayEntries, decrements completedToday, and cancels its queued
  // server push via clearPending() so the task is not completed remotely
  // either -- it simply reappears in getTasks() after the next sync re-fetches
  // it, since it was never actually closed on the server. No-op (returns
  // false) for an out-of-range index or a Synced entry (pending=false): a
  // sync has already confirmed that one, and there is nothing local left to
  // cancel.
  bool cancelCompletedLogEntry(size_t displayIndex);

  // Clears today's completion log if `today` (independently resolved by the
  // caller, NOT derived from this cache's own syncDate) has genuinely moved
  // past completedDay -- a no-op if completedDay is already at or ahead of
  // it, the same "only ever advance" rule rolloverCompletedIfNeeded() applies
  // in the other direction, so the two can never fight over completedDay and
  // undo each other's advance. Unlike rolloverCompletedIfNeeded(), this can
  // run before syncDate itself has been refreshed this sync -- see
  // runTasks()'s call site, which calls this right after a fresh NTP
  // resolution, before any network fetch that could still fail.
  //
  // Also drops pendingIds when it rolls over: a completion queued for the
  // server is abandoned, not carried into the new day, if it was not synced
  // before the day it happened on ended -- by design, the same choice this
  // makes for completedToday/completedTodayEntries, so the same "sync the
  // same day or it's lost" rule applies uniformly to everything a local
  // completion touches. Never touches syncDate itself (a different,
  // local-date concept used for overdue flags), pendingReschedules (a
  // reschedule has nothing to do with which day it was made), or the task
  // list.
  void clearCompletedIfStale(uint16_t today);

  // Manually zeroes today's completion log right now, unconditionally -- for
  // a user-triggered "Clear All" action (see QuickPickActivity's Logs tab),
  // independent of clearCompletedIfStale()'s own automatic day-boundary
  // check. Leaves completedDay and pendingIds untouched: this clears what the
  // log already shows for today, not what is still owed to the server -- a
  // completion queued here is still the same day it was made, so it still
  // deserves its sync, same as it would if Clear had never been pressed. A
  // later legitimate completion or sync still rolls over/overwrites correctly
  // regardless of this having run.
  void clearCompletedNow();

 private:
  // Recomputes every task's overdue flag against syncDate. The flag is derived
  // state, so it is set here rather than stored by the parser or the file.
  void applyOverdueFlags();

  // Zeroes completedToday (and abandons pendingIds -- see this method's own
  // .cpp comment) the first time syncDate's day moves past completedDay.
  // Shared by setTasks (a sync can itself roll the day over) and
  // completeTaskAt (a local completion can too, between syncs). Never rolls
  // completedDay backward -- see its own comment for why a syncDate that has
  // not synced yet today must not override a completedDay
  // clearCompletedIfStale() already advanced from the real clock.
  void rolloverCompletedIfNeeded();
};

#define TODOIST_TASKS TodoistTaskCache::getInstance()
