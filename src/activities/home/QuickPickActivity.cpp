#include "QuickPickActivity.h"

#include <CivilTime.h>
#include <GCalEventCache.h>
#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <I18n.h>
#include <TodoistTaskCache.h>
#include <YnabCategoryCache.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <vector>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/organizer/OrganizerLabels.h"
#include "activities/organizer/RescheduleTaskActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionState.h"
#include "companion/CompanionTracker.h"
#include "companion/QuickPickRoll.h"
#include "components/UITheme.h"
#include "components/icons/bell24.h"
#include "components/icons/budget24.h"
#include "components/icons/calendar24.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/OrganizerActions.h"
#include "util/OrganizerSync.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE
#include "network/BleNotificationQueue.h"
#endif

namespace {
// Bigger than the tabbed design's own figure (was 4): with no tab bar and no
// Tasks/Habits tabs to share room with, the companion is back to being most
// of what this screen is about -- though still short of the original,
// tab-less "full-screen hero" (MAX_SCALE 6), since the Logs list below still
// needs its own share of the space.
constexpr int MAX_SCALE = 5;
constexpr int PAD = 14;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;
// Between the sprite and the Logs list below it.
constexpr int LABEL_GAP = 20;
// Between the Logs list's own mood label and the log list under it.
constexpr int ROW_GAP = 4;
// Floor on the bubble's text column, so a one-word habit name still leaves
// room for the tail and rounded corners rather than shrinking to fit it
// exactly.
constexpr int MIN_BUBBLE_TEXT_WIDTH = 80;
// The companion figure + bubble's own share of the space below the header;
// the Logs list gets the rest. Bumped up from the tabbed design's 45%, for
// the same reason MAX_SCALE grew -- see its own comment.
constexpr int COMPANION_BUDGET_PERCENT = 55;

// Hover outline around the companion figure -- same rounded "outline, not
// fill" selection-box convention the pre-grid-tile Home screen's own
// drawCompanion() used to draw around its whole companion column (see git
// history before 688608bc: SELECTION_LINE_WIDTH/SELECTION_CORNER_RADIUS,
// identical values), and the same one LyraTheme's own grid tile still draws
// around a selected app icon today. Not routed through GUI/BaseTheme: like
// the rest of this file's own bespoke row highlighting, this shape belongs
// to this screen alone.
constexpr int SELECTION_BOX_LINE_WIDTH = 2;
constexpr int SELECTION_BOX_CORNER_RADIUS = 6;
// Inset from the box's own content on every side.
constexpr int SELECTION_BOX_PADDING = 10;

// Gap between the glance strip's own three lines, and between the strip and
// the bubble below it. Tighter than LABEL_GAP (used below the sprite, where
// the Logs list needs a real section break) -- these three lines are dense
// glance info, not a section boundary of their own.
constexpr int GLANCE_LINE_GAP = 4;
constexpr int GLANCE_STRIP_GAP = 10;
// The icon in front of each glance line, and the gap before its text.
constexpr int GLANCE_ICON_SIZE = 24;
constexpr int GLANCE_ICON_GAP = 8;

// Case-insensitive full-string match, for finding the YNAB category literally
// named "Wants" among the user's own (freeform) category names -- the
// existing isYnabInflowCategory() (YnabCategory.h) only checks a prefix,
// which is right for its own job (matching every numbered "Inflow: ..."
// category) but wrong for an exact name like this one.
bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Same bounds HabitsActivity's own number entry uses -- see its own comment
// for why 50/1/5.
constexpr int MAX_HABIT_LOG_AMOUNT = 50;
constexpr int HABIT_LOG_SMALL_STEP = 1;
constexpr int HABIT_LOG_LARGE_STEP = 5;

// The Logs list's own row geometry -- deliberately plain (one line of title
// text, selected row inverted), with a dithered separator between rows since
// nothing here is ever showing more than one selection fill at a time.
constexpr int ROW_HEIGHT = 32;
constexpr int SEPARATOR_HEIGHT = 2;

// Mirrored into CrossPointState rather than fished out reactively when sleep
// happens: keeps whatever the screen is showing durable at all times it's
// up, and needs no RTTI to read back out of a generic Activity* later (this
// build has none). Guarded so re-entering with the exact pick already held
// (a resume from sleep, or a reroll landing back where it started) does not
// cost a redundant SD write.
void mirrorToAppState(const std::string& text, const std::string& itemId, const bool isHabit, const bool poolEmpty) {
  if (APP_STATE.quickPickText == text && APP_STATE.quickPickItemId == itemId && APP_STATE.quickPickIsHabit == isHabit &&
      APP_STATE.quickPickPoolEmpty == poolEmpty) {
    return;
  }
  APP_STATE.quickPickText = text;
  APP_STATE.quickPickItemId = itemId;
  APP_STATE.quickPickIsHabit = isHabit;
  APP_STATE.quickPickPoolEmpty = poolEmpty;
  APP_STATE.saveToFile();
}
}  // namespace

