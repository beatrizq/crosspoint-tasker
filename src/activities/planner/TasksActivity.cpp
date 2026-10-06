#include "TasksActivity.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <TodoistStore.h>
#include <TodoistTaskCache.h>
#include <esp_sntp.h>
#include <time.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PlannerLabels.h"
#include "RescheduleTaskActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/PlannerActions.h"
#include "util/PlannerSync.h"
#include "util/TaskWatchdog.h"

void TasksActivity::loadCaches() {
  TODOIST_TASKS.loadFromFile();
  TODOIST_STORE.loadFromFile();
  rebuildTabs();
}

void TasksActivity::onEnter() {
  PlannerScreenActivity::onEnter();
  if (selectTaskId.empty()) return;

  const std::string targetId = std::move(selectTaskId);
  selectTaskId.clear();  // one-shot, regardless of whether the id is still found below

  const auto& tasks = TODOIST_TASKS.getTasks();
  int targetCacheIndex = -1;
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == targetId) {
      targetCacheIndex = static_cast<int>(i);
      break;
    }
  }
  // Gone (completed/deleted since the pick was made): leave onEnter()'s
  // default selection (tab 0, row 0) rather than landing on nothing.
  if (targetCacheIndex < 0) return;

  // The first tab the task matches; a task that matches none (a dated one
  // before any sync has settled today, say) stays on the default tab 0.
  int targetTab = 0;
  for (size_t t = 0; t < visibleTabs.size(); t++) {
    if (matchesKind(visibleTabs[t], static_cast<size_t>(targetCacheIndex))) {
      targetTab = static_cast<int>(t);
      break;
    }
  }
  setTab(targetTab);

  const int rows = rowCount();
  for (int row = 0; row < rows; row++) {
    if (cacheIndexForRow(row) == targetCacheIndex) {
      selectedIndex = row + 1;
      break;
    }
  }
}

const char* TasksActivity::screenTitle() const { return homeAppOrder::displayName(homeAppOrder::AppId::Tasks); }

TasksActivity::TabKind TasksActivity::kindAt(const int index) const {
  if (index < 0 || static_cast<size_t>(index) >= visibleTabs.size()) return visibleTabs.front();
  return visibleTabs[static_cast<size_t>(index)];
}

const char* TasksActivity::tabLabel(const int index) const { return taskTabModel::tabLabel(kindAt(index)); }

void TasksActivity::rebuildTabs() {
  // The kind selected now, so the same tab stays under the user across a rebuild
  // even though its index may move when a tab ahead of it appears or goes.
  const TabKind wanted = currentKind();
  const int restored = taskTabModel::rebuildVisibleTabs(wanted, visibleTabs);
  // Falls back to the first tab when the selected kind just emptied - completing
  // the last overdue task, say, which takes its tab away while the user is
  // standing on it.
  setTab(restored);

  // The new tab's list can be shorter than the old one, so the row selection has
  // to be pulled back inside it. Index 0 is the tab bar, which is always valid.
  const int rows = rowCount();
  if (selectedRow() >= rows) selectedIndex = rows;
  if (selectedIndex < 0) selectedIndex = 0;
}

// -- rows -------------------------------------------------------------------

int TasksActivity::rowCount() const { return countFor(currentKind()); }

void TasksActivity::drawRow(const RowLayout& layout) const {
  if (currentKind() == TabKind::LOGS) {
    const int entryIndex = logEntryIndexForRow(layout.index);
    if (entryIndex < 0) return;
    const auto& entry = TODOIST_TASKS.getCompletedTodayEntries()[static_cast<size_t>(entryIndex)];
    const auto shown = renderer.truncatedText(layout.titleFont, entry.title.c_str(), layout.width);
    renderer.drawText(layout.titleFont, layout.x, layout.textY, shown.c_str(), layout.ink);
    // Cached/Synced, the same dimmed second-line style the due date uses
    // below -- absorbed from the old standalone Logs screen's row model.
    const int tagY = layout.textY + renderer.getLineHeight(layout.titleFont);
    const char* tag = entry.pending ? tr(STR_LOG_CACHED) : tr(STR_LOG_SYNCED);
    renderer.drawText(layout.subtitleFont, layout.x, tagY, tag, layout.ink);
    dimText(layout.x, tagY, layout.subtitleFont, tag, layout.ink);
    return;
  }

  const int cacheIndex = cacheIndexForRow(layout.index);
  if (cacheIndex < 0) return;
  const auto& task = TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)];
  const auto shown = renderer.truncatedText(layout.titleFont, task.content.c_str(), layout.width);
  renderer.drawText(layout.titleFont, layout.x, layout.textY, shown.c_str(), layout.ink);

  if (!rowsHaveSubtitle()) return;
  // The due date under the task, as the Calendar screen dates an event - same
  // format, because a date reads the same wherever it appears on these screens.
  // Undated tasks draw nothing rather than "--": the row keeps its height, so the
  // list stays even, and an empty line says "no date" more quietly than a dash.
  if (task.dueDays == todoist::DUE_NONE) return;
  char when[16];
  planner::formatDayLabel(task.dueDays, when, sizeof(when));
  const auto shownWhen = renderer.truncatedText(layout.subtitleFont, when, layout.width);
  const int whenY = layout.textY + renderer.getLineHeight(layout.titleFont);
  renderer.drawText(layout.subtitleFont, layout.x, whenY, shownWhen.c_str(), layout.ink);
  // Greyed, so the date stays subordinate to the task rather than competing with
  // it. The smaller font alone was not enough separation.
  dimText(layout.x, whenY, layout.subtitleFont, shownWhen.c_str(), layout.ink);
}

