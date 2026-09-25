#include "TodoistTaskCache.h"

#include <Logging.h>

#include <algorithm>

void TodoistTaskCache::toJson(JsonDocument& doc) const {
  doc["syncDate"] = syncDate;
  JsonArray arr = doc["tasks"].to<JsonArray>();
  for (const auto& task : tasks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = task.id;
    obj["content"] = task.content;
    // Stored as ISO so the file stays readable; overdue is derived on load and
    // deliberately not written, so the two can never disagree.
    char iso[11];
    todoist::isoFromDueDays(task.dueDays, iso, sizeof(iso));
    if (iso[0] != '\0') obj["due"] = iso;
    obj["isRecurring"] = task.isRecurring;
    // Only written when it says something: Filter 1 alone is what a card
    // written before there were two filters holds.
    if (task.filterMask != TodoistTask::FILTER_1_BIT) obj["filters"] = task.filterMask;
    if (!task.labels.empty()) obj["labels"] = task.labels;
  }
  JsonArray pending = doc["pending"].to<JsonArray>();
  for (const auto& id : pendingIds) {
    pending.add(id);
  }
  JsonArray reschedules = doc["pendingReschedules"].to<JsonArray>();
  for (const auto& reschedule : pendingReschedules) {
    JsonObject obj = reschedules.add<JsonObject>();
    obj["id"] = reschedule.taskId;
    char iso[11];
    todoist::isoFromDueDays(reschedule.dueDays, iso, sizeof(iso));
    obj["due"] = iso;
  }
  doc["completedToday"] = completedToday;
  doc["completedDay"] = completedDay;
  JsonArray entries = doc["completedTodayEntries"].to<JsonArray>();
  for (const auto& entry : completedTodayEntries) {
    JsonObject obj = entries.add<JsonObject>();
    obj["title"] = entry.title;
    if (!entry.taskId.empty()) obj["taskId"] = entry.taskId;
    if (entry.pending) obj["pending"] = true;
    if (entry.filterMask != (TodoistTask::FILTER_1_BIT | TodoistTask::FILTER_2_BIT)) obj["filters"] = entry.filterMask;
  }
}

bool TodoistTaskCache::fromJson(JsonVariantConst doc) {
  tasks.clear();
  pendingIds.clear();
  pendingReschedules.clear();
  syncDate = doc["syncDate"] | "";
  completedToday = doc["completedToday"] | static_cast<uint16_t>(0);
  completedDay = doc["completedDay"] | todoist::DUE_NONE;
  completedTodayEntries.clear();
  JsonArrayConst entriesArr = doc["completedTodayEntries"];
  if (!entriesArr.isNull()) {
    const size_t entryCount = std::min(entriesArr.size(), MAX_COMPLETED_STORED);
    completedTodayEntries.reserve(entryCount);
    for (size_t i = 0; i < entryCount; i++) {
      const char* title = entriesArr[i]["title"] | "";
      if (title[0] == '\0') continue;
      TodoistCompletedLogEntry entry;
      entry.title = title;
      entry.taskId = entriesArr[i]["taskId"] | "";
      entry.pending = entriesArr[i]["pending"] | false;
      entry.filterMask =
          entriesArr[i]["filters"] | static_cast<uint8_t>(TodoistTask::FILTER_1_BIT | TodoistTask::FILTER_2_BIT);
      if ((entry.filterMask & (TodoistTask::FILTER_1_BIT | TodoistTask::FILTER_2_BIT)) == 0) {
        entry.filterMask = TodoistTask::FILTER_1_BIT | TodoistTask::FILTER_2_BIT;
      }
      completedTodayEntries.push_back(std::move(entry));
    }
  } else {
    // Pre-rename save (plain title strings, no id/pending) -- every entry is
    // treated as Synced (pending=false, taskId="") since anything from before
    // this feature existed was never tracked with a cancellable push anyway.
    JsonArrayConst titlesArr = doc["completedTodayTitles"];
    if (!titlesArr.isNull()) {
      const size_t titleCount = std::min(titlesArr.size(), MAX_COMPLETED_STORED);
      completedTodayEntries.reserve(titleCount);
      for (size_t i = 0; i < titleCount; i++) {
        const char* title = titlesArr[i] | "";
        if (title[0] != '\0') completedTodayEntries.push_back({title, "", false});
      }
    }
  }

  JsonArrayConst arr = doc["tasks"].as<JsonArrayConst>();
  tasks.reserve(std::min(arr.size(), MAX_TASKS));
  for (JsonObjectConst obj : arr) {
    if (tasks.size() >= MAX_TASKS) break;
    TodoistTask task;
    task.id = obj["id"] | "";
    task.content = obj["content"] | "";
    task.dueDays = todoist::dueDaysFromIso(obj["due"] | "");
    task.isRecurring = obj["isRecurring"] | false;
    task.labels = obj["labels"] | "";
    if (task.labels.size() > TodoistTask::LABELS_MAX_LEN) task.labels.resize(TodoistTask::LABELS_MAX_LEN);
    task.filterMask = obj["filters"] | static_cast<uint8_t>(TodoistTask::FILTER_1_BIT);
    if ((task.filterMask & (TodoistTask::FILTER_1_BIT | TodoistTask::FILTER_2_BIT)) == 0) {
      task.filterMask = TodoistTask::FILTER_1_BIT;
    }
    if (task.id.empty()) continue;
    tasks.push_back(std::move(task));
  }
  // Flags come from syncDate, not the file. A cache written by the pre-"due"
  // build has no dates, so every task reads as undated until the next sync -
  // the list still renders, it just shows no overdue marks.
  applyOverdueFlags();

  JsonArrayConst pending = doc["pending"].as<JsonArrayConst>();
  pendingIds.reserve(std::min(pending.size(), MAX_PENDING));
  for (JsonVariantConst value : pending) {
    if (pendingIds.size() >= MAX_PENDING) break;
    const char* id = value | "";
    if (id[0] == '\0') continue;
    pendingIds.emplace_back(id);
  }

  JsonArrayConst reschedules = doc["pendingReschedules"].as<JsonArrayConst>();
  pendingReschedules.reserve(std::min(reschedules.size(), MAX_PENDING));
  for (JsonObjectConst obj : reschedules) {
    if (pendingReschedules.size() >= MAX_PENDING) break;
    const char* id = obj["id"] | "";
    if (id[0] == '\0') continue;
    // DUE_NONE is a real, intentional value here -- a pending "clear the due
    // date" reschedule -- not just what a malformed "due" parses to, so it is
    // not skipped the way an empty id is. Dropping it silently would mean a
    // reboot loses that pending sync entirely, the same class of bug a lost
    // pending completion is.
    pendingReschedules.push_back({id, todoist::dueDaysFromIso(obj["due"] | "")});
  }

  LOG_DBG("TDC", "Loaded %zu tasks, %zu pending completions, %zu pending reschedules", tasks.size(), pendingIds.size(),
          pendingReschedules.size());
  return true;
}

