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
// Between the sprite and the tab bar below it.
constexpr int LABEL_GAP = 20;
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

// Same thickness as the header rule LyraTheme::drawHeader() draws under a
// title (LyraTheme.cpp's own `renderer.drawLine(..., 3, true)`) -- used here
// for two rules of this screen's own, bracketing the companion section: one
// where the glance strip ends, one where the tab bar begins, so the
// companion reads as visually separate from both neighbors instead of
// blending into either.
constexpr int SECTION_RULE_LINE_WIDTH = 3;

// Gap between the glance strip's own three lines, and between the strip and
// the bubble below it. Tighter than LABEL_GAP (used below the sprite, where
// the Logs list needs a real section break) -- these three lines are dense
// glance info, not a section boundary of their own.
constexpr int GLANCE_LINE_GAP = 4;
constexpr int GLANCE_STRIP_GAP = 10;
// The gap between a glance line's own leading bullet/icon and its text.
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

// Same dither-overlay technique as OrganizerScreenActivity::dimText() (and
// BleNotificationsActivity's own local copy): this e-ink panel has no real
// greyscale, so "dimmed" text is solid text with a checkerboard of pixels
// punched back out over it. Call right after drawText() at the same
// position -- used for the Calendar glance line's own trailing date/time,
// which reads as secondary detail next to the event's own summary.
void dimText(const GfxRenderer& renderer, const int x, const int y, const int fontId, const char* text) {
  if (text == nullptr || text[0] == '\0') return;
  const int width = renderer.getTextWidth(fontId, text);
  const int height = renderer.getLineHeight(fontId);
  for (int py = y; py < y + height; py++) {
    for (int px = x; px < x + width; px++) {
      if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }
  }
}

// How much of the header's own vertical space this screen's own layout
// treats as occupied, now that the header shows only a theme's
// clock/date/battery combo (see render()'s own comment) rather than the
// full title+subtitle metrics.headerHeight was sized for. Sized for
// LyraTheme's own clock row -- a headerClockIconSize (24px, LyraTheme.cpp)
// icon starting 5px into the header, plus a little breathing room -- now
// that that row matches the glance strip's own icon size instead of the
// smaller text-only reading it used to be. Moot on RoundedRaffTheme, which
// draws nothing into a null-title header at all (see render()'s own
// comment).
constexpr int HEADER_CONTENT_HEIGHT = 36;
// The header-focus highlight (see render()'s own comment) is drawn a little
// shorter than HEADER_CONTENT_HEIGHT, not the full amount: HEADER_CONTENT_
// HEIGHT already includes some breathing room below the actual clock/
// battery content, and using every pixel of it for the highlight too left
// its own bottom edge touching the glance strip's own icon/text right below.
constexpr int HEADER_HIGHLIGHT_HEIGHT = HEADER_CONTENT_HEIGHT - 6;

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
  // Re-reads the glance strip's own Calendar/Budget sources from disk, the
  // same "hydrate on entry" OrganizerScreenActivity::onEnter() -> loadCaches()
  // already does for Calendar/Budget (CalendarActivity/BudgetActivity's own
  // loadCaches() overrides) -- QuickPickActivity extends Activity directly,
  // so it never gets that hook. Without this, Sync All's own reboot (see
  // SyncAllActivity::onExit(), which fires whenever WiFi was activated, i.e.
  // on every real sync) starts these fresh from their constructors, and this
  // screen would otherwise read them exactly as they were before that sync
  // (hasSynced() == false, if this is a first sync) until something else --
  // Calendar/Budget's own onEnter() -- happened to load them first. A no-op
  // (returns false, changes nothing) when the file doesn't exist yet.
  GCAL_EVENTS.loadFromFile();
  YNAB_CATEGORIES.loadFromFile();
  // One I2C read to resolve the calendar day, so currentMood() is cheap from
  // the render path -- same reasoning as HomeActivity::onEnter()'s own call.
  // Needed here specifically because this screen can be reached directly
  // from sleep (CrossPointState reconstruction) without ever passing through
  // Home first.
  COMPANION.refreshForDisplay();
  lastCompanionRefreshMs = millis();
  lastCompanionMood = COMPANION.currentMood();
  logSelectedRow = 0;
  activeTab = Tab::Logs;
  tabBarFocused = false;
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

