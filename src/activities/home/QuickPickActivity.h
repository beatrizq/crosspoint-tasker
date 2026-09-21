#pragma once

#include <CompanionMood.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * The companion's own screen: a big pose of its figure, mood and speech
 * bubble (a task/habit suggestion, or the sleeping/empty text -- the bubble
 * itself never shows a BLE notification; those stay on the dedicated Alerts
 * screen). A glance strip sits right under the header -- today's own Google
 * Calendar events, bulleted (see todaysEvents()'s own comment; there can be
 * more than one, so this is a plain bullet, not that one app's own icon) --
 * purely informational: it neither joins the Left1/Left2 loop below nor sits
 * inside either focus highlight described there. The YNAB "Wants" balance
 * and the newest BLE alert are temporarily out of this strip (see render()'s
 * own comment; glanceBudgetWants()/glanceNewestAlert() are still here for
 * when they come back). The header itself carries no title or subtitle
 * of this screen's own (drawHeader() is called with both nullptr; see
 * render()'s own comment for why) -- the companion's name and its
 * Age/Highscore, shown here on earlier iterations of this screen, are gone.
 * On Lyra/Lyra 3 Covers the header is where that theme's own
 * clock+date+battery combo lives (LyraTheme::drawHeader draws it
 * unconditionally into whatever rect it's given, regardless of
 * title/subtitle) -- this screen does not draw a copy of that itself;
 * hovering the header (see the Focus enum) inverts whatever the theme drew
 * there, clock/date/battery included.
 *
 * Below the companion figure sits a tab bar (see the Tab enum) -- Tasks,
 * Habits, Logs -- so acting on a Tasks/Habits row updates the mood right
 * where it's shown, without leaving to the real Tasks/Habits screens. Tasks
 * and Habits show the same items that count toward the mood (see
 * relevantTaskIndices()/relevantHabitIndices()'s own comment: due-today-or-
 * overdue tasks, not-yet-done habits) -- exactly what a Random reroll could
 * land on. Logs is today's completed tasks/habits, unchanged from this
 * screen's own original single-list design.
 *
 * Reached from Home (the companion's own grid tile) or reconstructed on boot
 * from CrossPointState when the device was showing this screen at the moment
 * it went to sleep -- either way, everything this screen needs comes through
 * the constructor, since it also mirrors its own content into CrossPointState
 * on entry and on every reroll (see onEnter()/reroll()) rather than main.cpp
 * fishing it out reactively.
 *
 * Button IDs used here: Left1/Left2 are the pair that moves focus; Right1/
 * Right2 are the pair printed "Apps"/"Select" on the case (Right1 = the
 * button that otherwise always leaves, Right2 = the button that otherwise
 * always opens options). Focus (see the Focus enum) cycles through three
 * stops in one continuous circular loop -- the Logs section, the companion
 * figure, then the header -- in that order (Left2 = forward: last Logs-
 * section row -> header -> companion -> wraps to the Logs section's own tab
 * bar; Left1 = the exact reverse):
 *   - Logs section: the tab bar itself (see tabBarFocused's own comment) is
 *     always the first stop -- "the start of the Logs section" -- reachable
 *     regardless of whether the active tab has any rows; Right2 there cycles
 *     to the next tab (same convention every other tabbed screen in this app
 *     uses: OrganizerScreenActivity's own "index 0 is the tab bar"). Past the
 *     tab bar, each tab's own rows: Logs' own row is Select-less -- Right2 on
 *     a row is Clear instead, and only when that row is Cached (see
 *     LogEntry::cached and clearLogRow()'s own comment): a completion still
 *     local/unpushed, actually reversible, as opposed to Synced (a sync
 *     already confirmed it, nothing local left to undo, Right2 does nothing
 *     there). Tasks/Habits rows are actionable -- Right2 opens the same
 *     options popup (Complete/Focus session/Reschedule for a task; Log/
 *     Complete/Focus session for a habit) a real row's own action does (see
 *     showTaskRowOptions()/showHabitRowOptions()), resolved against a cache
 *     index straight from relevantTaskIndices()/relevantHabitIndices()
 *     rather than a row list this screen owns a copy of. Right1 leaves in
 *     every state here (tab bar or row), same as every state except the
 *     companion figure.
 *   - Companion figure: a rounded selection-box outline spanning (almost)
 *     the full content width, bounding the companion section alone -- bubble
 *     and sprite only, never the tab bar/rows below it (this screen reads as
 *     three sections, and the highlight only ever claims the one that's
 *     focused). Same rounded "outline, not fill" style the pre-grid-tile
 *     Home screen once drew around its own companion column. Right2 stays
 *     Select (acts on the suggestion) and Right1 -- which otherwise always
 *     leaves -- becomes Random instead.
 *   - Header (no title/subtitle of this screen's own, just whatever
 *     clock/date/battery the active theme draws into it): shown inverted
 *     (a plain black fill, nothing this screen draws back on top of it)
 *     rather than the companion's outline style when focused -- drawn as an
 *     overlay on top of the theme's own GUI.drawHeader() output (see
 *     render()'s own comment) rather than asking the theme for an inverted
 *     variant, since drawHeader() has no such parameter. Right1 leaves, same
 *     as the Logs section; Right2 is Sync All (activityManager.
 *     goToSyncAll()) instead of Clear/Select, since there's no per-row or
 *     per-suggestion action to offer here.
 * Right1 leaving always reports the current suggestion as a QuickPickResult
 * so Home's own companion tile stays in sync -- setResult() has to be called
 * before finish(), not in onExit() -- ActivityManager::popActivity() reads
 * the result before it runs the outgoing activity's onExit().
 *
 * Side Up/Down jump to the previous/next app in the home grid's own order --
 * the same shortcut every app screen has (see
 * OrganizerScreenActivity/SettingsActivity's own identical block),
 * independent of Left1/Left2/Right1/Right2 above.
 */
class QuickPickActivity final : public Activity {
 public:
  // itemId is the Todoist task id / Habitify habit id behind pickedText, so
  // Go can act on that exact item. Empty when poolEmpty is true (nothing was
  // picked).
  QuickPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pickedText, std::string itemId,
                    const bool isHabit, const bool poolEmpty)
      : Activity("QuickPick", renderer, mappedInput),
        pickedText(std::move(pickedText)),
        itemId(std::move(itemId)),
        isHabit(isHabit),
        poolEmpty(poolEmpty) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isQuickPickActivity() const override { return true; }

 private:
  // Rerolls via quickpick::roll() -- the same pool/weights Home's own roll
  // used -- and re-mirrors the result into CrossPointState.
  void reroll();

  // One row of the Logs row list -- see logEntries()'s own comment.
  struct LogEntry {
    std::string text;
    bool isTask;  // true: taskEntryIndex is valid. false: habitId is valid.
    // Whether Clear (Right2, see loop()'s own comment) is offered for this
    // row: Cached (still local/unpushed -- safely, actually reversible) vs
    // Synced (a sync already confirmed it -- nothing local left to undo).
    bool cached;
    size_t taskEntryIndex;  // Index into TODOIST_TASKS.getCompletedTodayEntries().
    std::string habitId;    // HABITIFY_HABITS habit id.
  };
  // Today's completed tasks/habits, in completion order -- same source
  // LogsActivity's own loadEntries() once read (TodoistTaskCache::
  // getCompletedTodayEntries() plus completed habits). Rebuilt fresh every
  // call rather than stored.
  std::vector<LogEntry> logEntries() const;
  // Right2 on a Cached row (see clearLogRow()'s own comment for what "Cached"
  // means for a task vs. a habit): actually reverses the completion --
  // TodoistTaskCache::cancelCompletedLogEntry() for a task,
  // HabitifyHabitCache::undoLocalCompletion() for a habit. A Synced row (a
  // sync already confirmed it) has nothing local left to undo, so Right2 is
  // simply not offered there (see render()'s own confirmLabel logic) --
  // clearLogRow() is never called for one.
  void clearLogRow(const LogEntry& entry);

  // Go opens this. Same [action, Focus session] choice Tasks/Habits show on
  // a row, resolved against itemId rather than a selected row.
  void showOptions();
  void completeSuggestedTask();
  void logSuggestedHabit();
  // The Options menu's "Complete" entry for a habit suggestion opens this:
  // marks it done directly, via organizerActions::completeHabit() - works
  // even for a goal-less habit logSuggestedHabit()'s number entry cannot
  // touch.
  void completeSuggestedHabit();
  // The Options menu's "Focus session" entry opens this: a duration picker,
  // then organizerActions::beginFocusSession() for the suggested item.
  void offerFocusSession();
  // The Options menu's "Reschedule" entry opens this (task suggestions only -
  // a habit has no due date): a sub-choice between picking a new date and
  // clearing the due date entirely.
  void offerReschedule();
  // "Pick a date" from offerReschedule()'s sub-menu: the date picker itself.
  void offerRescheduleDatePicker();
  // "No date" from offerReschedule()'s sub-menu: clears the due date directly,
  // no further confirmation - same immediacy as Complete.
  void clearTaskDueDate();

  // The cache indices that count toward the companion's mood right now --
  // same criteria quickpick::roll() itself pools from (see its own comment,
  // and currentPickStillEligible()'s identical task predicate): tasks
  // overdue or due today, habits not yet complete. Rebuilt fresh every call
  // rather than stored, the same way logEntries() already is.
  std::vector<size_t> relevantTaskIndices() const;
  std::vector<size_t> relevantHabitIndices() const;
  // How many rows the active tab currently has -- logEntries().size() for
  // Logs, relevantTaskIndices()/relevantHabitIndices().size() for the other
  // two. Used by loop()'s own Left1/Left2 traversal so it does not need to
  // know which list backs whichever tab is active.
  size_t activeTabRowCount() const;

  // Tasks tab row action -- mirrors TasksActivity's own showRowOptions() and
  // the actions it leads to, resolved against a task id (re-resolved to a
  // cache index at each step, the same "re-resolve after a popup closes"
  // pattern completeSuggestedTask()/offerRescheduleDatePicker() above
  // already use for itemId) rather than a row list this screen owns a copy
  // of -- a cache index alone would go stale if the list changes while a
  // popup from an earlier step is still open.
  void showTaskRowOptions(size_t cacheIndex);
  void completeTaskRow(const std::string& taskId);
  void offerRescheduleRow(const std::string& taskId);
  void offerRescheduleDatePickerRow(const std::string& taskId);
  void clearTaskDueDateRow(const std::string& taskId);
  void offerFocusSessionForTask(const std::string& taskId);

  // Habits tab row action -- mirrors HabitsActivity's own showRowOptions().
  void showHabitRowOptions(size_t cacheIndex);
  void logHabitRow(const std::string& habitId);
  void completeHabitRow(const std::string& habitId);
  void offerFocusSessionForHabit(const std::string& habitId);

  // Whether the current pick is still a valid quickpick candidate: present in
  // its cache and, for a habit, still short of its target. Checked once an
  // action has actually mutated the cache -- a completed task is gone from
  // the cache outright, and a habit logged to its target drops out the same
  // way roll()'s own pool would exclude it. Only then is a fresh suggestion
  // rolled; Focus session, a cancelled popup, or a habit log that leaves it
  // still short of target all leave the bubble showing exactly what it did
  // before.
  bool currentPickStillEligible() const;

  // Common tail of every suggestion action above: rerolls the suggestion if
  // what it was showing is no longer eligible, then repaints.
  void afterRowAction();

  // render()'s own three tab bodies, sharing the rect below the tab bar.
  void renderLogsTab(int top, int height) const;
  void renderTasksTab(int top, int height) const;
  void renderHabitsTab(int top, int height) const;

  // One line of the glance strip's own Calendar bullet list (see this
  // file's own header comment and todaysEvents()'s own comment). isFallback
  // means title is the whole line (not synced yet / nothing today) -- no
  // bullet, no time, drawn plain.
  struct GlanceEventRow {
    std::string title;
    std::string time;  // ", HH:MM" (pre-formatted, own leading separator), or empty.
    bool isFallback = false;
  };
  // Today's own Calendar events, one row per event, in GCAL_EVENTS' own
  // chronological order -- always at least one row (a fallback row covers
  // "not synced yet" and "nothing today" alike, so render() never needs to
  // check hasSynced() itself).
  std::vector<GlanceEventRow> todaysEvents() const;
  // Temporarily unused (see render()'s own comment) -- kept, not deleted,
  // since the glance strip is meant to grow back to include these.
  std::string glanceBudgetWants() const;
  std::string glanceNewestAlert() const;

  std::string pickedText;
  std::string itemId;
  bool isHabit;
  bool poolEmpty;

  // Row cursor into whichever list backs the active tab (logEntries(),
  // relevantTaskIndices(), or relevantHabitIndices()) -- a row index into
  // that list, not a cache index. Reset to 0 whenever the tab changes (see
  // switchTab()'s own comment): the old cursor was into a different list.
  int logSelectedRow = 0;
  // Which of this screen's three sections has focus (see this file's own
  // header comment for the full Logs/Companion/Header loop). Companion and
  // Header are single stops with no cursor of their own -- only the Logs
  // section needs logSelectedRow/tabBarFocused above/below.
  enum class Focus { Logs, Companion, Header };
  Focus focus = Focus::Companion;

  enum class Tab : uint8_t { Tasks = 0, Habits = 1, Logs = 2 };
  static constexpr int TAB_COUNT = 3;
  Tab nextTab() const { return static_cast<Tab>((static_cast<int>(activeTab) + 1) % TAB_COUNT); }
  // The tab bar's own label for a given tab, translated.
  static const char* tabLabel(Tab tab);
  // Switches the active tab, resetting the row cursor -- the old cursor was
  // into a different list, and rebuilding that list to check it is still in
  // range is not worth it for what a fresh 0 already gives for free. Does
  // not touch tabBarFocused: only ever called while the tab bar itself is
  // focused (Right2 there cycles tabs without leaving the tab bar).
  void switchTab(Tab next);

  Tab activeTab = Tab::Logs;
  // While focus == Logs: whether the tab bar itself, not a row, has the
  // highlight -- "the start of the Logs section" (see this file's own
  // header comment), and always reachable there regardless of whether the
  // active tab has any rows (mirrors OrganizerScreenActivity's own "index 0
  // is the tab bar" convention, just as a separate bool instead of folding
  // it into logSelectedRow's own index space).
  bool tabBarFocused = false;

  // Mirrors HomeActivity's own lastCompanionRefreshMs/lastCompanionMood --
  // this screen is reachable directly from sleep (CrossPointState
  // reconstruction) and can sit open just as long as Home, so it needs the
  // same idle re-check rather than only refreshing once at onEnter() (see
  // loop()'s own comment): without it, a mood that should have decayed, or
  // crossed into the sleep window, while this screen was already open
  // stayed stale until the user happened to visit Home, which is the only
  // other place that calls CompanionTracker::refreshForDisplay().
  unsigned long lastCompanionRefreshMs = 0;
  companion::Mood lastCompanionMood = companion::Mood::Happy;

  // See OrganizerScreenActivity's own swallow flags for why these exist: the
  // Options popup (and the confirmation or number entry it can lead to)
  // answers on a button press, not its release, and that release is still
  // owed to this screen once the sub-activity it was pushed from closes.
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;
  // Side Up/Down jump to the previous/next app in the home grid's own order
  // -- the same shortcut every other app screen has (see
  // OrganizerScreenActivity/SettingsActivity's own identical block). Guarded
  // by a fresh-press check the same way Right1/Right2 are above, in case one
  // was already held down when some other gesture left this screen.
  bool upPressSeen = false;
  bool downPressSeen = false;
};