size_t TodoistTaskCache::getOverdueCount() const {
  return static_cast<size_t>(std::count_if(tasks.begin(), tasks.end(), [](const TodoistTask& t) { return t.overdue; }));
}

size_t TodoistTaskCache::getDueTodayOrOverdueCount() const {
  const uint16_t today = todoist::dueDaysFromIso(syncDate.c_str());
  if (today == todoist::DUE_NONE) return getOverdueCount();
  return static_cast<size_t>(std::count_if(tasks.begin(), tasks.end(),
                                           [today](const TodoistTask& t) { return t.overdue || t.dueDays == today; }));
}

void TodoistTaskCache::setTasks(std::vector<TodoistTask>&& fetched, const std::string& date) {
  tasks = std::move(fetched);
  if (tasks.size() > MAX_TASKS) tasks.resize(MAX_TASKS);
  // Ascending by due date: oldest overdue first, today's tasks last, undated
  // after those (DUE_NONE is the maximum). Stable, so the server's ordering
  // survives within a date.
  std::stable_sort(tasks.begin(), tasks.end(),
                   [](const TodoistTask& a, const TodoistTask& b) { return a.dueDays < b.dueDays; });
  // An empty date means today could not be established this sync; the header
  // keeps showing the last date it did know rather than falling back to "--".
  if (!date.empty()) syncDate = date;
  applyOverdueFlags();
  rolloverCompletedIfNeeded();
}

void TodoistTaskCache::applyOverdueFlags() {
  const uint16_t threshold = todoist::dueDaysFromIso(syncDate.c_str());
  for (auto& task : tasks) {
    // Undated tasks are never overdue, and nothing is flagged until today is
    // known - guessing would report a wrong count, which is worse than zero.
    task.overdue = threshold != todoist::DUE_NONE && task.dueDays != todoist::DUE_NONE && task.dueDays < threshold;
  }
}

void TodoistTaskCache::completeTaskAt(const size_t index) {
  if (index >= tasks.size()) return;
  if (pendingIds.size() < MAX_PENDING) {
    pendingIds.push_back(tasks[index].id);
  } else {
    LOG_ERR("TDC", "Pending completion queue full (%zu), dropping push for %s", MAX_PENDING, tasks[index].id.c_str());
  }

  rolloverCompletedIfNeeded();
  if (completedToday < UINT16_MAX) completedToday++;
  // Appended for instant feedback on the Logs screen -- Cached (pending),
  // since this is a local, not-yet-pushed completion. The next sync's
  // setCompletedToday() replaces this with the server's authoritative list,
  // same as it does for the count.
  if (completedTodayEntries.size() < MAX_COMPLETED_STORED) {
    completedTodayEntries.push_back({tasks[index].content, tasks[index].id, /*pending=*/true, tasks[index].filterMask});
  }

  tasks.erase(tasks.begin() + static_cast<long>(index));
}