void QuickPickActivity::onEnter() {
  Activity::onEnter();
  mirrorToAppState(pickedText, itemId, isHabit, poolEmpty);
  // One I2C read to resolve the calendar day, so currentMood() is cheap from
  // the render path -- same reasoning as HomeActivity::onEnter()'s own call.
  // Needed here specifically because this screen can be reached directly
  // from sleep (CrossPointState reconstruction) without ever passing through
  // Home first.
  COMPANION.refreshForDisplay();
  lastCompanionRefreshMs = millis();
  lastCompanionMood = COMPANION.currentMood();
  logSelectedRow = 0;
  // Lands focused on the companion, not a log row -- it's the subject of
  // this screen, and starting anywhere else leaves the entry screen with no
  // highlight visible at all when there happen to be no logs yet.
  focus = Focus::Companion;
  requestUpdate(true);
}

std::vector<QuickPickActivity::LogEntry> QuickPickActivity::logEntries() const {
  const auto& taskEntries = TODOIST_TASKS.getCompletedTodayEntries();
  const auto& habits = HABITIFY_HABITS.getHabits();
  std::vector<LogEntry> entries;
  entries.reserve(taskEntries.size() + habits.size());
  for (size_t i = 0; i < taskEntries.size(); i++) {
    LogEntry entry;
    entry.text = taskEntries[i].title;
    entry.isTask = true;
    entry.cached = taskEntries[i].pending;
    entry.taskEntryIndex = i;
    entries.push_back(std::move(entry));
  }
  for (const auto& habit : habits) {
    if (!habit.isComplete()) continue;
    LogEntry entry;
    entry.text = habit.name;
    entry.isTask = false;
    entry.cached = habit.hasPending();
    entry.habitId = habit.id;
    entries.push_back(std::move(entry));
  }
  return entries;
}

void QuickPickActivity::clearLogRow(const LogEntry& entry) {
  if (!entry.cached) return;  // Synced -- nothing local left to undo.
  if (entry.isTask) {
    TODOIST_TASKS.cancelCompletedLogEntry(entry.taskEntryIndex);
    TODOIST_TASKS.saveToFile();
  } else {
    HABITIFY_HABITS.undoLocalCompletion(entry.habitId);
    HABITIFY_HABITS.saveToFile();
  }
  // Same reasoning as every other mutator of today's counts: the companion's
  // mood ladder needs to catch up with what just changed, immediately rather
  // than waiting for the next sync or Home visit.
  COMPANION.recordActivity();
  // The removed row's own slot is now whatever came after it (or, if it was
  // the last row, one past the new end) -- clamp back into range either way.
  const size_t newCount = logEntries().size();
  if (newCount == 0) {
    logSelectedRow = 0;
  } else if (static_cast<size_t>(logSelectedRow) >= newCount) {
    logSelectedRow = static_cast<int>(newCount) - 1;
  }
  requestUpdate(true);
}

void QuickPickActivity::reroll() {
  const auto rolled = quickpick::roll();
  pickedText = rolled.text;
  itemId = rolled.itemId;
  isHabit = rolled.isHabit;
  poolEmpty = rolled.poolEmpty;
  mirrorToAppState(pickedText, itemId, isHabit, poolEmpty);
  requestUpdate();
}

bool QuickPickActivity::currentPickStillEligible() const {
  if (poolEmpty) return false;
  if (isHabit) {
    for (const auto& habit : HABITIFY_HABITS.getHabits()) {
      if (habit.id == itemId) return !habit.isComplete();
    }
    return false;
  }
  // Same overdue-or-due-today rule quickpick::roll() itself filters on -
  // rescheduling can move a task out of the pool exactly as completing one
  // does, just without removing it from the cache.
  const bool knowToday = !TODOIST_TASKS.getSyncDate().empty();
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());
  for (const auto& task : TODOIST_TASKS.getTasks()) {
    if (task.id == itemId) return knowToday && (task.overdue || task.dueDays == today);
  }
  return false;
}

void QuickPickActivity::afterRowAction() {
  if (!currentPickStillEligible()) reroll();
  requestUpdate(true);
}

