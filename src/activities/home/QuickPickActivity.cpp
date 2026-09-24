#include "QuickPickActivity.h"

#include <CivilTime.h>
#include <GCalEventCache.h>
#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <I18n.h>
#include <TodoistTaskCache.h>
#include <YnabCategoryCache.h>
#include <esp_random.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/organizer/OrganizerLabels.h"
#include "activities/organizer/RescheduleTaskActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/OptionsMenuActivity.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "components/icons/bell24.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#include "util/OrganizerActions.h"
#include "util/OrganizerSync.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE
#include "companion/CompanionAlertBubble.h"
#include "network/BleNotificationQueue.h"
#endif

namespace {
// One integer step down from a prior 5 (a user-requested ~15% size
// reduction) -- the sprite renderer only supports whole-number scale steps
// (nearest-neighbor pixel blocks, no fractional scaling), so this is the
// closest available size.
constexpr int MAX_SCALE = 4;
constexpr int PAD = 14;
constexpr int TAIL_LENGTH = 16;
constexpr int BUBBLE_GAP = 4;
constexpr int MARGIN = 24;
// Between the sprite and the embedded Tasks section's own title below it.
constexpr int LABEL_GAP = 20;
// Floor on the bubble's text column, so a one-word habit name still leaves
// room for the tail and rounded corners rather than shrinking to fit it
// exactly.
constexpr int MIN_BUBBLE_TEXT_WIDTH = 80;
// The companion figure + bubble's own share of the space below the header;
// the embedded Tasks section gets the rest.
constexpr int COMPANION_BUDGET_PERCENT = 55;

// Gap between the glance strip above the companion figure and the bubble
// below it (hovering the figure inverts the bubble itself -- see render()'s
// own comment -- rather than drawing a selection box around this gap).
constexpr int SELECTION_BOX_PADDING = 10;

// This screen's own rule at the top of the Tasks/Habits section, so it reads
// as visually separate from the companion above it. The same weight as the
// line LyraTheme::drawTabBar() draws under the tab bar (a plain 1px line).
// The tab bar starts directly under it, with no gap: a focused bar's grey
// background begins at its own top edge, and any space left here showed as a
// white stripe between the line and the grey.
constexpr int SECTION_RULE_LINE_WIDTH = 1;

// Gap between the glance strip's own event lines, and between the strip and
// the bubble below it. Tighter than LABEL_GAP (used below the sprite, where
// the embedded Tasks section needs a real section break) -- these lines are
// dense glance info, not a section boundary of their own.
constexpr int GLANCE_LINE_GAP = 4;
constexpr int GLANCE_STRIP_GAP = 10;
// The gap between a glance line's own leading bullet/icon and its text.
constexpr int GLANCE_ICON_GAP = 8;

// Right1 means "go Home" on every other screen, but the instant you land
// here (this screen IS Home) it means "open Tasks" instead, since onEnter()
// always lands focus on the tab bar with Tasks active. A reflexive second
// press of the same physical button right after arriving -- e.g. pressing it
// again because the e-ink refresh did not yet visibly confirm the first
// press landed -- would otherwise drill straight into Tasks unasked. Any
// Right1 release within this window of onEnter() is ignored rather than
// acted on; long enough to absorb a reflexive double-press, short enough
// that a deliberate press still feels immediate.
constexpr unsigned long RIGHT1_ENTRY_GRACE_MS = 500;

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

// The HABITIFY_HABITS cache index for `habitId`, or the list size when it's
// gone -- re-resolved at every step of a habit action for the same reason the
// task actions do it: a popup sits on top for as long as the user takes to
// answer, and a bare cache index could go stale in the meantime.
size_t habitIndexForId(const std::string& habitId) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  for (size_t i = 0; i < habits.size(); i++) {
    if (habits[i].id == habitId) return i;
  }
  return habits.size();
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

// Same technique, for a row that can be selected (inverted) -- the embedded
// Tasks section's own rows, unlike the glance strip's date/time above, which
// never has a selection to worry about. No-op when !ink: punching white
// pixels out of already-white-on-black text would just make holes in it, not
// dim it (same guard OrganizerScreenActivity::dimText() uses).
void dimText(const GfxRenderer& renderer, const int x, const int y, const int fontId, const char* text,
             const bool ink) {
  if (!ink) return;
  dimText(renderer, x, y, fontId, text);
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

// Row separator between the embedded Tasks section's own rows.
constexpr int SEPARATOR_HEIGHT = 2;

// Same font selection as OrganizerScreenActivity's own titleFontId()/
// subtitleFontId() (Tasks/Calendar/Budget/Habits) -- not inherited (this
// class doesn't derive from that base), but kept in lockstep so the embedded
// section reads exactly like the real Tasks screen it's a scaled-down
// rendering of.
int taskRowTitleFontId() {
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? UI_10_FONT_ID : UI_12_FONT_ID;
}

int taskRowSubtitleFontId() {
  return SETTINGS.organizerFontSize == CrossPointSettings::ORGANIZER_FONT_SMALL ? SMALL_FONT_ID : UI_10_FONT_ID;
}
}  // namespace

void QuickPickActivity::onEnter() {
  Activity::onEnter();
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
  idleVariant = static_cast<uint8_t>(esp_random() % companion::IDLE_BUBBLE_VARIANT_COUNT);
  taskSelectedRow = 0;
  habitSelectedRow = 0;
  // Always Tasks on entry, and Habits at its own first tab (All) if it's
  // switched to later.
  section = Section::Tasks;
  activeAreaId.clear();
  // rebuildEmbeddedTabs() rebuilds visibleTabs from scratch regardless of
  // its prior content -- only activeKind (the "wanted" kind to try to keep,
  // see TaskTabModel::rebuildVisibleTabs()) needs setting here, to always
  // land on the first tab fresh on entry. OVERDUE is first in tab order, so
  // wanting it resolves to index 0 whether or not that tab currently exists.
  activeKind = taskTabModel::TaskTabKind::OVERDUE;
  rebuildEmbeddedTabs();
  tabBarFocused = true;
  // Lands focused on the tab bar (the first tab already active), not the
  // companion figure or a row -- the tab bar is always reachable regardless
  // of whether the active tab has any rows (see tabBarFocused's own
  // comment), so this is always a real, visible highlight to land on.
  focus = Focus::Embedded;
  enteredAtMs = millis();
  requestUpdate(true);
}

void QuickPickActivity::rebuildEmbeddedTabs() {
  const int restored = taskTabModel::rebuildVisibleTabs(activeKind, visibleTabs);
  activeKind = visibleTabs[static_cast<size_t>(restored)];

  // The new tab's list can be shorter than the old one (or empty), so the
  // row cursor has to be pulled back inside it -- same reasoning
  // TasksActivity::rebuildTabs() clamps selectedIndex for.
  const int rows = taskTabModel::countFor(activeKind);
  if (taskSelectedRow >= rows) taskSelectedRow = std::max(0, rows - 1);
  if (taskSelectedRow < 0) taskSelectedRow = 0;

  // The Habits section, the same way (HabitsActivity::rebuildTabs() clamps
  // its own selection for the same reason).
  const int restoredArea = habitTabModel::rebuildVisibleAreas(activeAreaId, visibleAreaIds);
  activeAreaId = visibleAreaIds[static_cast<size_t>(restoredArea)];
  const int habitRows = habitTabModel::rowCountFor(activeAreaId);
  if (habitSelectedRow >= habitRows) habitSelectedRow = std::max(0, habitRows - 1);
  if (habitSelectedRow < 0) habitSelectedRow = 0;
}

void QuickPickActivity::afterRowAction() {
  // A completion/reschedule can change which tabs exist (a task moving out
  // of Overdue, say, or the last Overdue task disappearing entirely) --
  // same reasoning TasksActivity's own performTaskCompletion() rebuilds for.
  rebuildEmbeddedTabs();
  requestUpdate(true);
}

void QuickPickActivity::showTaskRowOptions(const size_t cacheIndex) {
  const auto& tasks = TODOIST_TASKS.getTasks();
  if (cacheIndex >= tasks.size()) return;
  // Captured by id, not carried as cacheIndex: the popup below sits on top
  // for as long as the user takes to answer, and every step after this one
  // re-resolves the id to a (possibly different) cache index rather than
  // trusting a cacheIndex that could have gone stale in the meantime.
  const std::string taskId = tasks[cacheIndex].id;
  // Todoist has no way to reschedule a single occurrence of a recurring task
  // without replacing its recurrence entirely -- simplest is to just not
  // offer Reschedule for one at all.
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

int QuickPickActivity::embeddedRowCount() const {
  return section == Section::Tasks ? taskTabModel::countFor(activeKind) : habitTabModel::rowCountFor(activeAreaId);
}

bool QuickPickActivity::embeddedOnLogs() const {
  return section == Section::Tasks ? activeKind == taskTabModel::TaskTabKind::LOGS
                                   : habitTabModel::isLogsAreaId(activeAreaId);
}

void QuickPickActivity::switchSection(const Section next) {
  if (next == section) return;
  section = next;
  // Lands on the new section's tab bar rather than wherever the other one's
  // cursor happened to be -- always a valid, visible place to be (see
  // tabBarFocused's own comment).
  tabBarFocused = true;
  requestUpdate(true);
}

void QuickPickActivity::switchTab(const int delta) {
  if (section == Section::Habits) {
    const int areaCount = static_cast<int>(visibleAreaIds.size());
    if (areaCount <= 1) return;
    int index = 0;
    for (int i = 0; i < areaCount; i++) {
      if (visibleAreaIds[static_cast<size_t>(i)] == activeAreaId) {
        index = i;
        break;
      }
    }
    index = ((index + delta) % areaCount + areaCount) % areaCount;
    activeAreaId = visibleAreaIds[static_cast<size_t>(index)];
    habitSelectedRow = 0;
    if (habitTabModel::rowCountFor(activeAreaId) == 0) tabBarFocused = true;
    requestUpdate();
    return;
  }

  const int tabCount = static_cast<int>(visibleTabs.size());
  if (tabCount <= 1) return;

  int index = 0;
  for (int i = 0; i < tabCount; i++) {
    if (visibleTabs[static_cast<size_t>(i)] == activeKind) {
      index = i;
      break;
    }
  }
  // Wraps both ways, same as every other tabbed screen in this app.
  index = ((index + delta) % tabCount + tabCount) % tabCount;
  activeKind = visibleTabs[static_cast<size_t>(index)];
  taskSelectedRow = 0;
  // A row cursor can't sit on a tab with no rows; fall back to the tab bar,
  // which is always a valid place to be (see tabBarFocused's own comment).
  if (taskTabModel::countFor(activeKind) == 0) tabBarFocused = true;
  requestUpdate();
}

void QuickPickActivity::showHabitRowOptions(const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return;
  const std::string habitId = habits[cacheIndex].id;
  // A habit with no goal has no unit, so nothing can be logged against it --
  // Log is left off the menu rather than shown and silently failing. Complete
  // needs no unit, so it is offered either way.
  const bool canLog = !habits[cacheIndex].unitSymbol.empty();

  std::vector<std::string> options;
  options.reserve(3);
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
  const size_t index = habitIndexForId(habitId);
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (index >= habits.size() || habits[index].unitSymbol.empty()) return;
  const HabitifyHabit& habit = habits[index];

  // A number entry rather than a single +1, defaulting to 1 so one Confirm
  // press behaves like a plain "+1" -- same as HabitsActivity's own log entry.
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(renderer, mappedInput, "HabitifyLogAmount", StrId::STR_NONE_OPT, 1, 1,
                                                  habitTabModel::MAX_LOG_AMOUNT, habitTabModel::LOG_SMALL_STEP,
                                                  habitTabModel::LOG_LARGE_STEP, StrId::STR_NONE_OPT,
                                                  /*readerActivity=*/false, /*ignoreInitialConfirmRelease=*/true,
                                                  StrId::STR_NONE_OPT, habit.name, habit.unitSymbol),
      [this, habitId](const ActivityResult& result) {
        if (mappedInput.isPressed(MappedInputManager::Button::Right2)) {
          swallowConfirmRelease = true;
        }
        if (result.isCancelled || mappedInput.isPressed(MappedInputManager::Button::Right1)) {
          swallowBackRelease = true;
        }
        if (result.isCancelled) return;
        const auto amount = std::get<IntervalResult>(result.data).value;
        const size_t idx = habitIndexForId(habitId);
        if (idx < HABITIFY_HABITS.getHabits().size() && amount > 0) {
          RenderLock lock(*this);
          organizerActions::logHabit(idx, static_cast<float>(amount));
        }
        afterRowAction();
      });
}

void QuickPickActivity::completeHabitRow(const std::string& habitId) {
  const size_t idx = habitIndexForId(habitId);
  if (idx < HABITIFY_HABITS.getHabits().size()) {
    RenderLock lock(*this);
    organizerActions::completeHabit(idx);
  }
  afterRowAction();
}

void QuickPickActivity::offerFocusSessionForHabit(const std::string& habitId) {
  const size_t index = habitIndexForId(habitId);
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (index >= habits.size()) return;  // gone already
  const std::string capturedText = habits[index].name;

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

void QuickPickActivity::clearSelectedHabitLogRow() {
  if (!habitTabModel::isLogsAreaId(activeAreaId)) return;
  const int cacheIndex = habitTabModel::logEntryIndexForRow(habitSelectedRow);
  if (cacheIndex < 0) return;
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (static_cast<size_t>(cacheIndex) >= habits.size() || !habits[static_cast<size_t>(cacheIndex)].hasPending()) {
    return;  // Synced -- nothing local left to undo.
  }
  const std::string habitId = habits[static_cast<size_t>(cacheIndex)].id;

  {
    RenderLock lock(*this);
    HABITIFY_HABITS.undoLocalCompletion(habitId);
    HABITIFY_HABITS.saveToFile();
    // The companion's mood ladder needs to catch up immediately rather than
    // waiting for the next sync or Home visit -- same as HabitsActivity.
    COMPANION.recordActivity();
  }
  afterRowAction();
}

void QuickPickActivity::clearSelectedLogTaskRow() {
  if (activeKind != taskTabModel::TaskTabKind::LOGS) return;
  const int entryIndex = taskTabModel::logEntryIndexForRow(taskSelectedRow);
  if (entryIndex < 0) return;
  const auto& entries = TODOIST_TASKS.getCompletedTodayEntries();
  if (static_cast<size_t>(entryIndex) >= entries.size() || !entries[static_cast<size_t>(entryIndex)].pending) {
    return;  // Synced -- nothing local left to undo.
  }

  {
    RenderLock lock(*this);
    TODOIST_TASKS.cancelCompletedLogEntry(static_cast<size_t>(entryIndex));
    TODOIST_TASKS.saveToFile();
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
      // Fresh remark for the mood that's now showing -- see idleVariant's
      // own comment.
      idleVariant = static_cast<uint8_t>(esp_random() % companion::IDLE_BUBBLE_VARIANT_COUNT);
      requestUpdate();
    }
  }

  // A press seen here is a fresh one, so nothing is owed any more.
  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) swallowBackRelease = false;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  // Side Up/Down (labelled Tasks/Habits, see render()): which of the two
  // sections shows below the companion, from whichever focus stop the cursor
  // is on, instead of the app-jump shortcut every other screen's side
  // buttons have (see this file's own header comment). A fresh press each,
  // same guard reasoning as Right1/Right2 above.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) upPressSeen = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) downPressSeen = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upPressSeen) switchSection(Section::Tasks);
    upPressSeen = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downPressSeen) switchSection(Section::Habits);
    downPressSeen = false;
    return;
  }

  // Right1 (the "Apps" button) is a per-focus shortcut to a related screen
  // now (see this file's own header comment) -- never "leave", since this
  // screen is Home and there is nowhere left to leave to.
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    if (swallowBackRelease) {
      // The tail of the press that cancelled a popup pushed from this screen.
      // Acting on it would fire the shortcut below instead of just closing
      // the popup that press already closed.
      swallowBackRelease = false;
      return;
    }
    if (millis() - enteredAtMs < RIGHT1_ENTRY_GRACE_MS) {
      // See RIGHT1_ENTRY_GRACE_MS's own comment -- a reflexive press this
      // soon after landing on Home is not a deliberate shortcut request.
      return;
    }
    switch (focus) {
      case Focus::Header:
        activityManager.goToSettings();
        break;
      case Focus::Glance:
        activityManager.goToCalendar();
        break;
      case Focus::Companion:
#ifdef ENABLE_BLE_NOTIFY_SPIKE
        activityManager.goToBleNotifications();
#endif
        break;
      case Focus::Embedded:
        if (!tabBarFocused) {
          // Back: from a row, up to the tab bar of the section it belongs to
          // (labelled Back, see render()) -- the same two-level Back the real
          // Tasks/Habits/Calendar/Budget/Settings screens have.
          tabBarFocused = true;
          requestUpdate();
          break;
        }
        // On the tab bar: Random (see this file's own header comment) -- drops
        // onto a random row of the active tab, without leaving. Nothing to
        // randomize on a log view -- Clear is the only action there.
        if (!embeddedOnLogs()) {
          const int count = embeddedRowCount();
          if (count > 0) {
            embeddedRow() = static_cast<int>(esp_random() % static_cast<uint32_t>(count));
            tabBarFocused = false;
            requestUpdate();
          }
        }
        break;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left1)) {
    // Reverse of Left2 below -- see this file's own header comment for the
    // full Tasks/Header/Glance/Companion loop order.
    switch (focus) {
      case Focus::Companion:
        // Straight to the header when there's nothing in the glance strip
        // to land on -- see todaysEvents()'s own comment; an empty strip
        // means synced with genuinely nothing today, not a fallback line.
        focus = todaysEvents().empty() ? Focus::Header : Focus::Glance;
        break;
      case Focus::Glance:
        focus = Focus::Header;
        break;
      case Focus::Header: {
        // Off the top -- wrap to the last row of the active tab, or its tab
        // bar when that tab is empty (four stops, always all reachable
        // regardless of whether the active tab has any rows in it).
        focus = Focus::Embedded;
        const int count = embeddedRowCount();
        tabBarFocused = count == 0;
        embeddedRow() = count == 0 ? 0 : count - 1;
        break;
      }
      case Focus::Embedded:
        if (tabBarFocused) {
          // Off the top of the tab bar -- move onto the companion figure,
          // continuing the loop order.
          focus = Focus::Companion;
        } else if (embeddedRow() == 0) {
          // Off the top of the row list -- move onto the tab bar, "the
          // start of the embedded section" (see this file's own header
          // comment), not straight to the companion figure.
          tabBarFocused = true;
        } else {
          const int count = embeddedRowCount();
          embeddedRow() = (embeddedRow() + count - 1) % count;
        }
        break;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left2)) {
    // Forward direction of the loop: last Tasks-section row -> header ->
    // glance -> companion -> wraps to the Tasks section's own tab bar (see
    // this file's own header comment).
    switch (focus) {
      case Focus::Embedded: {
        const int count = embeddedRowCount();
        if (tabBarFocused) {
          if (count == 0) {
            // No rows to move onto -- the tab bar doubles as "the last row"
            // when the active tab is empty, so this continues straight to
            // the header, same as the row branch below does.
            focus = Focus::Header;
          } else {
            tabBarFocused = false;
            embeddedRow() = 0;
          }
        } else if (embeddedRow() == count - 1) {
          // Off the bottom of the list -- move onto the header instead of
          // wrapping straight to the companion figure.
          focus = Focus::Header;
        } else {
          embeddedRow() = (embeddedRow() + 1) % count;
        }
        break;
      }
      case Focus::Header:
        // Straight past the glance strip when there's nothing on it to land
        // on -- same reasoning as Left1's own Companion case above.
        focus = todaysEvents().empty() ? Focus::Companion : Focus::Glance;
        break;
      case Focus::Glance:
        focus = Focus::Companion;
        break;
      case Focus::Companion:
        // Onto the tab bar -- "the start of the Tasks section" (see this
        // file's own header comment).
        focus = Focus::Embedded;
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
#ifdef ENABLE_BLE_NOTIFY_SPIKE
        // Dismiss: steps the bubble to the next-newest undismissed alert, if
        // there's one showing at all (see this file's own header comment).
        if (ALERT_BUBBLE.current() != nullptr) {
          ALERT_BUBBLE.dismiss();
          requestUpdate();
        }
#endif
        break;
      case Focus::Header:
        // No per-row or per-suggestion action to offer for the header itself
        // (see this file's own header comment) -- Sync All instead. Returns
        // via goHome() rather than goToCompanion() (this screen is Home now
        // either way, and goHome() is the one mechanism every other screen's
        // own Sync All return uses too).
        activityManager.goToSyncAll([] { activityManager.goHome(); });
        break;
      case Focus::Glance:
        // Nothing to select here -- Right1 (Calendar) is this stop's only
        // action.
        break;
      case Focus::Embedded:
        if (tabBarFocused) {
          // Cycles the currently-visible tabs without leaving the tab bar --
          // same convention every other tabbed screen in this app uses (see
          // this file's own header comment).
          switchTab(1);
          break;
        }
        if (section == Section::Habits) {
          if (habitTabModel::isLogsAreaId(activeAreaId)) {
            clearSelectedHabitLogRow();
          } else {
            const int cacheIndex = habitTabModel::cacheIndexForRow(activeAreaId, habitSelectedRow);
            if (cacheIndex >= 0) showHabitRowOptions(static_cast<size_t>(cacheIndex));
          }
        } else if (activeKind == taskTabModel::TaskTabKind::LOGS) {
          clearSelectedLogTaskRow();
        } else {
          const int cacheIndex = taskTabModel::taskCacheIndexForRow(activeKind, taskSelectedRow);
          if (cacheIndex >= 0) showTaskRowOptions(static_cast<size_t>(cacheIndex));
        }
        break;
    }
  }
}

