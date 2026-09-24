#include "TaskTabModel.h"

#include <I18n.h>
#include <TodoistTaskCache.h>

namespace taskTabModel {

bool matchesKind(const TaskTabKind kind, const size_t cacheIndex) {
  if (kind == TaskTabKind::LOGS) return false;

  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return false;
  const TodoistTask& task = tasks[cacheIndex];

  // Today, as the last sync settled it. DUE_NONE when nothing has synced or
  // the date could not be established, which is why the three dated kinds
  // check it: without today there is no before, on, or after to sort a task
  // into, and guessing would file it under the wrong one.
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());
  const bool dated = task.dueDays != todoist::DUE_NONE;
  const bool knowToday = today != todoist::DUE_NONE;

  switch (kind) {
    case TaskTabKind::OVERDUE:
      // The cache owns this flag, against the same date, so the tab agrees
      // with the overdue count elsewhere by construction.
      return task.overdue;
    case TaskTabKind::TODAY:
      return knowToday && dated && task.dueDays == today;
    case TaskTabKind::UPCOMING:
      // Strictly after today, and dated: DUE_NONE is the maximum, so an
      // undated task would otherwise read as the furthest-future one there
      // is.
      return knowToday && dated && task.dueDays > today;
    case TaskTabKind::NO_DATE:
      // No date needs no date: this is the one kind that means something
      // before a sync has worked out what today is.
      return !dated;
    case TaskTabKind::LOGS:
      return false;
  }
  return false;
}

int countFor(const TaskTabKind kind) {
  if (kind == TaskTabKind::LOGS) return static_cast<int>(TODOIST_TASKS.getCompletedTodayEntries().size());

  const auto& tasks = TODOIST_TASKS.getTasks();
  int count = 0;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (matchesKind(kind, i)) count++;
  }
  return count;
}

int taskCacheIndexForRow(const TaskTabKind kind, const int row) {
  if (row < 0 || kind == TaskTabKind::LOGS) return -1;
  const auto& tasks = TODOIST_TASKS.getTasks();
  int seen = 0;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (!matchesKind(kind, i)) continue;
    if (seen == row) return static_cast<int>(i);
    seen++;
  }
  return -1;
}

int logEntryIndexForRow(const int row) {
  if (row < 0 || static_cast<size_t>(row) >= TODOIST_TASKS.getCompletedTodayEntries().size()) return -1;
  return row;
}

bool rowsHaveSubtitle(const TaskTabKind kind) { return kind == TaskTabKind::UPCOMING || kind == TaskTabKind::LOGS; }

int rebuildVisibleTabs(const TaskTabKind wanted, std::vector<TaskTabKind>& visibleTabs) {
  visibleTabs.clear();
  visibleTabs.reserve(5);
  // Each of these earns its place by having rows, so an inbox with nothing
  // overdue carries no dead Overdue tab.
  for (const TaskTabKind kind :
       {TaskTabKind::OVERDUE, TaskTabKind::TODAY, TaskTabKind::UPCOMING, TaskTabKind::NO_DATE}) {
    if (countFor(kind) > 0) visibleTabs.push_back(kind);
  }
  // Logs always shows -- it's a log view, not a filter, so an empty state
  // ("nothing completed today yet") is itself useful feedback rather than
  // noise to hide. It's also what keeps this list from ever being empty.
  visibleTabs.push_back(TaskTabKind::LOGS);

  for (size_t i = 0; i < visibleTabs.size(); i++) {
    if (visibleTabs[i] == wanted) return static_cast<int>(i);
  }
  // Falls back to the first visible tab when the selected kind just emptied
  // - completing the last overdue task, say, which takes its tab away while
  // the user is standing on it.
  return 0;
}

const char* tabLabel(const TaskTabKind kind) {
  switch (kind) {
    case TaskTabKind::OVERDUE:
      return tr(STR_OVERDUE);
    case TaskTabKind::TODAY:
      return tr(STR_TODAY);
    case TaskTabKind::UPCOMING:
      return tr(STR_TASKS_TAB_UPCOMING);
    case TaskTabKind::NO_DATE:
      return tr(STR_TASKS_TAB_NO_DATE);
    case TaskTabKind::LOGS:
      return tr(STR_LOGS);
  }
  return "";
}

}  // namespace taskTabModel