void QuickPickActivity::showOptions() {
  // A habit with no goal has no unit, so nothing can be logged against it (see
  // logSuggestedHabit()) - Log is left off the menu rather than shown and
  // silently failing. Complete needs no unit, so it is offered either way.
  bool canLog = true;
  if (isHabit) {
    for (const auto& habit : HABITIFY_HABITS.getHabits()) {
      if (habit.id == itemId) {
        canLog = !habit.unitSymbol.empty();
        break;
      }
    }
  }

  // Todoist has no way to reschedule a single occurrence of a recurring task
  // without replacing its recurrence entirely, and the warning that fact
  // requires doesn't fit the popup -- simplest and clearest is to just not
  // offer Reschedule for a recurring task at all.
  bool canReschedule = true;
  if (!isHabit) {
    for (const auto& task : TODOIST_TASKS.getTasks()) {
      if (task.id == itemId) {
        canReschedule = !task.isRecurring;
        break;
      }
    }
  }

  std::vector<std::string> options;
  if (isHabit) {
    if (canLog) options.push_back(tr(STR_HABITIFY_LOG));
    options.push_back(tr(STR_COMPLETE_HABIT));
    options.push_back(tr(STR_FOCUS_SESSION));
  } else {
    options.push_back(tr(STR_COMPLETE_TASK));
    options.push_back(tr(STR_FOCUS_SESSION));
    if (canReschedule) options.push_back(tr(STR_RESCHEDULE_TASK));
  }
  // Positions within the habit branch's own entries; the task branch's are
  // fixed (0/1/[2]) and never overlap with these, since the two branches are
  // mutually exclusive on isHabit.
  const int logIdx = canLog ? 0 : -1;
  const int completeHabitIdx = canLog ? 1 : 0;
  const int habitFocusIdx = canLog ? 2 : 1;
  const int rescheduleIdx = canReschedule ? 2 : -1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, logIdx, completeHabitIdx, habitFocusIdx, rescheduleIdx](const ActivityResult& result) {
        // Right2 may still be physically down (the popup answers on the
        // press, this screen on the release). Right1 is swallowed whenever
        // the result was cancelled at all, since dismissing the popup with
        // Right1 can itself be release-triggered - by then the button is no
        // longer down, but the release is still what this screen would see
        // next.
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (isHabit) {
          if (idx == logIdx) {
            logSuggestedHabit();
          } else if (idx == completeHabitIdx) {
            completeSuggestedHabit();
          } else if (idx == habitFocusIdx) {
            offerFocusSession();
          }
        } else {
          if (idx == 0) {
            completeSuggestedTask();
          } else if (idx == 1) {
            offerFocusSession();
          } else if (idx == rescheduleIdx) {
            offerReschedule();
          }
        }
      });
}

void QuickPickActivity::offerFocusSession() {
  const std::string capturedText = pickedText;
  const std::string capturedItemId = itemId;
  const bool capturedIsHabit = isHabit;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, capturedText, capturedItemId, capturedIsHabit](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(capturedText, capturedItemId, capturedIsHabit,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
                         });
}

void QuickPickActivity::completeSuggestedTask() {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already (completed/deleted since the pick was made)

  // Asked rather than done: completing pushes to Todoist and cannot be undone
  // from the device -- same prompt TasksActivity itself shows.
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODOIST_COMPLETE_PROMPT),
                                                                tasks[cacheIndex].content),
                         [this](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;

                           // Re-resolve: the popup sat on top for as long as the user took to answer.
                           const auto& tasks2 = TODOIST_TASKS.getTasks();
                           size_t idx = tasks2.size();
                           for (size_t i = 0; i < tasks2.size(); i++) {
                             if (tasks2[i].id == itemId) {
                               idx = i;
                               break;
                             }
                           }
                           if (idx < tasks2.size()) {
                             RenderLock lock(*this);
                             organizerActions::completeTask(idx);
                           }
                           afterRowAction();
                         });
}

void QuickPickActivity::offerReschedule() {
  std::vector<std::string> options;
  options.push_back(tr(STR_PICK_DATE));
  options.push_back(tr(STR_TASKS_TAB_NO_DATE));

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_RESCHEDULE_TASK, std::move(options)),
      [this](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          offerRescheduleDatePicker();
        } else if (idx == 1) {
          clearTaskDueDate();
        }
      });
}

void QuickPickActivity::offerRescheduleDatePicker() {
  // Only ever reached for a non-recurring task -- showOptions() leaves
  // Reschedule off the menu entirely for a recurring one.
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already
  const uint16_t seed = tasks[cacheIndex].dueDays != todoist::DUE_NONE
                            ? tasks[cacheIndex].dueDays
                            : todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());

  startActivityForResult(std::make_unique<RescheduleTaskActivity>(renderer, mappedInput, seed),
                         [this](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const auto* date = std::get_if<DateResult>(&result.data);
                           if (!date) return;

                           // Re-resolve: the picker sat on top for as long as the user took to answer.
                           const auto& tasks2 = TODOIST_TASKS.getTasks();
                           size_t idx = tasks2.size();
                           for (size_t i = 0; i < tasks2.size(); i++) {
                             if (tasks2[i].id == itemId) {
                               idx = i;
                               break;
                             }
                           }
                           if (idx < tasks2.size()) {
                             RenderLock lock(*this);
                             organizerActions::rescheduleTask(idx, date->packedDate);
                           }
                           afterRowAction();
                         });
}