std::vector<size_t> QuickPickActivity::relevantTaskIndices() const {
  // Same overdue-or-due-today rule quickpick::roll() itself pools from, and
  // currentPickStillEligible()'s own identical task predicate uses -- these
  // are exactly the tasks a Random reroll could land on, so completing/
  // rescheduling one here credits the companion the same way acting on the
  // Logs tab's own suggestion does.
  std::vector<size_t> indices;
  const auto& tasks = TODOIST_TASKS.getTasks();
  const bool knowToday = !TODOIST_TASKS.getSyncDate().empty();
  const uint16_t today = todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());
  indices.reserve(tasks.size());
  for (size_t i = 0; i < tasks.size(); i++) {
    if (knowToday && (tasks[i].overdue || tasks[i].dueDays == today)) indices.push_back(i);
  }
  return indices;
}

std::vector<size_t> QuickPickActivity::relevantHabitIndices() const {
  std::vector<size_t> indices;
  const auto& habits = HABITIFY_HABITS.getHabits();
  indices.reserve(habits.size());
  for (size_t i = 0; i < habits.size(); i++) {
    if (!habits[i].isComplete()) indices.push_back(i);
  }
  return indices;
}

size_t QuickPickActivity::activeTabRowCount() const {
  switch (activeTab) {
    case Tab::Tasks:
      return relevantTaskIndices().size();
    case Tab::Habits:
      return relevantHabitIndices().size();
    case Tab::Logs:
      return logEntries().size();
  }
  return 0;
}

const char* QuickPickActivity::tabLabel(const Tab tab) {
  switch (tab) {
    case Tab::Tasks:
      return tr(STR_COMPANION_TAB_TASKS);
    case Tab::Habits:
      return tr(STR_COMPANION_TAB_HABITS);
    case Tab::Logs:
      return tr(STR_COMPANION_TAB_LOGS);
  }
  return "";
}

void QuickPickActivity::switchTab(const Tab next) {
  if (activeTab == next) return;
  activeTab = next;
  logSelectedRow = 0;
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

void QuickPickActivity::showTaskRowOptions(const size_t cacheIndex) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return;
  // Captured by id, not carried as cacheIndex: the popup below sits on top
  // for as long as the user takes to answer, and every step after this one
  // re-resolves the id to a (possibly different) cache index, the same
  // pattern completeSuggestedTask()/offerRescheduleDatePicker() above
  // already use for itemId.
  const std::string taskId = tasks[cacheIndex].id;
  // Same recurring-task guard showOptions() applies to itemId.
  const bool canReschedule = !tasks[cacheIndex].isRecurring;

  std::vector<std::string> options;
  options.push_back(tr(STR_COMPLETE_TASK));
  options.push_back(tr(STR_FOCUS_SESSION));
  if (canReschedule) options.push_back(tr(STR_RESCHEDULE_TASK));
  const int rescheduleIdx = canReschedule ? 2 : -1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, taskId, rescheduleIdx](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          completeTaskRow(taskId);
        } else if (idx == 1) {
          offerFocusSessionForTask(taskId);
        } else if (idx == rescheduleIdx) {
          offerRescheduleRow(taskId);
        }
      });
}

void QuickPickActivity::completeTaskRow(const std::string& taskId) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == taskId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already

  // Asked rather than done: completing pushes to Todoist and cannot be undone
  // from the device -- same prompt TasksActivity itself shows.
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TODOIST_COMPLETE_PROMPT),
                                                                tasks[cacheIndex].content),
                         [this, taskId](const ActivityResult& result) {
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
                             if (tasks2[i].id == taskId) {
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

void QuickPickActivity::offerRescheduleRow(const std::string& taskId) {
  std::vector<std::string> options;
  options.push_back(tr(STR_PICK_DATE));
  options.push_back(tr(STR_TASKS_TAB_NO_DATE));

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_RESCHEDULE_TASK, std::move(options)),
      [this, taskId](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == 0) {
          offerRescheduleDatePickerRow(taskId);
        } else if (idx == 1) {
          clearTaskDueDateRow(taskId);
        }
      });
}