void TasksActivity::formatStatus(char* out, const size_t outSize) const {
  char date[16];
  planner::formatDayLabel(civil::dateFromIso(TODOIST_TASKS.getSyncDate().c_str()), date, sizeof(date));
  if (TODOIST_TASKS.hasPending()) {
    char waiting[32];
    snprintf(waiting, sizeof(waiting), tr(STR_TODOIST_PENDING_COMPLETIONS),
             static_cast<int>(TODOIST_TASKS.pendingSyncCount()));
    snprintf(out, outSize, "%s  ·  %s", date, waiting);
    return;
  }
  snprintf(out, outSize, "%s", date);
}

const char* TasksActivity::emptyMessage() const {
  if (currentKind() == TabKind::LOGS) return tr(STR_LOG_EMPTY);
  if (!TODOIST_TASKS.hasSynced()) return tr(STR_TODOIST_NEVER_SYNCED);
  // Reached on All, since every other tab is hidden when it has no rows. An empty
  // All after a successful sync means the filter matched nothing, which is a
  // different problem from having no tasks - and the one the user can act on.
  return TODOIST_TASKS.getTasks().empty() ? tr(STR_TODOIST_FILTER_NO_MATCH) : tr(STR_TODOIST_NO_TASKS);
}

const char* TasksActivity::syncingMessage() const { return tr(STR_TODOIST_SYNCING); }

// -- completion -------------------------------------------------------------

bool TasksActivity::rowsHaveSubtitle() const { return taskTabModel::rowsHaveSubtitle(currentKind()); }

const char* TasksActivity::rowConfirmLabel() const {
  if (currentKind() == TabKind::LOGS) {
    const int entryIndex = logEntryIndexForRow(selectedRow());
    if (entryIndex < 0) return "";
    return TODOIST_TASKS.getCompletedTodayEntries()[static_cast<size_t>(entryIndex)].pending ? tr(STR_CLEAR_BUTTON)
                                                                                             : "";
  }
  return tr(STR_SELECT);
}

void TasksActivity::onRowConfirm() {
  if (currentKind() == TabKind::LOGS) {
    clearSelectedLogRow();
    return;
  }
  showRowOptions();
}

void TasksActivity::clearSelectedLogRow() {
  const int entryIndex = logEntryIndexForRow(selectedRow());
  if (entryIndex < 0) return;
  const auto& entries = TODOIST_TASKS.getCompletedTodayEntries();
  if (static_cast<size_t>(entryIndex) >= entries.size() || !entries[static_cast<size_t>(entryIndex)].pending) {
    return;  // Synced -- nothing local left to undo.
  }

  {
    // Same reasoning as performTaskCompletion(): the render task reads the
    // list, and clearing a logged completion is as much a change to it as
    // completing one is.
    RenderLock lock(*this);
    TODOIST_TASKS.cancelCompletedLogEntry(static_cast<size_t>(entryIndex));
    TODOIST_TASKS.saveToFile();
    // Same reasoning as every other mutator of today's counts: the
    // companion's mood ladder needs to catch up immediately rather than
    // waiting for the next sync or Home visit.
    COMPANION.recordActivity();
    const int remaining = rowCount();
    if (selectedRow() >= remaining) selectedIndex = remaining;
    if (selectedIndex < 1) selectedIndex = remaining > 0 ? 1 : 0;
  }
  requestUpdate(true);
}

void TasksActivity::showRowOptions() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;

  // Todoist has no way to reschedule a single occurrence of a recurring task
  // without replacing its recurrence entirely, and the warning that fact
  // requires doesn't fit the popup -- simplest and clearest is to just not
  // offer Reschedule for a recurring task at all.
  const bool canReschedule = !TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)].isRecurring;

  std::vector<std::string> options{tr(STR_COMPLETE_TASK), tr(STR_FOCUS_SESSION)};
  if (canReschedule) options.push_back(tr(STR_RESCHEDULE_TASK));
  const int rescheduleIdx = canReschedule ? 2 : -1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, cacheIndex, rescheduleIdx](const ActivityResult& result) {
        // Confirm may still be physically down (the popup answers on the
        // press, this screen on the release) -- same dance completeSelectedTask()
        // does below for the popup it pushes in turn.
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          completeSelectedTask();
        } else if (idx == 1) {
          offerFocusSession(cacheIndex);
        } else if (idx == rescheduleIdx) {
          offerReschedule(cacheIndex);
        }
      });
}