void QuickPickActivity::clearTaskDueDate() {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already

  {
    RenderLock lock(*this);
    organizerActions::rescheduleTask(cacheIndex, todoist::DUE_NONE);
  }
  afterRowAction();
}

void QuickPickActivity::logSuggestedHabit() {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= habits.size()) return;  // gone already
  // A habit with no goal has no unit either, so there is nothing to log
  // against it -- same guard HabitsActivity's own row applies.
  if (habits[cacheIndex].unitSymbol.empty()) return;
  const auto& habit = habits[cacheIndex];

  startActivityForResult(std::make_unique<IntervalSelectionActivity>(
                             renderer, mappedInput, "HabitifyLogAmount", StrId::STR_NONE_OPT, 1, 1,
                             MAX_HABIT_LOG_AMOUNT, HABIT_LOG_SMALL_STEP, HABIT_LOG_LARGE_STEP, StrId::STR_NONE_OPT,
                             /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true, StrId::STR_NONE_OPT,
                             habit.name, habit.unitSymbol),
                         [this](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;

                           const auto amount = std::get<IntervalResult>(result.data).value;
                           const auto& habits2 = HABITIFY_HABITS.getHabits();
                           size_t idx = habits2.size();
                           for (size_t i = 0; i < habits2.size(); i++) {
                             if (habits2[i].id == itemId) {
                               idx = i;
                               break;
                             }
                           }
                           if (idx < habits2.size()) {
                             RenderLock lock(*this);
                             organizerActions::logHabit(idx, static_cast<float>(amount));
                           }
                           afterRowAction();
                         });
}

void QuickPickActivity::completeSuggestedHabit() {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == itemId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= habits.size()) return;  // gone already

  {
    RenderLock lock(*this);
    organizerActions::completeHabit(cacheIndex);
  }
  afterRowAction();
}

// -- input --------------------------------------------------------------------

void QuickPickActivity::loop() {
  // Re-checks the companion's mood (in particular, whether it's now inside
  // its sleep window, or a day has rolled over) on the same idle timer
  // HomeActivity::loop() uses -- this screen can sit open just as long (see
  // lastCompanionRefreshMs's own comment in the header), so a repaint is
  // owed here too, not only on the next visit to Home.
  constexpr unsigned long COMPANION_REFRESH_INTERVAL_MS = 60000;
  if (millis() - lastCompanionRefreshMs >= COMPANION_REFRESH_INTERVAL_MS) {
    lastCompanionRefreshMs = millis();
    COMPANION.refreshForDisplay();
    const auto mood = COMPANION.currentMood();
    if (mood != lastCompanionMood) {
      lastCompanionMood = mood;
      requestUpdate();
    }
  }

  // A press seen here is a fresh one, so nothing is owed any more.
  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) swallowBackRelease = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  // Side Up/Down: jump to the previous/next app in the home grid's own
  // order, from wherever the cursor already is -- the same shortcut every
  // other app screen has (see OrganizerScreenActivity/SettingsActivity's own
  // identical block). A fresh press each, same guard reasoning as Right1/
  // Right2 above.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) upPressSeen = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) downPressSeen = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upPressSeen) {
      activityManager.goToApp(homeAppOrder::adjacentVisibleApp(homeAppOrder::AppId::Companion, /*forward=*/false));
    }
    upPressSeen = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downPressSeen) {
      activityManager.goToApp(homeAppOrder::adjacentVisibleApp(homeAppOrder::AppId::Companion, /*forward=*/true));
    }
    downPressSeen = false;
    return;
  }

  // Right1 (the "Apps" button) always leaves -- except while the companion
  // is focused (see this file's own header comment), where it becomes Random.
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    if (swallowBackRelease) {
      // The tail of the press that cancelled a popup pushed from this screen.
      // Acting on it would leave the screen entirely instead of just closing
      // the popup that press already closed.
      swallowBackRelease = false;
      return;
    }
    if (focus == Focus::Companion) {
      if (!poolEmpty) reroll();
      return;
    }
    setResult(QuickPickResult{pickedText, itemId, isHabit, poolEmpty});
    finish();
    return;
  }

  const auto entries = logEntries();
  if (mappedInput.wasReleased(MappedInputManager::Button::Left1)) {
    // Reverse of Left2 below -- see this file's own header comment for the
    // full Logs/Companion/Header loop order.
    switch (focus) {
      case Focus::Companion:
        focus = Focus::Header;
        break;
      case Focus::Header:
        // Off the top -- wrap to the last Logs row, or the empty placeholder
        // when there are none (two more sections, always both reachable
        // regardless of whether Logs has any real rows in it).
        focus = Focus::Logs;
        logSelectedRow = entries.empty() ? 0 : static_cast<int>(entries.size()) - 1;
        break;
      case Focus::Logs:
        if (entries.empty() || logSelectedRow == 0) {
          // Off the top of the list -- move onto the companion figure
          // instead of the header directly, continuing the loop order.
          focus = Focus::Companion;
        } else {
          logSelectedRow = static_cast<int>((logSelectedRow + entries.size() - 1) % entries.size());
        }
        break;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left2)) {
    // Forward direction of the loop: last Logs row -> header -> companion ->
    // wraps to the first row (see this file's own header comment).
    switch (focus) {
      case Focus::Logs:
        if (entries.empty() || static_cast<size_t>(logSelectedRow) == entries.size() - 1) {
          // Off the bottom of the list -- move onto the header instead of
          // wrapping straight to the companion figure.
          focus = Focus::Header;
        } else {
          logSelectedRow = static_cast<int>((static_cast<size_t>(logSelectedRow) + 1) % entries.size());
        }
        break;
      case Focus::Header:
        focus = Focus::Companion;
        break;
      case Focus::Companion:
        // Wraps back to the first row (or the empty placeholder).
        focus = Focus::Logs;
        logSelectedRow = 0;
        break;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    switch (focus) {
      case Focus::Companion:
        if (poolEmpty) {
          setResult(QuickPickResult{pickedText, itemId, isHabit, poolEmpty});
          finish();
          return;
        }
        showOptions();
        break;
      case Focus::Header:
        // No per-row or per-suggestion action to offer for the header itself
        // (see this file's own header comment) -- Sync All instead.
        activityManager.goToSyncAll();
        break;
      case Focus::Logs:
        if (!entries.empty() && logSelectedRow >= 0 && static_cast<size_t>(logSelectedRow) < entries.size()) {
          // Clear -- only meaningful for a Cached row (see clearLogRow()'s
          // own comment); a no-op for a Synced one, matching render()'s own
          // blank confirmLabel there.
          clearLogRow(entries[static_cast<size_t>(logSelectedRow)]);
        }
        break;
    }
  }
}