void QuickPickActivity::offerRescheduleDatePickerRow(const std::string& taskId) {
  // Only ever reached for a non-recurring task -- showTaskRowOptions() leaves
  // Reschedule off the menu entirely for a recurring one.
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == taskId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already
  const uint16_t seed = tasks[cacheIndex].dueDays != todoist::DUE_NONE
                            ? tasks[cacheIndex].dueDays
                            : todoist::dueDaysFromIso(TODOIST_TASKS.getSyncDate().c_str());

  startActivityForResult(std::make_unique<RescheduleTaskActivity>(renderer, mappedInput, seed),
                         [this, taskId](const ActivityResult& result) {
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
                             if (tasks2[i].id == taskId) {
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

void QuickPickActivity::clearTaskDueDateRow(const std::string& taskId) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == taskId) {
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

void QuickPickActivity::offerFocusSessionForTask(const std::string& taskId) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  size_t cacheIndex = tasks.size();
  for (size_t i = 0; i < tasks.size(); i++) {
    if (tasks[i].id == taskId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= tasks.size()) return;  // gone already
  const std::string capturedText = tasks[cacheIndex].content;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, capturedText, taskId](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(capturedText, taskId, /*isHabit=*/false,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
                         });
}

void QuickPickActivity::showHabitRowOptions(const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return;
  const std::string habitId = habits[cacheIndex].id;
  // Same no-unit guard showOptions() applies to itemId -- a habit with no
  // goal has no unit either, so nothing can be logged against it.
  const bool canLog = !habits[cacheIndex].unitSymbol.empty();

  std::vector<std::string> options;
  if (canLog) options.push_back(tr(STR_HABITIFY_LOG));
  options.push_back(tr(STR_COMPLETE_HABIT));
  options.push_back(tr(STR_FOCUS_SESSION));
  const int logIdx = canLog ? 0 : -1;
  const int completeIdx = canLog ? 1 : 0;
  const int focusIdx = canLog ? 2 : 1;

  startActivityForResult(
      std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_OPTIONS, std::move(options)),
      [this, habitId, logIdx, completeIdx, focusIdx](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const int idx = std::get<OptionPickResult>(result.data).index;
        if (idx == logIdx) {
          logHabitRow(habitId);
        } else if (idx == completeIdx) {
          completeHabitRow(habitId);
        } else if (idx == focusIdx) {
          offerFocusSessionForHabit(habitId);
        }
      });
}

void QuickPickActivity::logHabitRow(const std::string& habitId) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == habitId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= habits.size()) return;  // gone already
  if (habits[cacheIndex].unitSymbol.empty()) return;
  const auto& habit = habits[cacheIndex];

  startActivityForResult(std::make_unique<IntervalSelectionActivity>(
                             renderer, mappedInput, "HabitifyLogAmount", StrId::STR_NONE_OPT, 1, 1,
                             MAX_HABIT_LOG_AMOUNT, HABIT_LOG_SMALL_STEP, HABIT_LOG_LARGE_STEP, StrId::STR_NONE_OPT,
                             /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true, StrId::STR_NONE_OPT,
                             habit.name, habit.unitSymbol),
                         [this, habitId](const ActivityResult& result) {
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
                             if (habits2[i].id == habitId) {
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

void QuickPickActivity::completeHabitRow(const std::string& habitId) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == habitId) {
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

void QuickPickActivity::offerFocusSessionForHabit(const std::string& habitId) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  size_t cacheIndex = habits.size();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == habitId) {
      cacheIndex = i;
      break;
    }
  }
  if (cacheIndex >= habits.size()) return;  // gone already
  const std::string capturedText = habits[cacheIndex].name;

  startActivityForResult(std::make_unique<OptionsMenuActivity>(renderer, mappedInput, StrId::STR_FOCUS_SESSION,
                                                               organizerActions::focusSessionDurationOptions()),
                         [this, capturedText, habitId](const ActivityResult& result) {
                           if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
                             swallowConfirmRelease = true;
                           }
                           if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
                             swallowBackRelease = true;
                           }
                           if (result.isCancelled) return;
                           const int idx = std::get<OptionPickResult>(result.data).index;
                           if (idx < 0 || idx >= organizerActions::FOCUS_SESSION_DURATIONS_COUNT) return;
                           organizerActions::beginFocusSession(capturedText, habitId, /*isHabit=*/true,
                                                               organizerActions::FOCUS_SESSION_DURATIONS_MINUTES[idx],
                                                               renderer, mappedInput);
                         });
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Left1)) {
    // Reverse of Left2 below -- see this file's own header comment for the
    // full Logs/Companion/Header loop order.
    switch (focus) {
      case Focus::Companion:
        focus = Focus::Header;
        break;
      case Focus::Header: {
        // Off the top -- wrap to the last row of the active tab, or its tab
        // bar when that tab is empty (three sections, always all reachable
        // regardless of whether the active tab has any rows in it).
        focus = Focus::Logs;
        const size_t count = activeTabRowCount();
        tabBarFocused = count == 0;
        logSelectedRow = count == 0 ? 0 : static_cast<int>(count) - 1;
        break;
      }
      case Focus::Logs:
        if (tabBarFocused) {
          // Off the top of the tab bar -- move onto the companion figure
          // instead of the header directly, continuing the loop order.
          focus = Focus::Companion;
        } else if (logSelectedRow == 0) {
          // Off the top of the row list -- move onto the tab bar, "the
          // start of the Logs section" (see this file's own header
          // comment), not straight to the companion figure.
          tabBarFocused = true;
        } else {
          const size_t count = activeTabRowCount();
          logSelectedRow = static_cast<int>((static_cast<size_t>(logSelectedRow) + count - 1) % count);
        }
        break;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left2)) {
    // Forward direction of the loop: last Logs-section row -> header ->
    // companion -> wraps to the Logs section's own tab bar (see this file's
    // own header comment).
    switch (focus) {
      case Focus::Logs: {
        const size_t count = activeTabRowCount();
        if (tabBarFocused) {
          if (count == 0) {
            // No rows to move onto -- the tab bar doubles as "the last row"
            // when the active tab is empty, so this continues straight to
            // the header, same as the row branch below does.
            focus = Focus::Header;
          } else {
            tabBarFocused = false;
            logSelectedRow = 0;
          }
        } else if (static_cast<size_t>(logSelectedRow) == count - 1) {
          // Off the bottom of the list -- move onto the header instead of
          // wrapping straight to the companion figure.
          focus = Focus::Header;
        } else {
          logSelectedRow = static_cast<int>((static_cast<size_t>(logSelectedRow) + 1) % count);
        }
        break;
      }
      case Focus::Header:
        focus = Focus::Companion;
        break;
      case Focus::Companion:
        // Wraps back onto the tab bar -- "the start of the Logs section"
        // (see this file's own header comment).
        focus = Focus::Logs;
        tabBarFocused = true;
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
        if (tabBarFocused) {
          // Cycles tabs without leaving the tab bar -- same convention
          // every other tabbed screen in this app uses (see this file's own
          // header comment).
          switchTab(nextTab());
          requestUpdate();
          break;
        }
        switch (activeTab) {
          case Tab::Logs: {
            const auto entries = logEntries();
            if (!entries.empty() && logSelectedRow >= 0 && static_cast<size_t>(logSelectedRow) < entries.size()) {
              // Clear -- only meaningful for a Cached row (see
              // clearLogRow()'s own comment); a no-op for a Synced one,
              // matching render()'s own blank confirmLabel there.
              clearLogRow(entries[static_cast<size_t>(logSelectedRow)]);
            }
            break;
          }
          case Tab::Tasks: {
            const auto indices = relevantTaskIndices();
            if (!indices.empty() && logSelectedRow >= 0 && static_cast<size_t>(logSelectedRow) < indices.size()) {
              showTaskRowOptions(indices[static_cast<size_t>(logSelectedRow)]);
            }
            break;
          }
          case Tab::Habits: {
            const auto indices = relevantHabitIndices();
            if (!indices.empty() && logSelectedRow >= 0 && static_cast<size_t>(logSelectedRow) < indices.size()) {
              showHabitRowOptions(indices[static_cast<size_t>(logSelectedRow)]);
            }
            break;
          }
        }
        break;
    }
  }
}

