#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * The Tasks tab set and row-resolution rules, shared between the real
 * TasksActivity screen and QuickPickActivity's own scaled-down embedded
 * rendering of it below the companion figure -- extracted so the two never
 * disagree about which task is in which tab, or about the "keep the same
 * kind selected across a rebuild" state machine (see TasksActivity's own
 * class doc comment for the full reasoning behind that).
 *
 * LOGS is new here relative to TasksActivity's original tab set: today's
 * completed tasks (TodoistTaskCache::getCompletedTodayEntries(), a distinct
 * vector from the live task list, not a filter over it) -- absorbed from the
 * old standalone Logs screen, which combined this with Habits' own
 * completions; HabitsActivity's own Logs tab is the habit half.
 */
namespace taskTabModel {

// Not a tab index: which of these are on screen depends on what the filter
// returned (and, for LOGS, on what's been completed today), so the two are
// mapped through a caller-owned `visibleTabs` vector, same as TasksActivity's
// own convention.
enum class TaskTabKind : uint8_t { OVERDUE, TODAY, UPCOMING, NO_DATE, LOGS };

// Whether the live task at `cacheIndex` (TODOIST_TASKS.getTasks()) belongs to
// `kind`. Always false for LOGS -- a log row is never a live-task cache row;
// see logEntryIndexForRow() for how a Logs row is actually resolved.
bool matchesKind(TaskTabKind kind, size_t cacheIndex);

// Row count for `kind`: a scan over the live tasks for the five real kinds,
// or TODOIST_TASKS.getCompletedTodayEntries().size() for LOGS.
int countFor(TaskTabKind kind);

// The live-task cache index behind visible row `row` under one of the five
// real kinds, or -1. Always -1 for LOGS -- use logEntryIndexForRow() there
// instead; kept as a distinct function (rather than one that silently
// returns a meaningless value) so a caller cannot mix the two index spaces
// up by accident.
int taskCacheIndexForRow(TaskTabKind kind, int row);

// The index into TODOIST_TASKS.getCompletedTodayEntries() behind visible Log
// row `row`, or -1 out of range. Meaningful only under LOGS.
int logEntryIndexForRow(int row);

// Whether a row under `kind` draws a second, dimmed line -- the due date for
// the real kinds (skipped on TODAY, where it would just repeat the tab's own
// name, and NO_DATE, where there is no date), or the Cached/Synced tag for
// LOGS.
bool rowsHaveSubtitle(TaskTabKind kind);

// Recomputes which tabs currently have rows into `visibleTabs` (LOGS always
// shows; OVERDUE/TODAY/UPCOMING/NO_DATE only when countFor() > 0), keeping
// `wanted` selected where it survives the rebuild and falling back to index
// 0 -- the first visible tab -- otherwise. Mirrors TasksActivity::
// rebuildTabs()'s own state machine exactly, so the embedded section and
// the real screen never disagree about the tab set. Returns the index
// `wanted` (or that fallback) landed at in the rebuilt `visibleTabs`. There
// is no "all tasks" tab any more, so a caller that just wants the default
// tab passes OVERDUE (first in tab order): it lands on index 0 whether or
// not that tab currently exists.
int rebuildVisibleTabs(TaskTabKind wanted, std::vector<TaskTabKind>& visibleTabs);

// Tab bar label for `kind`.
const char* tabLabel(TaskTabKind kind);

}  // namespace taskTabModel