// -- render ---------------------------------------------------------------

void QuickPickActivity::renderLogsTab(const int top, const int height) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int centreX = pageWidth / 2;
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;

  // Mood label, centred under the companion figure -- same treatment and
  // same SETTINGS.companionShowMoodLabel gating as Home's own drawCompanion(),
  // just without its bulleted today's-tasks/habits-completed lines (today's
  // completions are the log list right below instead).
  int listTop = top;
  if (SETTINGS.companionShowMoodLabel != 0) {
    const char* label = companion::moodLabel(COMPANION.currentMood());
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, centreX - labelW / 2, top, label, true, EpdFontFamily::BOLD);
    listTop = top + renderer.getLineHeight(UI_10_FONT_ID) + ROW_GAP;
  }
  const int listHeight = std::max(0, top + height - listTop);

  // Today's completed tasks/habits -- hoverable (see this file's own header
  // comment), with a Cached/Synced tag next to each one showing whether
  // Right2 (Clear) is actually offered.
  const auto entries = logEntries();
  if (entries.empty()) {
    // Centred in the section either way -- only the highlight (and the
    // text's own ink) toggles with focus, never its position, so hovering
    // onto it doesn't make it jump.
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int rowY = listTop + (listHeight - ROW_HEIGHT) / 2;
    const bool logsFocused = focus == Focus::Logs;
    if (logsFocused) {
      // The Logs section itself is focused, same as when a real row is
      // selected below -- the empty placeholder gets that section's own
      // row-highlight treatment (solid fill, inverted text) rather than the
      // companion's box style, so there is always something to see focused
      // here too.
      renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, rowY + (ROW_HEIGHT - lineH) / 2, tr(STR_LOG_EMPTY), !logsFocused);
    return;
  }

  const int pageItems = std::max(1, listHeight / ROW_HEIGHT);
  const int pageStart = (logSelectedRow / pageItems) * pageItems;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= static_cast<int>(entries.size())) break;
    const auto& entry = entries[static_cast<size_t>(i)];
    const int rowY = listTop + row * ROW_HEIGHT;
    // Focus is elsewhere (companion or header), not on any row -- see this
    // file's own header comment -- so no row shows the selection fill.
    const bool selected = focus == Focus::Logs && i == logSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);

    const char* tag = entry.cached ? tr(STR_LOG_CACHED) : tr(STR_LOG_SYNCED);
    const int tagW = renderer.getTextWidth(UI_10_FONT_ID, tag);
    const int rowMaxWidth = textWidth - tagW - metrics.contentSidePadding / 2;
    const auto shown = renderer.truncatedText(UI_10_FONT_ID, entry.text.c_str(), rowMaxWidth);
    renderer.drawText(UI_10_FONT_ID, textX, rowY + (ROW_HEIGHT - lineH) / 2, shown.c_str(), ink);
    renderer.drawText(UI_10_FONT_ID, textX + textWidth - tagW, rowY + (ROW_HEIGHT - lineH) / 2, tag, ink);

    const bool lastOnPage = row + 1 >= pageItems || i + 1 >= static_cast<int>(entries.size());
    if (!selected && !lastOnPage) {
      renderer.fillRectDither(textX, rowY + ROW_HEIGHT - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                              Color::LightGray);
    }
  }
}