// -- render ---------------------------------------------------------------

void QuickPickActivity::renderLogsTab(const int top, const int height) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;

  // Today's completed tasks/habits -- hoverable (see this file's own header
  // comment), with a Cached/Synced tag next to each one showing whether
  // Right2 (Clear) is actually offered. Focus is on this tab's own rows only
  // when the Logs section is focused AND its tab bar isn't (see
  // tabBarFocused's own comment) -- both other cases mean focus is
  // elsewhere, so no row shows the selection fill.
  const bool rowsFocused = focus == Focus::Logs && !tabBarFocused;
  const auto entries = logEntries();
  if (entries.empty()) {
    // Centred in the section either way -- only the highlight (and the
    // text's own ink) toggles with focus, never its position, so hovering
    // onto it doesn't make it jump.
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int rowY = top + (height - ROW_HEIGHT) / 2;
    if (rowsFocused) {
      // The row list itself is focused, same as when a real row is selected
      // below -- the empty placeholder gets that section's own row-
      // highlight treatment (solid fill, inverted text) rather than the
      // companion's box style, so there is always something to see focused
      // here too.
      renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, rowY + (ROW_HEIGHT - lineH) / 2, tr(STR_LOG_EMPTY), !rowsFocused);
    return;
  }

  const int pageItems = std::max(1, height / ROW_HEIGHT);
  const int pageStart = (logSelectedRow / pageItems) * pageItems;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= static_cast<int>(entries.size())) break;
    const auto& entry = entries[static_cast<size_t>(i)];
    const int rowY = top + row * ROW_HEIGHT;
    const bool selected = rowsFocused && i == logSelectedRow;
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