// -- render ---------------------------------------------------------------

void QuickPickActivity::renderTasksTab(const int top, const int height) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  const bool rowsFocused = focus == Focus::Embedded && !tabBarFocused;

  const int totalRows = taskTabModel::countFor(activeKind);
  if (totalRows == 0) {
    const char* empty = activeKind == taskTabModel::TaskTabKind::LOGS ? tr(STR_LOG_EMPTY) : tr(STR_TODOIST_NO_TASKS);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2, empty);
    return;
  }

  // Title + a dimmed second line -- the due date for the five real kinds
  // (skipped on Today/No date, see TaskTabModel::rowsHaveSubtitle()'s own
  // comment), or the Cached/Synced tag for Logs -- the same two-line style
  // the real Tasks screen itself uses, following SETTINGS.organizerFontSize.
  const int titleFont = taskRowTitleFontId();
  const int subtitleFont = taskRowSubtitleFontId();
  const bool hasSubtitle = taskTabModel::rowsHaveSubtitle(activeKind);
  const int rowPad = std::max(6, renderer.getLineHeight(titleFont) * 2 / 5);
  const int rowHeight = hasSubtitle ? renderer.getLineHeight(titleFont) + renderer.getLineHeight(subtitleFont) + rowPad
                                    : renderer.getLineHeight(titleFont) + rowPad;
  const int pageItems = std::max(1, height / rowHeight);
  const int pageStart = (taskSelectedRow / pageItems) * pageItems;

  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= totalRows) break;
    const int rowY = top + row * rowHeight;
    const bool selected = rowsFocused && i == taskSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, rowHeight);

    const int textY = rowY + rowPad / 2;
    if (activeKind == taskTabModel::TaskTabKind::LOGS) {
      const int entryIndex = taskTabModel::logEntryIndexForRow(i);
      if (entryIndex < 0) continue;
      const auto& entry = TODOIST_TASKS.getCompletedTodayEntries()[static_cast<size_t>(entryIndex)];
      const auto shown = renderer.truncatedText(titleFont, entry.title.c_str(), textWidth);
      renderer.drawText(titleFont, textX, textY, shown.c_str(), ink);
      const int subY = textY + renderer.getLineHeight(titleFont);
      const char* tag = entry.pending ? tr(STR_LOG_CACHED) : tr(STR_LOG_SYNCED);
      renderer.drawText(subtitleFont, textX, subY, tag, ink);
      dimText(renderer, textX, subY, subtitleFont, tag, ink);
    } else {
      const int cacheIndex = taskTabModel::taskCacheIndexForRow(activeKind, i);
      if (cacheIndex < 0) continue;
      const auto& task = TODOIST_TASKS.getTasks()[static_cast<size_t>(cacheIndex)];
      const auto shown = renderer.truncatedText(titleFont, task.content.c_str(), textWidth);
      renderer.drawText(titleFont, textX, textY, shown.c_str(), ink);
      // Undated tasks draw nothing rather than "--": the row keeps its
      // height, so the list stays even, and an empty line says "no date"
      // more quietly than a dash -- same reasoning TasksActivity's own
      // drawRow() uses.
      if (hasSubtitle && task.dueDays != todoist::DUE_NONE) {
        char when[16];
        organizer::formatDayLabel(task.dueDays, when, sizeof(when));
        const auto shownWhen = renderer.truncatedText(subtitleFont, when, textWidth);
        const int whenY = textY + renderer.getLineHeight(titleFont);
        renderer.drawText(subtitleFont, textX, whenY, shownWhen.c_str(), ink);
        dimText(renderer, textX, whenY, subtitleFont, shownWhen.c_str(), ink);
      }
    }

    const bool lastOnPage = row + 1 >= pageItems || i + 1 >= totalRows;
    if (!selected && !lastOnPage) {
      renderer.fillRectDither(textX, rowY + rowHeight - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                              Color::LightGray);
    }
  }
}