std::string QuickPickActivity::glanceNextEvent() const {
  // No text label here -- the calendar icon next to this line is the label
  // (see render()'s own glance-strip drawing).
  char line[160];
  if (!GCAL_EVENTS.hasSynced()) {
    return std::string(tr(STR_GCAL_NEVER_SYNCED));
  }
  // GCalClient's own fetch window already starts at "today" as of the last
  // sync (see OrganizerSync.cpp's own fetchEvents() call), but a sync can be
  // hours or days old -- today's own local date (organizerSync::
  // todayLocalDate(), re-derived fresh here) can have moved on since, so the
  // cached front() entry is not trusted blindly.
  const uint16_t today = organizerSync::todayLocalDate();
  for (const auto& event : GCAL_EVENTS.getEvents()) {
    if (event.date == civil::NO_DATE) continue;
    if (today != civil::NO_DATE && event.date < today) continue;
    char when[24];
    organizer::formatDayLabel(event.date, when, sizeof(when));
    if (event.isAllDay()) {
      snprintf(line, sizeof(line), "%s, %s", event.summary.c_str(), when);
    } else {
      snprintf(line, sizeof(line), "%s, %s %02u:%02u", event.summary.c_str(), when,
               static_cast<unsigned>(event.startMin / 60), static_cast<unsigned>(event.startMin % 60));
    }
    return std::string(line);
  }
  return std::string(tr(STR_GCAL_NO_EVENTS));
}

std::string QuickPickActivity::glanceBudgetWants() const {
  // No text label here -- the budget icon next to this line is the label
  // (see render()'s own glance-strip drawing).
  if (!YNAB_CATEGORIES.hasSynced()) {
    return std::string(tr(STR_YNAB_NEVER_SYNCED));
  }
  for (const auto& category : YNAB_CATEGORIES.getCategories()) {
    if (equalsIgnoreCase(category.name, "Wants")) {
      return category.balance;
    }
  }
  return std::string(tr(STR_COMPANION_GLANCE_NO_WANTS));
}

std::string QuickPickActivity::glanceNewestAlert() const {
  // No text label here -- the bell icon next to this line is the label (see
  // render()'s own glance-strip drawing).
#ifdef ENABLE_BLE_NOTIFY_SPIKE
  // Always index 0 (newest), fresh every render -- no dismiss/cursor state:
  // the line simply shows whatever the newest alert currently is, and gets
  // replaced the moment a newer one arrives.
  if (BLE_NOTIFICATIONS.getCount() > 0) {
    const auto& entry = BLE_NOTIFICATIONS.getEntry(0);
    char line[192];
    if (entry.content[0] != '\0') {
      snprintf(line, sizeof(line), "%s: %s", entry.title, entry.content);
    } else {
      snprintf(line, sizeof(line), "%s", entry.title);
    }
    return std::string(line);
  }
#endif
  return std::string(tr(STR_BLE_NO_NOTIFICATIONS));
}

void QuickPickActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Header stays at the very top, same as every other screen -- on Lyra/
  // Lyra 3 Covers this is also where that theme's own clock+date+battery
  // combo lives (LyraTheme::drawHeader draws it unconditionally into
  // whatever rect it's given, regardless of title/subtitle); this screen
  // does not draw a copy of that itself. Title and subtitle (the companion's
  // name and its Age/Highscore) are both deliberately passed as nullptr --
  // this header is the clock+date+battery row alone. On Lyra, title's own
  // text and the header's rule underneath it are both drawn inside the same
  // `if (title)` block (LyraTheme.cpp), and subtitle inside its own
  // `if (subtitle)` block, so nullptr drops both while leaving the
  // clock/date/battery combo above (drawn unconditionally by that same
  // function) untouched. This is Lyra-specific: RoundedRaffTheme's own
  // drawHeader() returns immediately on a null title, so on that theme this
  // whole header band goes blank (battery included) rather than losing just
  // the title/subtitle -- not something this screen can route around
  // without a theme-level change.
  const int headerTop = metrics.topPadding;
  const Rect headerRect{0, headerTop, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, /*title=*/nullptr, /*subtitle=*/nullptr, /*showRule=*/true);

  // Hovering the header (see this file's own header comment): drawn
  // inverted -- a straight black fill on top of whatever the active theme
  // just drew above (on Lyra, its own clock/date/battery, simply covered
  // while focused) -- rather than asking the theme for an inverted variant.
  // This screen already owns its other focus highlights directly (see
  // SELECTION_BOX_LINE_WIDTH's own comment), and duplicating any one
  // theme's own header layout here would both cross that boundary and only
  // look right on whichever theme happens to be active. Nothing of this
  // screen's own to redraw on top -- there's no title/subtitle left to show.
  if (focus == Focus::Header) {
    renderer.fillRect(headerRect.x, headerRect.y, headerRect.width, headerRect.height);
  }

  // The glance strip (see this file's own header comment) sits right under
  // the header -- purely informational itself (it neither joins the
  // Left1/Left2 loop below nor sits inside either focus highlight), but
  // contentTop (defined after it) is what the companion figure/bubble and
  // its own focus box are actually positioned from. Each line is led by
  // that app's own icon (Calendar/Budget/Bell) instead of a text label --
  // GLANCE_ICON_SIZE (24px) is a size no theme's icon set already had (only
  // 32px/80px assets existed for these three), generated via
  // scripts/convert_icon.py from the existing larger bitmaps rather than
  // original source art, since the pixel data is all this needed.
  const std::string glanceLines[3] = {glanceNextEvent(), glanceBudgetWants(), glanceNewestAlert()};
  static const uint8_t* const GLANCE_ICONS[3] = {Calendar24Icon, Budget24Icon, Bell24Icon};
  const int glanceLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int glanceRowH = std::max(glanceLineH, GLANCE_ICON_SIZE);
  const int glanceStripTop = headerTop + metrics.headerHeight;
  const int glanceStripHeight = glanceRowH * 3 + GLANCE_LINE_GAP * 2 + GLANCE_STRIP_GAP;
  {
    const int iconX = metrics.contentSidePadding;
    const int textX = iconX + GLANCE_ICON_SIZE + GLANCE_ICON_GAP;
    const int glanceMaxWidth = pageWidth - metrics.contentSidePadding * 2 - GLANCE_ICON_SIZE - GLANCE_ICON_GAP;
    int rowY = glanceStripTop;
    for (int i = 0; i < 3; i++) {
      renderer.drawIcon(GLANCE_ICONS[i], iconX, rowY, GLANCE_ICON_SIZE);
      const std::string truncated = renderer.truncatedText(UI_10_FONT_ID, glanceLines[i].c_str(), glanceMaxWidth);
      renderer.drawText(UI_10_FONT_ID, textX, rowY + (glanceRowH - glanceLineH) / 2, truncated.c_str());
      rowY += glanceRowH + GLANCE_LINE_GAP;
    }
  }

  // SELECTION_BOX_PADDING here, not metrics.verticalSpacing: the companion
  // section's own box highlight is drawn at contentTop - SELECTION_BOX_PADDING
  // (see below), and the point of this offset is landing that box's own top
  // edge exactly on the glance strip's bottom edge, rather than leaving the
  // old, larger vertical gap below it.
  const int contentTop = glanceStripTop + glanceStripHeight + SELECTION_BOX_PADDING;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int totalContentHeight = contentBottom - contentTop;
  const int contentWidth = pageWidth - MARGIN * 2;
  const int maxTextWidth = contentWidth - PAD * 2;

  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();

  // Only ever a task/habit suggestion, or the sleeping/empty text -- never a
  // BLE notification (see this file's own header comment).
  const std::string text = mood == companion::Mood::Sleeping ? std::string(tr(STR_COMPANION_SLEEPING_BUBBLE))
                           : poolEmpty                       ? std::string(tr(STR_QUICK_PICK_EMPTY))
                                                             : pickedText;
  const auto textFit = companion::fitBubbleText(renderer, UI_10_FONT_ID, text, maxTextWidth, MIN_BUBBLE_TEXT_WIDTH, 4);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bubbleH = static_cast<int>(textFit.lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = bubbleH + TAIL_LENGTH + BUBBLE_GAP;
  const int bubbleWidth = textFit.textWidth + PAD * 2;

  // The companion figure (bubble + sprite) gets a fixed share of the space
  // below the header; the Logs list gets what is left over.
  const int companionBudget = totalContentHeight * COMPANION_BUDGET_PERCENT / 100;
  int scale = 1;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) > contentWidth) continue;
    if (bubbleBlock + companion::poseHeight(candidate) <= companionBudget) {
      scale = candidate;
      break;
    }
  }

  const int spriteW = companion::poseWidth(scale);
  const int spriteH = companion::poseHeight(scale);
  const int centreX = pageWidth / 2;
  const int bubbleX = centreX - bubbleWidth / 2;
  const int spriteTop = contentTop + bubbleBlock;

  // Mirrors renderLogsTab()'s own mood-label placement exactly (same
  // SETTINGS.companionShowMoodLabel gate; LABEL_GAP is the same gap
  // renderLogsTab()'s own `top` param already carries from spriteTop+spriteH,
  // since that is where it actually draws the label) so the box below can
  // bound the companion section -- label included, not cut through --
  // without renderLogsTab needing to report its own layout back out to
  // render().
  const int moodLabelBlockHeight =
      SETTINGS.companionShowMoodLabel != 0 ? LABEL_GAP + renderer.getLineHeight(UI_10_FONT_ID) : 0;

  // Hovering the companion figure from the Logs list (see this file's own
  // header comment): a rounded selection-box outline bounding the companion
  // section only -- bubble, sprite and the mood label under it -- fixed to
  // (almost) the full content width, but never reaching into the Logs
  // section below it: this screen reads as three sections (header,
  // companion, logs), and the highlight should only ever claim the one
  // that's focused. Left
  // and right edges line up with metrics.contentSidePadding -- the same
  // inset the header's own title/status text and the Logs section's own
  // rows use -- rather than this file's own (slightly wider) MARGIN, so the
  // box reads as bounding the same content column everything else on this
  // screen already lines up with. See SELECTION_BOX_LINE_WIDTH's own comment
  // for where this style comes from.
  if (focus == Focus::Companion) {
    const int boxX = metrics.contentSidePadding;
    const int boxWidth = pageWidth - metrics.contentSidePadding * 2;
    const int boxY = contentTop - SELECTION_BOX_PADDING;
    const int boxBottom = spriteTop + spriteH + moodLabelBlockHeight + SELECTION_BOX_PADDING;
    renderer.drawRoundedRect(boxX, boxY, boxWidth, boxBottom - boxY, SELECTION_BOX_LINE_WIDTH,
                             SELECTION_BOX_CORNER_RADIUS, true);
  }

  companion::drawSpeechBubble(renderer, bubbleX, contentTop, bubbleWidth, bubbleH, TAIL_LENGTH,
                              companion::TailSide::Bottom);
  const Rect textBounds{bubbleX + PAD, contentTop, textFit.textWidth, bubbleH};
  int textY = contentTop + PAD;
  for (const auto& line : textFit.lines) {
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str());
    textY += lineH;
  }

  companion::drawPose(renderer, id, mood, centreX - spriteW / 2, spriteTop, scale);

  const int listTop = spriteTop + spriteH + LABEL_GAP;
  const int listHeight = std::max(0, contentBottom - listTop);
  renderLogsTab(listTop, listHeight);

  // Right1 always lands on Home in every launch path this screen has (see
  // this file's own header comment) -- whether that is really finish()
  // popping back to the HomeActivity caller (activateCompanion()) or a
  // replaceActivity() hand-off with no caller pushed at all
  // (FocusSessionActivity, the boot quick-resume path), never a return to
  // some other, non-Home screen -- except while the companion is focused,
  // where it is Random instead, so this says Home in every OTHER state.
  const char* backLabel = tr(STR_HOME);
  const char* confirmLabel = "";
  const char* leftLabel = tr(STR_DIR_UP);
  const char* rightLabel = tr(STR_DIR_DOWN);
  switch (focus) {
    case Focus::Companion:
      // Hovering the companion figure exposes the suggestion actions on
      // Right1/Right2 rather than a row's own Left1/Left2 (which are busy
      // continuing the circular loop here).
      backLabel = poolEmpty ? "" : tr(STR_QUICK_PICK_RANDOM);
      confirmLabel = poolEmpty ? "" : tr(STR_SELECT);
      break;
    case Focus::Header:
      // Right1 stays Home (the default above); Right2 is Sync All instead of
      // Clear/Select -- there's no per-row or per-suggestion action to offer
      // for the header itself.
      confirmLabel = tr(STR_SYNC_ALL);
      break;
    case Focus::Logs: {
      const auto entries = logEntries();
      const bool onCachedRow = !entries.empty() && logSelectedRow >= 0 &&
                               static_cast<size_t>(logSelectedRow) < entries.size() &&
                               entries[static_cast<size_t>(logSelectedRow)].cached;
      confirmLabel = onCachedRow ? tr(STR_CLEAR_BUTTON) : "";
      break;
    }
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