void QuickPickActivity::renderTasksTab(const int top, const int height) const {
  const auto indices = relevantTaskIndices();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  const bool rowsFocused = focus == Focus::Logs && !tabBarFocused;

  if (indices.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2, tr(STR_TODOIST_NO_TASKS));
    return;
  }

  const int pageItems = std::max(1, height / ROW_HEIGHT);
  const int pageStart = (logSelectedRow / pageItems) * pageItems;
  const auto& tasks = TODOIST_TASKS.getTasks();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= static_cast<int>(indices.size())) break;
    const size_t cacheIndex = indices[static_cast<size_t>(i)];
    if (cacheIndex >= tasks.size()) continue;
    const int rowY = top + row * ROW_HEIGHT;
    const bool selected = rowsFocused && i == logSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);

    // Overdue leads as a small bold tag, so the one thing this filtered list
    // cannot otherwise show (why a task is here) is not lost by keeping
    // this row to one line, the same "plain" geometry Logs' own rows use.
    const char* tag = tasks[cacheIndex].overdue ? tr(STR_OVERDUE) : nullptr;
    int rowX = textX;
    int rowMaxWidth = textWidth;
    if (tag != nullptr) {
      const int tagW = renderer.getTextWidth(UI_10_FONT_ID, tag, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rowX, rowY + (ROW_HEIGHT - lineH) / 2, tag, ink, EpdFontFamily::BOLD);
      rowX += tagW + metrics.contentSidePadding / 2;
      rowMaxWidth -= tagW + metrics.contentSidePadding / 2;
    }
    const auto shown = renderer.truncatedText(UI_10_FONT_ID, tasks[cacheIndex].content.c_str(), rowMaxWidth);
    renderer.drawText(UI_10_FONT_ID, rowX, rowY + (ROW_HEIGHT - lineH) / 2, shown.c_str(), ink);

    const bool lastOnPage = row + 1 >= pageItems || i + 1 >= static_cast<int>(indices.size());
    if (!selected && !lastOnPage) {
      renderer.fillRectDither(textX, rowY + ROW_HEIGHT - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                              Color::LightGray);
    }
  }
}

