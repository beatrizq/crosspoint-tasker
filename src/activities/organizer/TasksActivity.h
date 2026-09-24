#pragma once
#include <TodoistClient.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "OrganizerScreenActivity.h"
#include "TaskTabModel.h"

/**
 * The Tasks screen: whatever the Todoist Filter setting matches, split by date.
 *
 * What arrives is the user's business, not this screen's: the Filter setting
 * holds a Todoist filter query and the sync asks for exactly that. The tabs only
 * partition the result - they no longer decide what it is, which is what the
 * hardcoded "overdue | due before: +30 days" used to do.
 *
 * There is no "all tasks" tab: Overdue, Today and Upcoming split the dated tasks
 * around the date the last sync settled on - before it, on it, after it - and
 * No date collects the rest. A purely date-based filter never returns undated
 * tasks, so No date is empty by construction for one; a filter like "view all"
 * fills all four.
 *
 * The tab bar is built from what is actually there: a tab with no rows is left
 * out entirely, so an inbox with nothing overdue does not carry a dead Overdue
 * tab. That makes the tab set change under the user - after a sync, and after
 * completing the last task in a tab - so it is rebuilt on both, holding the same
 * *kind* of tab selected rather than the same index. Whichever tab is first is
 * the default one the screen opens on (index 0).
 *
 * The three dated tabs are all empty until a sync establishes today: without it
 * there is no before, on or after, and filing a task under a guessed date is
 * worse than not filing it. With no All tab to catch them, dated tasks are
 * simply not listed until that first sync settles the date.
 *
 * A trailing Logs tab always shows too: today's completed tasks, each row
 * tagged Cached (a local completion not yet pushed -- Right2/Select undoes
 * it) or Synced (a sync already confirmed it, nothing local left to undo).
 * Absorbed from the old standalone Logs screen, which combined this with
 * Habits' own completions; HabitsActivity's own Logs tab is the habit half.
 * The tab set/row-matching rules (including Logs) live in TaskTabModel,
 * shared with QuickPickActivity's own scaled-down embedded rendering of this
 * same screen below the companion figure, so the two never disagree.
 */
class TasksActivity final : public OrganizerScreenActivity {
 public:
  // What a tab holds. Not a tab index: which of these are on screen depends on
  // what the filter returned, so the two are mapped through `visibleTabs`.
  // See TaskTabModel.h -- shared with QuickPickActivity's own embedded tab
  // bar.
  using TabKind = taskTabModel::TaskTabKind;

  // selectTaskId, when non-empty, jumps straight to that task on first paint:
  // whichever tab it falls in, at its own row, rather than wherever
  // initialTab/row 0 would otherwise land. One-shot -- cleared once applied,
  // so a later tab switch behaves normally.
  explicit TasksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int initialTab = 0,
                         std::string selectTaskId = "")
      : OrganizerScreenActivity("Tasks", renderer, mappedInput, initialTab), selectTaskId(std::move(selectTaskId)) {}

  void onEnter() override;

 protected:
  const char* screenTitle() const override;
  homeAppOrder::AppId appId() const override { return homeAppOrder::AppId::Tasks; }
  int tabCount() const override { return static_cast<int>(visibleTabs.size()); }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // The due date goes on a second line wherever it tells two rows apart. Not on
  // Today, where every row is due today and the line would repeat the tab's own
  // name, and not on No date, where there is no date to draw, and not on Logs,
  // whose second line is the Cached/Synced tag instead (see drawRow()).
  bool rowsHaveSubtitle() const override;
  const char* rowConfirmLabel() const override;
  void onRowConfirm() override;
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::TASKS; }

 private:
  // The kind on screen at `index`, or ALL when the index is out of range.
  TabKind kindAt(int index) const;
  // The kind the active tab holds.
  TabKind currentKind() const { return kindAt(tab()); }

  // Thin delegations to TaskTabModel (shared with QuickPickActivity's own
  // embedded tab bar -- see TaskTabModel.h).
  bool matchesKind(TabKind kind, size_t cacheIndex) const { return taskTabModel::matchesKind(kind, cacheIndex); }
  int countFor(TabKind kind) const { return taskTabModel::countFor(kind); }

  // Recomputes which tabs have rows, keeping the active *kind* selected where it
  // survives and falling back to the first tab where it does not. Also clamps the selection
  // into the new tab's row count, since a rebuild can shorten the list under it.
  void rebuildTabs();

  // The cache index behind a visible row under one of the five real kinds,
  // or -1 (including whenever the current kind is LOGS -- see
  // logEntryIndexForRow() instead). Scanned rather than cached in a vector:
  // the list is capped at TODOIST_MAX_TASKS, and a stored mapping would have
  // to be rebuilt on every sync, tab switch and completion.
  int cacheIndexForRow(int row) const { return taskTabModel::taskCacheIndexForRow(currentKind(), row); }
  // The index into TODOIST_TASKS.getCompletedTodayEntries() behind visible
  // Logs row `row`, or -1. Only meaningful when currentKind() == LOGS.
  int logEntryIndexForRow(int row) const { return taskTabModel::logEntryIndexForRow(row); }

  // Right2/Select on a Cached Logs row: undoes today's local completion via
  // TodoistTaskCache::cancelCompletedLogEntry(), the same action the old
  // standalone Logs screen offered. No-op on a Synced row (nothing local
  // left to undo).
  void clearSelectedLogRow();

  // Select opens this first, rather than completeSelectedTask() directly --
  // see rowConfirmLabel()/onRowConfirm().
  void showRowOptions();
  // Asks first; performTaskCompletion() is what actually closes the task.
  void completeSelectedTask();
  void performTaskCompletion(int cacheIndex);
  void performTaskSync();
  // The Options menu's "Focus session" entry opens this: a duration picker,
  // then organizerActions::beginFocusSession() for the same task.
  void offerFocusSession(int cacheIndex);
  // The Options menu's "Reschedule" entry opens this: a sub-choice between
  // picking a new date and clearing the due date entirely.
  void offerReschedule(int cacheIndex);
  // "Pick a date" from offerReschedule()'s sub-menu: the date picker itself.
  void offerRescheduleDatePicker(int cacheIndex);
  // "No date" from offerReschedule()'s sub-menu: clears the due date
  // directly, no further confirmation - same immediacy as Complete.
  void clearTaskDueDate(int cacheIndex);

  // Tabs currently on screen, in display order. Always ends with LOGS, so
  // never empty once rebuildTabs() has run; until then this placeholder is
  // OVERDUE, first in tab order, so "stay on the current tab" resolves to
  // index 0 on that first rebuild whether or not Overdue currently exists.
  std::vector<TabKind> visibleTabs{TabKind::OVERDUE};

  // See the constructor comment. Consumed and cleared in onEnter().
  std::string selectTaskId;
};