void TasksActivity::offerFocusSession(const int cacheIndex) {
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= TODOIST_TASKS.getTasks().size()) return;
  const std::string text = TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)].content;
  const std::string id = TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)].id;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               plannerActions::focusSessionDurationOptions()),
                         [this, text, id](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= plannerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           plannerActions::beginFocusSession(
                               text, id, plannerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx], renderer, mappedInput);
                         });
}

void TasksActivity::offerReschedule(const int cacheIndex) {
  std::vector<std::string> options;
  options.push_back(tr(STR_PICK_DATE));
  options.push_back(tr(STR_TASKS_TAB_NO_DATE));

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_RESCHEDULE_TASK, std::move(options)),
      [this, cacheIndex](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          offerRescheduleDatePicker(cacheIndex);
        } else if (idx == 1) {
          clearTaskDueDate(cacheIndex);
        }
      });
}

void TasksActivity::offerRescheduleDatePicker(const int cacheIndex) {
  // Only ever reached for a non-recurring task -- showRowOptions() leaves
  // Reschedule off the menu entirely for a recurring one.
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= TODOIST_TASKS.getTasks().size()) return;
  const auto& task = TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)];
  const uint16_t seed =
      task.dueDays != todoist::DUE_NONE ? task.dueDays : todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());

  startActivityForResult(std::make_unique<RescheduleTaskActivity>(renderer, mappedInput, seed),
                         [this, cacheIndex](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const auto* date = std::get_if<DateResult>(&result.data);
                           if (!date) return;
                           if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= TODOIST_TASKS.getTasks().size())
                             return;

                           {
                             // Same reasoning as performTaskCompletion(): the render task reads
                             // the list, and a reschedule can move the task between tabs just
                             // as completing one removes it from all of them.
                             RenderLock lock(*this);
                             plannerActions::rescheduleTask(static_cast<size_t>(cacheIndex), date->packedDate);
                             rebuildTabs();
                           }
                         });
}

void TasksActivity::clearTaskDueDate(const int cacheIndex) {
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= TODOIST_TASKS.getTasks().size()) return;

  {
    // Same reasoning as performTaskCompletion(): the render task reads the
    // list, and clearing a due date can move the task between tabs just as
    // completing one removes it from all of them.
    RenderLock lock(*this);
    plannerActions::rescheduleTask(static_cast<size_t>(cacheIndex), todoist::DUE_NONE);
    rebuildTabs();
  }
}

void TasksActivity::completeSelectedTask() {
  const int cacheIndex = cacheIndexForRow(selectedRow());
  if (cacheIndex < 0) return;

  // Asked rather than done: completing pushes to Todoist and cannot be undone
  // from the device, and Select is the same button that switches tabs one row
  // up. The prompt names the task, because the list is behind it by then, and
  // it opens on Cancel.
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODOIST_COMPLETE_PROMPT),
                                             TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)].content),
      [this, cacheIndex](const ActivityResult& result) {
        // Confirm may still be physically down (the popup answers on the press,
        // this screen on the release). Back is swallowed whenever the result was
        // cancelled at all, since dismissing the popup with Back can itself be
        // release-triggered - by then the button is no longer down, but the
        // release is still what this screen would see next.
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) {
          LOG_DBG("TASKS", "Task completion cancelled");
          return;
        }
        performTaskCompletion(cacheIndex);
      });
}

void TasksActivity::performTaskCompletion(const int cacheIndex) {
  // Re-checked: the prompt sat on top of this screen for as long as the user
  // took to answer, and an index is not a task.
  if (cacheIndex < 0 || static_cast<size_t>(cacheIndex) >= TODOIST_TASKS.getTasks().size()) return;

  {
    // The render task reads the task list; hold the lock across the removal so
    // it never paints a half-updated list.
    RenderLock lock(*this);
    plannerActions::completeTask(static_cast<size_t>(cacheIndex));
    // Completing the last task in a tab takes that tab away, so the bar is rebuilt
    // before the selection is settled. rebuildTabs() keeps the same kind selected
    // where it survives and clamps the row selection itself.
    rebuildTabs();
    const int remaining = rowCount();
    if (selectedRow() >= remaining) selectedIndex = remaining;
    if (selectedIndex < 1) selectedIndex = remaining > 0 ? 1 : 0;
  }
}

// -- sync -------------------------------------------------------------------

void TasksActivity::startSync() {
  if (!TODOIST_STORE.hasToken()) {
    failSync(tr(STR_TODOIST_NO_TOKEN));
    return;
  }
  runSync([this] { performTaskSync(); });
}

void TasksActivity::performTaskSync() {
  // The requests and the cache update live in plannerSync so the home screen's
  // sync-everything can drive the same sequence over one Wi-Fi association. What
  // stays here is what only this screen can do.
  const char* failure = plannerSync::run(plannerSync::Service::Tasks);

  // Drop the radio before repainting; the full teardown happens on the silent
  // reboot in onExit().
  tearDownRadio();

  if (failure == nullptr) {
    // A new result set means new row counts, so which tabs exist changes with it.
    RenderLock lock(*this);
    rebuildTabs();
  }
  finishSync(failure);
}