void TodoistTaskCache::setCompletedToday(const uint16_t count, const std::string& date,
                                         std::vector<TodoistCompletedLogEntry>&& entries) {
  if (!date.empty()) syncDate = date;
  completedToday = count;
  completedDay = todoist::dueDaysFromIso(syncDate.c_str());
  // A fetch is authoritative -- every resulting row is Synced (pending=false).
  completedTodayEntries = std::move(entries);
  if (completedTodayEntries.size() > MAX_COMPLETED_STORED) completedTodayEntries.resize(MAX_COMPLETED_STORED);
  for (auto& entry : completedTodayEntries) entry.pending = false;
}

bool TodoistTaskCache::cancelCompletedLogEntry(const size_t displayIndex) {
  if (displayIndex >= completedTodayEntries.size()) return false;
  const auto& entry = completedTodayEntries[displayIndex];
  if (!entry.pending) return false;  // Synced -- nothing local left to cancel.

  if (!entry.taskId.empty()) clearPending(entry.taskId);
  completedTodayEntries.erase(completedTodayEntries.begin() + static_cast<long>(displayIndex));
  if (completedToday > 0) completedToday--;
  return true;
}

void TodoistTaskCache::clearPending(const std::string& id) {
  pendingIds.erase(std::remove(pendingIds.begin(), pendingIds.end(), id), pendingIds.end());
}

void TodoistTaskCache::rescheduleTaskAt(const size_t index, const uint16_t newDueDays) {
  if (index >= tasks.size()) return;
  tasks[index].dueDays = newDueDays;
  applyOverdueFlags();

  const std::string& id = tasks[index].id;
  const auto found = std::find_if(pendingReschedules.begin(), pendingReschedules.end(),
                                  [&id](const TodoistPendingReschedule& p) { return p.taskId == id; });
  if (found != pendingReschedules.end()) {
    found->dueDays = newDueDays;
  } else if (pendingReschedules.size() < MAX_PENDING) {
    pendingReschedules.push_back({id, newDueDays});
  } else {
    LOG_ERR("TDC", "Pending reschedule queue full (%zu), dropping push for %s", MAX_PENDING, id.c_str());
  }
}

void TodoistTaskCache::clearPendingReschedule(const std::string& id) {
  pendingReschedules.erase(std::remove_if(pendingReschedules.begin(), pendingReschedules.end(),
                                          [&id](const TodoistPendingReschedule& p) { return p.taskId == id; }),
                           pendingReschedules.end());
}

void TodoistTaskCache::rolloverCompletedIfNeeded() {
  const uint16_t today = todoist::dueDaysFromIso(syncDate.c_str());
  // Undated ("today" unknown) leaves the counter alone rather than resetting
  // it against a sentinel: the same tolerance applyOverdueFlags() has for not
  // yet knowing what today is.
  //
  // Rolls forward only: completedDay ahead of syncDate's own (possibly stale,
  // not-yet-synced-today) notion of today means clearCompletedIfStale() has
  // already advanced it from the real clock, which is more current than a
  // stale syncDate can be. Treating that as "different, so roll over" would
  // walk completedDay back to the stale day and wipe the very completion this
  // call is in the middle of recording -- exactly what happened completing a
  // task before today's first sync: this function and clearCompletedIfStale()
  // would fight over completedDay, each call here reverting it to yesterday
  // just in time for the next recordActivity() to see "stale" and clear it
  // again, so the completion and the mood credit it should have earned both
  // silently disappeared.
  if (today == todoist::DUE_NONE) return;
  if (completedDay != todoist::DUE_NONE && completedDay >= today) return;
  completedDay = today;
  completedToday = 0;
  completedTodayEntries.clear();
  // A completion queued for the server but not pushed before the day it
  // happened on ended is abandoned rather than carried forward -- by
  // design, per user choice: sync the same day or the completion is lost.
  // The task stays gone from `tasks` (already erased in completeTaskAt())
  // until a real sync re-fetches it, still open, from a server that was
  // never actually told.
  pendingIds.clear();
}

void TodoistTaskCache::clearCompletedIfStale(const uint16_t today) {
  if (today == todoist::DUE_NONE || completedDay == todoist::DUE_NONE) return;
  // Only ever advances -- see rolloverCompletedIfNeeded()'s own comment: the
  // two calls must agree that completedDay never moves backward, or whichever
  // runs second undoes whatever the first one just recorded.
  if (completedDay >= today) return;
  completedDay = today;
  completedToday = 0;
  completedTodayEntries.clear();
  // Same abandon-on-rollover policy as rolloverCompletedIfNeeded()'s own
  // pendingIds.clear() -- whichever of the two calls notices the day changed
  // first, a completion not pushed before then is dropped, not carried into
  // the new day.
  pendingIds.clear();
}

void TodoistTaskCache::clearCompletedNow() {
  completedToday = 0;
  completedTodayEntries.clear();
}