void QuickPickActivity::renderHabitsTab(const int top, const int height) const {
  const auto indices = relevantHabitIndices();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  const bool rowsFocused = focus == Focus::Logs && !tabBarFocused;

  if (indices.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2, tr(STR_HABITIFY_ALL_DONE));
    return;
  }

  const int pageItems = std::max(1, height / ROW_HEIGHT);
  const int pageStart = (logSelectedRow / pageItems) * pageItems;
  const auto& habits = HABITIFY_HABITS.getHabits();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= static_cast<int>(indices.size())) break;
    const size_t cacheIndex = indices[static_cast<size_t>(i)];
    if (cacheIndex >= habits.size()) continue;
    const auto& habit = habits[cacheIndex];
    const int rowY = top + row * ROW_HEIGHT;
    const bool selected = rowsFocused && i == logSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, ROW_HEIGHT);

    char progress[24];
    // %g rather than %f: a count-based habit reads "1/3", not "1.000000/3.000000".
    if (habit.hasTarget()) {
      snprintf(progress, sizeof(progress), "%g/%g", static_cast<double>(habit.shownCurrent()),
               static_cast<double>(habit.target));
    } else {
      snprintf(progress, sizeof(progress), "%g", static_cast<double>(habit.shownCurrent()));
    }

    const int gap = renderer.getSpaceWidth(UI_10_FONT_ID) * 2;
    const int progressWidth = renderer.getTextWidth(UI_10_FONT_ID, progress);
    const int nameWidth = std::max(0, textWidth - progressWidth - gap);
    const auto shownName = renderer.truncatedText(UI_10_FONT_ID, habit.name.c_str(), nameWidth);
    const int textY = rowY + (ROW_HEIGHT - lineH) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, shownName.c_str(), ink);
    renderer.drawText(UI_10_FONT_ID, textX + textWidth - progressWidth, textY, progress, ink);

    const bool lastOnPage = row + 1 >= pageItems || i + 1 >= static_cast<int>(indices.size());
    if (!selected && !lastOnPage) {
      renderer.fillRectDither(textX, rowY + ROW_HEIGHT - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                              Color::LightGray);
    }
  }
}