void QuickPickActivity::renderHabitsTab(const int top, const int height) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int textX = metrics.contentSidePadding;
  const int textWidth = pageWidth - metrics.contentSidePadding * 2;
  const bool rowsFocused = focus == Focus::Embedded && !tabBarFocused;
  const bool onLogs = habitTabModel::isLogsAreaId(activeAreaId);

  const int totalRows = habitTabModel::rowCountFor(activeAreaId);
  if (totalRows == 0) {
    // Same distinctions HabitsActivity::emptyMessage() draws: everything
    // hidden by "hide completed" is a different state from having no habits,
    // and the one the user can act on.
    const char* empty;
    if (onLogs) {
      empty = tr(STR_LOG_EMPTY);
    } else if (!HABITIFY_HABITS.hasSynced()) {
      empty = tr(STR_HABITIFY_NEVER_SYNCED);
    } else if (HABITIFY_STORE.getHideCompleted() && !HABITIFY_HABITS.getHabits().empty()) {
      empty = tr(STR_HABITIFY_ALL_DONE);
    } else {
      empty = tr(STR_HABITIFY_NO_HABITS);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2, empty);
    return;
  }

  // One line per habit -- the name with its progress hard right -- except on
  // Logs, which carries a second Cached/Synced line, same as the real screen.
  const int titleFont = taskRowTitleFontId();
  const int subtitleFont = taskRowSubtitleFontId();
  const int rowPad = std::max(6, renderer.getLineHeight(titleFont) * 2 / 5);
  const int rowHeight = onLogs ? renderer.getLineHeight(titleFont) + renderer.getLineHeight(subtitleFont) + rowPad
                               : renderer.getLineHeight(titleFont) + rowPad;
  const int pageItems = std::max(1, height / rowHeight);
  const int pageStart = (habitSelectedRow / pageItems) * pageItems;

  for (int row = 0; row < pageItems; row++) {
    const int i = pageStart + row;
    if (i >= totalRows) break;
    const int rowY = top + row * rowHeight;
    const bool selected = rowsFocused && i == habitSelectedRow;
    const bool ink = !selected;

    if (selected) renderer.fillRect(0, rowY, pageWidth, rowHeight);

    const int textY = rowY + rowPad / 2;
    const int cacheIndex = habitTabModel::cacheIndexForRow(activeAreaId, i);
    if (cacheIndex < 0) continue;
    const HabitifyHabit& habit = HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)];

    if (onLogs) {
      const auto shownName = renderer.truncatedText(titleFont, habit.name.c_str(), textWidth);
      renderer.drawText(titleFont, textX, textY, shownName.c_str(), ink);
      const int subY = textY + renderer.getLineHeight(titleFont);
      const char* tag = habit.hasPending() ? tr(STR_LOG_CACHED) : tr(STR_LOG_SYNCED);
      renderer.drawText(subtitleFont, textX, subY, tag, ink);
      dimText(renderer, textX, subY, subtitleFont, tag, ink);
    } else {
      char progress[24];
      habitTabModel::formatProgress(habit, progress, sizeof(progress));

      // The figure hard right, drawn whole because it is the part being
      // glanced at, and the name takes what is left -- two spaces of the row's
      // own font between them, same as HabitsActivity::drawRow().
      const int gap = renderer.getSpaceWidth(titleFont) * 2;
      const int progressWidth = renderer.getTextWidth(titleFont, progress, EpdFontFamily::REGULAR);
      const int nameWidth = std::max(0, textWidth - progressWidth - gap);
      const auto shownName = renderer.truncatedText(titleFont, habit.name.c_str(), nameWidth, EpdFontFamily::REGULAR);
      renderer.drawText(titleFont, textX, textY, shownName.c_str(), ink, EpdFontFamily::REGULAR);
      renderer.drawText(titleFont, textX + textWidth - progressWidth, textY, progress, ink, EpdFontFamily::REGULAR);

      // A met goal is greyed, so a finished habit reads at a glance.
      if (habit.isComplete()) {
        dimText(renderer, textX, textY, titleFont, shownName.c_str(), ink);
        dimText(renderer, textX + textWidth - progressWidth, textY, titleFont, progress, ink);
      }
    }

    const bool lastOnPage = row + 1 >= pageItems || i + 1 >= totalRows;
    if (!selected && !lastOnPage) {
      renderer.fillRectDither(textX, rowY + rowHeight - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
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
  // Empty (not a fallback placeholder row) when synced and there is
  // genuinely nothing today -- the glance strip itself collapses to nothing
  // in that case (see render()'s own glanceTextHeight/glanceStripHeight),
  // rather than spending space on a "nothing today" line.
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
  // Zero for both when there is nothing today -- genuinely no space spent on
  // an empty strip, not even the breathing-room gap, rather than glanceRows'
  // old guaranteed-non-empty invariant (a fallback placeholder row) this
  // depended on. glanceRows.size() is unsigned, so the empty case is handled
  // explicitly rather than letting size()-1 underflow.
  const int glanceTextHeight = glanceRows.empty() ? 0
                                                  : static_cast<int>(glanceRows.size()) * glanceLineH +
                                                        static_cast<int>(glanceRows.size() - 1) * GLANCE_LINE_GAP;
  const int glanceStripHeight = glanceRows.empty() ? 0 : glanceTextHeight + GLANCE_STRIP_GAP;
  // The focus invert (below) spans the full glanceStripHeight -- so the text
  // itself is nudged down by half of GLANCE_STRIP_GAP here, splitting that
  // gap evenly above and below the glyphs instead of leaving it all beneath
  // them.
  const int glanceVerticalOffset = (glanceStripHeight - glanceTextHeight) / 2;
  {
    constexpr int BULLET_SIZE = 6;
    const int bulletX = metrics.contentSidePadding;
    const int textX = bulletX + BULLET_SIZE + GLANCE_ICON_GAP;
    const int glanceMaxWidth = pageWidth - metrics.contentSidePadding * 2 - BULLET_SIZE - GLANCE_ICON_GAP;
    int rowY = glanceStripTop + glanceVerticalOffset;
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
  // Hovering the glance strip (see this file's own header comment): a true
  // pixel invert over the whole strip, the same technique the header and
  // the bubble already use when focused -- see glanceVerticalOffset's own
  // comment for how the text stays centred within this box.
  if (focus == Focus::Glance) {
    renderer.invertRect(0, glanceStripTop, pageWidth, glanceStripHeight);
  }

  // SELECTION_BOX_PADDING here, not metrics.verticalSpacing: a tighter gap
  // than the old, larger vertical spacing this replaced.
  const int contentTop = glanceStripTop + glanceStripHeight + SELECTION_BOX_PADDING;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int totalContentHeight = contentBottom - contentTop;
  const int contentWidth = pageWidth - MARGIN * 2;
  const int maxTextWidth = contentWidth - PAD * 2;

  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();

  // A sleeping companion doesn't also show an alert, so that line still wins
  // outright; otherwise the bubble shows the newest BLE alert not yet
  // dismissed from here (see CompanionAlertBubble's own comment), or a
  // mood-flavored idle line once there's none left. Never a task/habit
  // suggestion -- see this file's own header comment.
  std::string text;
  if (mood == companion::Mood::Sleeping) {
    text = tr(STR_COMPANION_SLEEPING_BUBBLE);
  } else {
    bool showedAlert = false;
#ifdef ENABLE_BLE_NOTIFY_SPIKE
    if (const auto* alert = ALERT_BUBBLE.current()) {
      char line[192];
      if (alert->content[0] != '\0') {
        snprintf(line, sizeof(line), "%s: %s", alert->title, alert->content);
      } else {
        snprintf(line, sizeof(line), "%s", alert->title);
      }
      text = line;
      showedAlert = true;
    }
#endif
    if (!showedAlert) text = companion::idleBubbleText(mood, idleVariant);
  }
  const auto textFit = companion::fitBubbleText(renderer, UI_10_FONT_ID, text, maxTextWidth, MIN_BUBBLE_TEXT_WIDTH, 4);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int bubbleH = static_cast<int>(textFit.lines.size()) * lineH + PAD * 2;
  const int bubbleBlock = bubbleH + TAIL_LENGTH + BUBBLE_GAP;
  const int bubbleWidth = textFit.textWidth + PAD * 2;

  // The companion figure (bubble + sprite) gets a fixed share of the space
  // below the header; the embedded Tasks section gets what is left over.
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

  // Hovering the companion figure (see this file's own header comment): the
  // bubble itself is drawn filled -- black background, white text -- rather
  // than a selection-box outline around the figure, since the bubble IS the
  // thing Right2 (Dismiss) acts on here. Filled from within drawSpeechBubble
  // itself (see its own `filled` comment) so the black exactly follows the
  // bubble's rounded body and tail, not a plain inverted bounding rect that
  // would also blacken the paper-colored corners just outside the curve.
  const bool bubbleFocused = focus == Focus::Companion;
  companion::drawSpeechBubble(renderer, bubbleX, contentTop, bubbleWidth, bubbleH, TAIL_LENGTH,
                              companion::TailSide::Bottom, /*lineWidth=*/2, bubbleFocused);
  const Rect textBounds{bubbleX + PAD, contentTop, textFit.textWidth, bubbleH};
  int textY = contentTop + PAD;
  for (const auto& line : textFit.lines) {
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, textY, line.c_str(), /*black=*/!bubbleFocused);
    textY += lineH;
  }

  companion::drawPose(renderer, id, mood, centreX - spriteW / 2, spriteTop, scale);

  // Straight from the sprite to the embedded Tasks section below -- no mood
  // label in between any more (removed entirely, freeing this space for the
  // section to sit higher).
  const int sectionTop = spriteTop + spriteH + LABEL_GAP;

  // The section below is a scaled-down rendering of the real Tasks or Habits
  // screen (see this file's own header comment) -- but without that screen's
  // own title, or the companion's Highscore that used to sit on the same row:
  // just a single rule marking where it starts, the same weight as the line
  // LyraTheme::drawTabBar draws under the tab bar. Drawn directly rather than
  // through GUI.drawHeader(), which only ever draws its rule alongside a
  // title. Then the tab bar itself (visibleTabs/visibleAreaIds mirror the real
  // screens' own tab bars exactly, via the shared TaskTabModel/HabitTabModel).
  const bool habitsShowing = section == Section::Habits;
  renderer.drawLine(0, sectionTop, pageWidth - 1, sectionTop, SECTION_RULE_LINE_WIDTH, true);
  const int tabBarTop = sectionTop + SECTION_RULE_LINE_WIDTH;

  // tabBarFocused (not activeKind) decides whether the bar is drawn
  // "focused" (matches OrganizerScreenActivity's own "index 0 is the tab
  // bar" convention, the same one loop() mirrors for Left1/Left2/Right2).
  std::vector<TabInfo> tabs;
  if (habitsShowing) {
    tabs.reserve(visibleAreaIds.size());
    for (const auto& areaId : visibleAreaIds) {
      tabs.push_back(TabInfo{habitTabModel::tabLabel(areaId), areaId == activeAreaId});
    }
  } else {
    tabs.reserve(visibleTabs.size());
    for (const auto kind : visibleTabs) {
      tabs.push_back(TabInfo{taskTabModel::tabLabel(kind), kind == activeKind});
    }
  }
  GUI.drawTabBar(renderer, Rect{0, tabBarTop, pageWidth, metrics.tabBarHeight}, tabs,
                 focus == Focus::Embedded && tabBarFocused);

  const int listTop = tabBarTop + metrics.tabBarHeight;
  const int listHeight = std::max(0, contentBottom - listTop);
  if (habitsShowing) {
    renderHabitsTab(listTop, listHeight);
  } else {
    renderTasksTab(listTop, listHeight);
  }

  // Right1 is a per-focus shortcut now (see this file's own header comment),
  // not a fixed "leave" action -- blank wherever there's nothing to open.
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* leftLabel = tr(STR_DIR_UP);
  const char* rightLabel = tr(STR_DIR_DOWN);
  switch (focus) {
    case Focus::Header:
      backLabel = homeAppOrder::displayName(homeAppOrder::AppId::Settings);
      // Right2 is Sync All instead of Clear/Select -- there's no per-row or
      // per-suggestion action to offer for the header itself.
      confirmLabel = tr(STR_SYNC_ALL);
      break;
    case Focus::Glance:
      backLabel = homeAppOrder::displayName(homeAppOrder::AppId::Calendar);
      break;
    case Focus::Companion:
#ifdef ENABLE_BLE_NOTIFY_SPIKE
      backLabel = homeAppOrder::displayName(homeAppOrder::AppId::Notifications);
      // Dismiss only when there's actually an alert showing.
      if (ALERT_BUBBLE.current() != nullptr) confirmLabel = tr(STR_DISMISS);
#endif
      break;
    case Focus::Embedded:
      if (tabBarFocused) {
        // Random (see loop()'s own Right1 handler) -- from the tab bar one row
        // is already enough to roll onto, and there is nothing to roll on a
        // Logs tab.
        if (!embeddedOnLogs() && embeddedRowCount() > 0) backLabel = tr(STR_RANDOM);
        // Same touch OrganizerScreenActivity's own tab bar gives Right2: its
        // label previews the tab a press would switch to.
        if (section == Section::Habits) {
          if (visibleAreaIds.size() > 1) {
            size_t index = 0;
            for (size_t i = 0; i < visibleAreaIds.size(); i++) {
              if (visibleAreaIds[i] == activeAreaId) {
                index = i;
                break;
              }
            }
            confirmLabel = habitTabModel::tabLabel(visibleAreaIds[(index + 1) % visibleAreaIds.size()]);
          }
        } else if (visibleTabs.size() > 1) {
          int index = 0;
          for (size_t i = 0; i < visibleTabs.size(); i++) {
            if (visibleTabs[i] == activeKind) {
              index = static_cast<int>(i);
              break;
            }
          }
          confirmLabel = taskTabModel::tabLabel(
              visibleTabs[static_cast<size_t>((index + 1) % static_cast<int>(visibleTabs.size()))]);
        }
      } else {
        // A row is focused: Right1 goes Back up to the tab bar (see loop()'s
        // own Right1 handler); Right2 is whatever the row offers.
        backLabel = tr(STR_BACK);
        if (section == Section::Habits) {
          const int cacheIndex = habitTabModel::cacheIndexForRow(activeAreaId, habitSelectedRow);
          if (cacheIndex < 0) {
            confirmLabel = "";
          } else if (habitTabModel::isLogsAreaId(activeAreaId)) {
            const bool onCachedRow = HABITIFY_HABITS.getHabits()[static_cast<size_t>(cacheIndex)].hasPending();
            confirmLabel = onCachedRow ? tr(STR_CLEAR_BUTTON) : "";
          } else {
            confirmLabel = tr(STR_SELECT);
          }
        } else if (activeKind == taskTabModel::TaskTabKind::LOGS) {
          const int entryIndex = taskTabModel::logEntryIndexForRow(taskSelectedRow);
          const auto& entries = TODOIST_TASKS.getCompletedTodayEntries();
          const bool onCachedRow = entryIndex >= 0 && static_cast<size_t>(entryIndex) < entries.size() &&
                                   entries[static_cast<size_t>(entryIndex)].pending;
          confirmLabel = onCachedRow ? tr(STR_CLEAR_BUTTON) : "";
        } else {
          confirmLabel = taskTabModel::taskCacheIndexForRow(activeKind, taskSelectedRow) >= 0 ? tr(STR_SELECT) : "";
        }
      }
      break;
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Side button labels (Up = "Tasks", Down = "Habits") -- which section
  // shows below the companion (see loop()'s own comment above and
  // switchSection()). The one showing is drawn inverted.
  GUI.drawSideButtonHints(renderer, tr(STR_COMPANION_SIDE_TASKS), tr(STR_COMPANION_SIDE_HABITS),
                          /*topSelected=*/section == Section::Tasks, /*bottomSelected=*/section == Section::Habits);

  renderer.displayBuffer();
}
