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
 * old standalone Logs screen.
 */
namespace taskTabModel {

// Not a tab index: which of these are on screen depends on what the filter
// returned (and, for LOGS, on what's been completed today), so the two are
// mapped through a caller-owned `visibleTabs` vector, same as TasksActivity's
// own convention.
enum class TaskTabKind : uint8_t { OVERDUE, TODAY, UPCOMING, NO_DATE, LOGS };

// Which project's tasks a lookup below covers. ALL_PROJECTS is no restriction at
// all -- what the real Tasks screen uses. Any other value is a
// todoist::hashProjectId() (TodoistTask::projectHash); UNKNOWN_PROJECT is the
// group of tasks whose project is not in the cached project list (no id, or a
// project the last sync could not name), so no task falls out of every group.
constexpr uint32_t ALL_PROJECTS = 0xFFFFFFFFu;
constexpr uint32_t UNKNOWN_PROJECT = 0;

// Whether the live task at `cacheIndex` (TODOIST_TASKS.getTasks()) belongs to
// `kind`, within `projectScope`. Always false for LOGS -- a log row is never a
// live-task cache row; see logEntryIndexForRow() for how a Logs row is actually
// resolved.
bool matchesKind(TaskTabKind kind, size_t cacheIndex, uint32_t projectScope = ALL_PROJECTS);

// How many live tasks are in `projectScope`, whatever tab they would land on --
// what decides which projects the Companion's title row lists.
int countInProject(uint32_t projectScope);

// Row count for `kind`, within `projectScope`: a scan over the live tasks for
// the four real kinds, or all of today's completed entries for LOGS (the log is
// not split by project).
int countFor(TaskTabKind kind, uint32_t projectScope = ALL_PROJECTS);

// The live-task cache index behind visible row `row` under one of the five
// real kinds, or -1. Always -1 for LOGS -- use logEntryIndexForRow() there
// instead; kept as a distinct function (rather than one that silently
// returns a meaningless value) so a caller cannot mix the two index spaces
// up by accident.
int taskCacheIndexForRow(TaskTabKind kind, int row, uint32_t projectScope = ALL_PROJECTS);

// The index into TODOIST_TASKS.getCompletedTodayEntries() behind visible Log
// row `row`, or -1 out of range. Meaningful only under LOGS.
int logEntryIndexForRow(int row);

// Whether a row under `kind` draws a second, dimmed line -- the due date on
// UPCOMING, the one tab where it says something the tab's own name doesn't
// (OVERDUE and TODAY are already a date range, and NO_DATE has none), or the
// Cached/Synced tag for LOGS.
bool rowsHaveSubtitle(TaskTabKind kind);

// Recomputes which tabs currently have rows into `visibleTabs` (LOGS shows
// whenever `includeLogs`, which is what keeps this list from ever being empty;
// OVERDUE/TODAY/UPCOMING/NO_DATE only when countFor() > 0 within
// `projectScope`). With `includeLogs` false the list can come back empty --
// callers that leave Logs out give it a screen of its own. Keeps
// `wanted` selected where it survives the rebuild and falling back to index
// 0 -- the first visible tab -- otherwise. Mirrors TasksActivity::
// rebuildTabs()'s own state machine exactly, so the embedded section and
// the real screen never disagree about the tab set. Returns the index
// `wanted` (or that fallback) landed at in the rebuilt `visibleTabs`. There
// is no "all tasks" tab any more, so a caller that just wants the default
// tab passes OVERDUE (first in tab order): it lands on index 0 whether or
// not that tab currently exists.
int rebuildVisibleTabs(TaskTabKind wanted, std::vector<TaskTabKind>& visibleTabs, uint32_t projectScope = ALL_PROJECTS,
                       bool includeLogs = true);

// Tab bar label for `kind`.
const char* tabLabel(TaskTabKind kind);

}  // namespace taskTabModel