std::vector<QuickPickActivity::GlanceEventRow> QuickPickActivity::todaysEvents() const {
  std::vector<GlanceEventRow> rows;
  const uint16_t today = organizerSync::todayLocalDate();
  // Same fallback either way (no usable "today" to filter by is no more
  // useful to the user than a never-synced cache) -- both mean this line
  // cannot promise anything about what's actually happening today.
  if (!GCAL_EVENTS.hasSynced() || today == civil::NO_DATE) {
    rows.push_back(GlanceEventRow{std::string(tr(STR_GCAL_NEVER_SYNCED)), "", true});
    return rows;
  }
  // Exact-day match, not the old ">= today" window: a sync can be hours or
  // days old, and today's own local date (re-derived fresh here) can have
  // moved on since, so an event dated "yesterday" in a stale cache simply
  // no longer matches -- no separate staleness check needed the way the
  // ">= today" comparison this replaced did.
  for (const auto& event : GCAL_EVENTS.getEvents()) {
    if (event.date != today) continue;
    GlanceEventRow row;
    row.title = event.summary;
    if (!event.isAllDay()) {
      char time[10];
      snprintf(time, sizeof(time), ", %02u:%02u", static_cast<unsigned>(event.startMin / 60),
               static_cast<unsigned>(event.startMin % 60));
      row.time = time;
    }
    rows.push_back(std::move(row));
  }
  if (rows.empty()) {
    rows.push_back(GlanceEventRow{std::string(tr(STR_COMPANION_GLANCE_NO_EVENTS_TODAY)), "", true});
  }
  return rows;
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
  // without a theme-level change. metrics.headerHeight itself is sized for
  // a full title+subtitle header this screen no longer draws, so it is
  // still passed to drawHeader() (its own contract, and title/subtitle draw
  // at fixed offsets from rect.y regardless of rect.height anyway), but
  // HEADER_CONTENT_HEIGHT -- not metrics.headerHeight -- is what this
  // screen's own layout below actually uses, so the glance strip sits right
  // under the clock/battery row instead of leaving the empty space
  // metrics.headerHeight otherwise reserved for the title this row no
  // longer has.
  const int headerTop = metrics.topPadding;
  const Rect headerRect{0, headerTop, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, /*title=*/nullptr, /*subtitle=*/nullptr, /*showRule=*/true);

  // Hovering the header (see this file's own header comment): a true pixel
  // invert (GfxRenderer::invertRect(), the same bit-flip invertScreen() does,
  // scoped to this rect) rather than a fillRect() -- fillRect would just
  // paint over the clock/date/battery LyraTheme already drew, hiding it
  // instead of turning it white the way an opaque fill can't without also
  // redrawing (and thus duplicating) that theme's own content. Scoped to
  // HEADER_HIGHLIGHT_HEIGHT (see its own comment), not the full header rect,
  // so only the actual clock/battery row inverts, not the empty space below
  // it or the glance strip's own icon/text right after that.
  if (focus == Focus::Header) {
    renderer.invertRect(headerRect.x, headerRect.y, headerRect.width, HEADER_HIGHLIGHT_HEIGHT);
  }

  // The glance strip (see this file's own header comment) sits right under
  // the header -- purely informational itself (it neither joins the
  // Left1/Left2 loop below nor sits inside either focus highlight), but
  // contentTop (defined after it) is what the companion figure/bubble and
  // its own focus box are actually positioned from. Today's Calendar events
  // only for now -- glanceBudgetWants()/glanceNewestAlert() are temporarily
  // not called here (still defined, for whenever this strip grows back to
  // include them). One bulleted line per event (a plain dot, not that app's
  // own icon: unlike the single-line rows this replaced, there can be more
  // than one), so the row count -- and this strip's own height -- is
  // whatever todaysEvents() actually returns, not a fixed GLANCE_ROW_COUNT.
  const auto glanceRows = todaysEvents();
  const int glanceLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int glanceStripTop = headerTop + HEADER_CONTENT_HEIGHT;
  const int glanceStripHeight = static_cast<int>(glanceRows.size()) * glanceLineH +
                                static_cast<int>(glanceRows.size() - 1) * GLANCE_LINE_GAP + GLANCE_STRIP_GAP;
  {
    constexpr int BULLET_SIZE = 6;
    const int bulletX = metrics.contentSidePadding;
    const int textX = bulletX + BULLET_SIZE + GLANCE_ICON_GAP;
    const int glanceMaxWidth = pageWidth - metrics.contentSidePadding * 2 - BULLET_SIZE - GLANCE_ICON_GAP;
    int rowY = glanceStripTop;
    for (const auto& row : glanceRows) {
      // No bullet for a fallback row (not synced yet / nothing today) --
      // it isn't an event, so it shouldn't read as one.
      if (!row.isFallback) {
        const int bulletY = rowY + (glanceLineH - BULLET_SIZE) / 2;
        renderer.fillRoundedRect(bulletX, bulletY, BULLET_SIZE, BULLET_SIZE, BULLET_SIZE / 2, Color::Black);
      }
      const std::string mainShown = renderer.truncatedText(UI_10_FONT_ID, row.title.c_str(), glanceMaxWidth);
      renderer.drawText(UI_10_FONT_ID, textX, rowY, mainShown.c_str());
      // The event's own time, dimmed as secondary detail next to its title
      // -- empty for an all-day event or a fallback row (see
      // todaysEvents()'s own comment) -- only drawn when the title itself
      // wasn't truncated away, and only as much of it as still fits.
      if (!row.time.empty() && mainShown == row.title) {
        const int mainWidth = renderer.getTextWidth(UI_10_FONT_ID, mainShown.c_str());
        const int dimMaxWidth = glanceMaxWidth - mainWidth;
        if (dimMaxWidth > 0) {
          const int dimX = textX + mainWidth;
          const std::string dimShown = renderer.truncatedText(UI_10_FONT_ID, row.time.c_str(), dimMaxWidth);
          renderer.drawText(UI_10_FONT_ID, dimX, rowY, dimShown.c_str());
          dimText(renderer, dimX, rowY, UI_10_FONT_ID, dimShown.c_str());
        }
      }
      rowY += glanceLineH + GLANCE_LINE_GAP;
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

  // Hovering the companion figure from the Logs section (see this file's own
  // header comment): a rounded selection-box outline bounding the companion
  // section only -- bubble and sprite, nothing below it -- fixed to (almost)
  // the full content width, but never reaching into the Logs section below
  // it: this screen reads as three
  // sections (header, companion, logs), and the highlight should only ever
  // claim the one that's focused. Left and right edges line up with
  // metrics.contentSidePadding -- the same inset the header's own title/
  // status text and the Logs section's own rows use -- rather than this
  // file's own (slightly wider) MARGIN, so the box reads as bounding the
  // same content column everything else on this screen already lines up
  // with. See SELECTION_BOX_LINE_WIDTH's own comment for where this style
  // comes from.
  if (focus == Focus::Companion) {
    const int boxX = metrics.contentSidePadding;
    const int boxWidth = pageWidth - metrics.contentSidePadding * 2;
    const int boxY = contentTop - SELECTION_BOX_PADDING;
    const int boxBottom = spriteTop + spriteH + SELECTION_BOX_PADDING;
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

  // The tab bar -- Tasks, Habits, Logs -- sits at the start of the Logs
  // section (see this file's own header comment), right under the
  // companion figure. tabBarFocused (not activeTab) decides whether it's
  // drawn "focused" (matches OrganizerScreenActivity's own "index 0 is the
  // tab bar" convention, the same one loop() mirrors for Left1/Left2/Right2).
  const int tabBarTop = spriteTop + spriteH + LABEL_GAP;
  std::vector<TabInfo> tabs;
  tabs.reserve(TAB_COUNT);
  for (int i = 0; i < TAB_COUNT; i++) {
    const auto tab = static_cast<Tab>(i);
    tabs.push_back(TabInfo{tabLabel(tab), tab == activeTab});
  }
  GUI.drawTabBar(renderer, Rect{0, tabBarTop, pageWidth, metrics.tabBarHeight}, tabs,
                 focus == Focus::Logs && tabBarFocused);

  const int listTop = tabBarTop + metrics.tabBarHeight;
  const int listHeight = std::max(0, contentBottom - listTop);
  switch (activeTab) {
    case Tab::Logs:
      renderLogsTab(listTop, listHeight);
      break;
    case Tab::Tasks:
      renderTasksTab(listTop, listHeight);
      break;
    case Tab::Habits:
      renderHabitsTab(listTop, listHeight);
      break;
  }

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
    case Focus::Logs:
      if (tabBarFocused) {
        // Same touch OrganizerScreenActivity's own tab bar gives Right2:
        // its label previews the tab a press would switch to.
        confirmLabel = tabLabel(nextTab());
      } else {
        switch (activeTab) {
          case Tab::Logs: {
            const auto entries = logEntries();
            const bool onCachedRow = !entries.empty() && logSelectedRow >= 0 &&
                                     static_cast<size_t>(logSelectedRow) < entries.size() &&
                                     entries[static_cast<size_t>(logSelectedRow)].cached;
            confirmLabel = onCachedRow ? tr(STR_CLEAR_BUTTON) : "";
            break;
          }
          case Tab::Tasks: {
            const auto indices = relevantTaskIndices();
            confirmLabel =
                (!indices.empty() && logSelectedRow >= 0 && static_cast<size_t>(logSelectedRow) < indices.size())
                    ? tr(STR_SELECT)
                    : "";
            break;
          }
          case Tab::Habits: {
            const auto indices = relevantHabitIndices();
            confirmLabel =
                (!indices.empty() && logSelectedRow >= 0 && static_cast<size_t>(logSelectedRow) < indices.size())
                    ? tr(STR_SELECT)
                    : "";
            break;
          }
        }
      }
      break;
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Both rules bracketing the companion section (see SECTION_RULE_LINE_
  // WIDTH's own comment) are drawn last, not at the point their own y is
  // first computed: GUI.drawTabBar()'s own "focused" style fills across the
  // whole bar, including its own top row, which would otherwise paint right
  // over a rule drawn before that call -- drawing both here, after
  // everything else, guarantees neither can be covered by anything that
  // draws at the same y earlier in this function.
  renderer.drawLine(0, glanceStripTop + glanceStripHeight, pageWidth - 1, glanceStripTop + glanceStripHeight,
                    SECTION_RULE_LINE_WIDTH, true);
  renderer.drawLine(0, tabBarTop, pageWidth - 1, tabBarTop, SECTION_RULE_LINE_WIDTH, true);

  renderer.displayBuffer();
}
