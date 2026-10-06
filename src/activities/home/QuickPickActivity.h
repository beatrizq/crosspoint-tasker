#pragma once

#include <CompanionMood.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/organizer/TaskTabModel.h"

/**
 * The companion's own screen -- the main screen's "Tasker" tile
 * (MainMenuActivity), reached with ActivityManager::goToCompanion(). Home itself
 * is that main screen now, so this one is a level below it: its status bar's
 * Right1 goes back there, and so does the home gesture. A big pose of the
 * companion's figure, its mood, and a speech bubble holding only the
 * companion's own remarks -- a plain mood-flavored idle line, or the sleeping
 * line. Never a task suggestion (that's what the embedded Tasks section below
 * is for), never a BLE alert, and the companion does not react to anything
 * else. The figure and bubble can be hidden entirely (Settings -> Companion ->
 * Show Companion), which hands their space to the Tasks section.
 *
 * At the top sit the header and a glance strip -- today's own Google Calendar
 * events, bulleted (see todaysEvents()'s own comment; there can be more than
 * one, so this is a plain bullet, not that one app's own icon), collapsing to
 * nothing when there is genuinely nothing today rather than spending space on
 * a "nothing today" line. While any BLE alert is pending, the alert area takes
 * both their places, showing one alert at a time: the newest pending one,
 * with older ones piled up underneath it. Dismissing it brings the next-newest
 * up, and so on. Once the last is gone the header and glance strip are back.
 * The header itself carries no title or
 * subtitle of this screen's own (drawHeader() is called with both nullptr;
 * see render()'s own comment for why) -- the companion's name and its
 * Age/Highscore, shown here on earlier iterations of this screen, are gone.
 * On Lyra/Lyra 3 Covers the header is where that theme's own
 * clock+date+battery combo lives (LyraTheme::drawHeader draws it
 * unconditionally into whatever rect it's given, regardless of
 * title/subtitle) -- this screen does not draw a copy of that itself;
 * hovering the header (see the Focus enum) inverts whatever the theme drew
 * there, clock/date/battery included.
 *
 * Straight below the companion figure -- no mood label any more (removed
 * entirely, freeing that space for the section below to sit higher) -- is a
 * scaled-down rendering of the *real* Tasks screen itself: a title row listing
 * the Todoist projects that have tasks under the Filter setting (see
 * rebuildProjectEntries()) followed by Logs, the selected one in bold and the
 * rest regular, with the thick header rule under it (the top line of the tab
 * bar, no clock/battery row of its own), and its own tab bar
 * (Overdue/Today/Upcoming/No date, whichever have rows for the selected
 * project -- there is no "All" tab; the first visible one is the default; Logs
 * is selected from the title row like a project and shows today's completions
 * with no tab bar -- see
 * activities/organizer/TaskTabModel.h, shared with the real TasksActivity
 * screen so the two never disagree about which task is in which tab),
 * confined to the space budget below the companion figure
 * (COMPANION_BUDGET_PERCENT). Acting on a row here updates the mood right
 * where it's shown, without leaving to the real screen. The side Up/Down
 * buttons (unlabelled, see below) step to the previous/next entry of the title
 * row, wrapping. Note that this screen has no button that opens the real Tasks
 * screen: the side buttons that used to cycle apps now step through projects,
 * and Right1 on the tab bar is Random.
 *
 * Reconstructed on boot from CrossPointState when the device was showing
 * this screen at the moment it went to sleep (see
 * CrossPointState::lastSleepFromQuickPick), same as every other cold-boot
 * path that lands here via goHome() -- this screen carries no per-instance
 * content of its own, so there is never a payload to reconstruct, only "open
 * the screen".
 *
 * Button IDs used here: Left1/Left2 are the pair that moves focus; Right1/
 * Right2 are the pair printed "Apps"/"Select" on the case. Focus (see the
 * Focus enum) cycles through its stops (the alert area standing in for the
 * header and glance strip while alerts are pending) in one continuous circular loop, in
 * visual top-to-bottom order -- the header, the glance strip, the companion
 * figure, the title row, then the embedded Tasks section -- wrapping back to the header
 * (Left2 = forward, Left1 = the exact reverse). Right2 acts on whatever is
 * focused, same as always; Right1 no longer means "leave" anywhere on this
 * screen -- there is nowhere left to leave *to*, this screen already is Home
 * -- so it is a per-stop shortcut instead:
 *   - Header (no title/subtitle of this screen's own, just whatever
 *     clock/date/battery the active theme draws into it): shown inverted
 *     (a plain black fill, nothing this screen draws back on top of it)
 *     rather than the companion's outline style when focused -- drawn as an
 *     overlay on top of the theme's own GUI.drawHeader() output (see
 *     render()'s own comment) rather than asking the theme for an inverted
 *     variant, since drawHeader() has no such parameter. Right1 goes back to
 *     the main screen (MainMenuActivity; Settings is one press from there);
 *     Right2 is Sync All (activityManager.goToSyncAll(), returning here) instead
 *     of Clear/Select, since there's no per-row action to offer here.
 *   - Glance strip: the whole strip is one stop, not one per event -- there
 *     can be several bulleted events, but Right1's own action (open
 *     Calendar) is the same regardless of which one is showing. Inverted the
 *     same way the header is when focused. Skipped by Left1/Left2 entirely
 *     when there is genuinely nothing today (see todaysEvents()'s own
 *     comment) -- an empty strip has nothing to land on, same reasoning the
 *     old mood-label stop used to skip itself when hidden.
 *   - Alert area (only while alerts are pending; it replaces the Header and
 *     Glance stops with a single stop of its own, inverted when focused).
 *     Right2 dismisses the alert on top, revealing the next-newest (dismissal
 *     is soft -- the Alerts screen still lists it); Right1 opens the Alerts
 *     screen. Header actions
 *     (Settings, Sync All) are out of reach until the last alert is dismissed.
 *   - Companion figure: the bubble itself inverts (black background, white
 *     text) rather than a selection-box outline around the figure. Right2
 *     does nothing -- the bubble only holds the companion's own remarks.
 *     Right1 opens the Alerts screen (only a real destination in builds with
 *     ENABLE_BLE_NOTIFY_SPIKE defined; a no-op otherwise). Not a stop at all
 *     while the companion is hidden.
 *   - Title row (the Todoist projects, then Logs): while focused the selected
 *     entry sits in the same rounded black pill a selected tab does; unfocused
 *     it is just bold. Right1/Right2 step to the previous/next entry,
 *     wrapping, and are labelled with where they would land; the selected
 *     entry is drawn bold. Selecting one reloads the section below it on its
 *     first tab.
 *   - Embedded section: the tab bar
 *     itself (see tabBarFocused's own comment) is always the first stop --
 *     "the start of the section" -- reachable regardless of whether the
 *     active tab has any rows; Right2 there cycles to the next of the
 *     currently-visible tabs (same convention every other tabbed screen in
 *     this app uses: OrganizerScreenActivity's own "index 0 is the tab
 *     bar"), and Right1 is Random: it drops onto a random row of the active
 *     tab (nothing to randomize on a Logs tab, so it is blank there). From a
 *     row, Right1 is Back instead: it returns the cursor to that tab bar --
 *     the same two-level Back the real Tasks/Calendar/Settings screens now
 *     have. Past the tab bar, each tab's own rows are actionable
 *     -- Right2 opens the same Options popup a real row's own action does
 *     (see showTaskRowOptions()). On a Logs tab, Right2
 *     is Clear on a Cached row (undoes the local completion, same as the old
 *     standalone Logs screen).
 *
 * Side Up/Down are overridden on this screen only: everywhere else in the
 * app they jump to the previous/next app in the home grid's own order (see
 * OrganizerScreenActivity/SettingsActivity's own identical block), but here
 * they step the title row to the previous/next project (stepProject()), from
 * whichever focus stop the cursor is on -- independent of
 * Left1/Left2/Right1/Right2 above. They carry no on-screen labels here, so the
 * section below the companion uses the full width of the screen. This is the one
 * screen from which the app-jump shortcut is not reachable at all.
 */
class QuickPickActivity final : public Activity {
 public:
  // `openOptionsForTaskId`: when set, this screen opens that task's Select menu
  // (Complete, Focus session, Reschedule...) as soon as it is up -- how a
  // finished focus session hands its task back, since the user may now want to
  // say it is done.
  QuickPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string openOptionsForTaskId = "")
      : Activity("QuickPick", renderer, mappedInput), pendingOptionsTaskId(std::move(openOptionsForTaskId)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isQuickPickActivity() const override { return true; }

 private:
  // Tasks tab row action -- mirrors TasksActivity's own showRowOptions() and
  // the actions it leads to, resolved against a task id (re-resolved to a
  // cache index at each step -- a cache index alone would go stale if the
  // list changes while a popup from an earlier step is still open) rather
  // than a row list this screen owns a copy of.
  void showTaskRowOptions(size_t cacheIndex);
  void completeTaskRow(const std::string& taskId);
  void offerRescheduleRow(const std::string& taskId);
  void offerRescheduleDatePickerRow(const std::string& taskId);
  void clearTaskDueDateRow(const std::string& taskId);
  void offerFocusSessionForTask(const std::string& taskId);

  // The embedded section's row/tab facts. embeddedRow() is its row cursor.
  int embeddedRowCount() const;
  bool embeddedOnLogs() const;
  int& embeddedRow() { return taskSelectedRow; }
  int embeddedRow() const { return taskSelectedRow; }

  // One entry of the title row: a Todoist project that has tasks under the
  // Filter, or Logs (today's completions, shown like one more project but with
  // no tab bar). `scope` is what taskTabModel scopes the tasks by.
  struct ProjectEntry {
    uint32_t scope = 0;
    std::string name;
    bool isLogs = false;
  };
  // Rebuilds projectEntries from the cache -- the projects that have tasks, a
  // generic "Tasks" group for tasks whose project is unknown (or when none has
  // a name yet), and Logs last -- keeping the same entry selected where it
  // survives. Called on entry and after any change to the task list.
  void rebuildProjectEntries();
  // The taskTabModel scope of the selected entry.
  uint32_t currentScope() const;
  bool onLogsEntry() const;
  // Selects entry `index`: first tab, first row, tab bar focused (or, on Logs,
  // no tab bar to focus).
  void selectProject(size_t index);
  // Previous (-1) / next (+1) entry, wrapping at both ends -- Right1/Right2 on
  // the title row and the side Up/Down buttons. Focus stays on the title row if
  // it was there, and otherwise lands at the start of the section below it.
  void stepProject(int delta);
  // Moves focus to the start of the Tasks section: its tab bar, or on Logs its
  // first row (the title row itself when there are none).
  void focusEmbeddedFirst();

  // Moves the embedded section to the previous (delta -1) or next (delta +1)
  // visible tab, wrapping at both ends. Right2 on the tab bar. No-op with
  // fewer than two tabs.
  void switchTab(int delta);

  // Right2/Right1(Select) on a Cached Logs row: undoes today's local
  // completion, the same action TasksActivity's own Logs tab offers. No-op
  // on a Synced row or when the active kind isn't LOGS.
  void clearSelectedLogTaskRow();

  // Common tail of every tab-row action above: repaints once the popup chain
  // it led to has closed. A completion/reschedule can change which tabs
  // exist (same as the real screen), so this also rebuilds the tab set.
  void afterRowAction();

  // Recomputes the tab set (visibleTabs/activeKind, see
  // TaskTabModel::rebuildVisibleTabs) and clamps the row cursor into the
  // rebuilt tab's row count. Called from onEnter(), afterRowAction() and
  // switchFilter().
  void rebuildEmbeddedTabs();

  // The embedded Tasks section's own row renderer, confined to the given
  // (top, height) band below the companion figure -- same convention this
  // screen's own tab bar/header already use for staying inside that budget.
  void renderTasksTab(int top, int height) const;

  // One line of the glance strip's own Calendar bullet list (see this
  // file's own header comment and todaysEvents()'s own comment). isFallback
  // means title is the whole line (not synced yet) -- no bullet, no time,
  // drawn plain.
  struct GlanceEventRow {
    std::string title;
    std::string time;  // ", HH:MM" (pre-formatted, own leading separator), or empty.
    bool isFallback = false;
  };
  // Today's own Calendar events, one row per event, in GCAL_EVENTS' own
  // chronological order. Empty (not a fallback row) when synced and there is
  // genuinely nothing today; a fallback row only ever covers "not synced
  // yet" (see todaysEvents()'s own comment).
  std::vector<GlanceEventRow> todaysEvents() const;
  // Pending BLE alerts, newest first -- what the alert area shows in place of
  // the header and glance strip (see this file's own header comment). Always 0
  // in builds without ENABLE_BLE_NOTIFY_SPIKE.
  static size_t pendingAlertCount();
  // Writes pending alert k's (0 = newest) "title: content" line into out; false if k is out of range.
  static bool pendingAlertLine(size_t k, char* out, size_t outSize);
  // Keeps focus on a stop that exists right now: the alert area takes over
  // the Header and Glance stops while alerts are pending and gives them back
  // once the last one is dismissed, and the Companion stop is gone while the
  // companion is hidden.
  void reconcileFocus();
  // Whether the companion figure and its bubble are drawn (Settings -> Companion).
  static bool companionVisible();
  // Focus-loop moves in and out of the top block (the alert area, or else the
  // header and glance strip) -- shared by Left1/Left2 so the two directions
  // cannot disagree about which stops exist.
  void focusTopBlockFirst();
  void focusTopBlockLast();
  void focusAfterTopBlock();
  void focusEmbeddedLast();

  // Row cursor into the embedded Tasks section's active tab -- a row index
  // into whichever list backs activeKind (the live task cache for the five
  // real kinds, TODOIST_TASKS.getCompletedTodayEntries() for LOGS), not a
  // cache index itself. Reset by rebuildEmbeddedTabs() whenever the tab set
  // changes.
  int taskSelectedRow = 0;
  // The title row's entries and which one is selected (see ProjectEntry).
  std::vector<ProjectEntry> projectEntries;
  size_t projectIndex = 0;
  // Which of this screen's stops has focus (see this file's own header
  // comment for the full loop order). Header, Glance and Companion are
  // single stops with no cursor of their own -- only the embedded section
  // needs its row cursor and tabBarFocused.
  enum class Focus { Embedded, Header, Glance, Alerts, Companion, Projects };
  Focus focus = Focus::Companion;
  // Pending alerts the last loop() saw, so one arriving (or being dismissed
  // elsewhere) repaints the screen.
  size_t lastAlertCount = 0;

  // Tabs currently visible in the embedded section, and which one is active
  // -- same shape as TasksActivity's own visibleTabs/currentKind(), kept in
  // sync with it via the shared TaskTabModel (see this file's own header
  // comment).
  std::vector<taskTabModel::TaskTabKind> visibleTabs{taskTabModel::TaskTabKind::OVERDUE};
  taskTabModel::TaskTabKind activeKind = taskTabModel::TaskTabKind::OVERDUE;
  // While focus == Embedded: whether the tab bar itself, not a row, has the
  // highlight -- "the start of the embedded section" (see this file's own
  // header comment), and always reachable there regardless of whether the
  // active tab has any rows (mirrors OrganizerScreenActivity's own "index 0
  // is the tab bar" convention, just as a separate bool instead of folding
  // it into the row cursor's own index space).
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
  // How many glance rows the last idle check found, so an event ending (which
  // removes its row) repaints the screen -- see loop()'s own comment. -1 until
  // the first check, which then repaints once at most.
  int lastGlanceRowCount = -1;
  // Which of companion::idleBubbleText()'s IDLE_BUBBLE_VARIANT_COUNT lines
  // the bubble shows for the current mood, when it's showing an idle line at
  // all (see render()'s own comment) -- rolled fresh in onEnter() and again
  // whenever the mood changes (loop()'s own periodic re-check), so the same
  // remark doesn't repeat on every visit or every mood shift, but also
  // doesn't change mid-visit just from moving focus around the screen.
  uint8_t idleVariant = 0;

  // See OrganizerScreenActivity's own swallow flags for why these exist: the
  // Options popup (and the confirmation or number entry it can lead to)
  // answers on a button press, not its release, and that release is still
  // owed to this screen once the sub-activity it was pushed from closes.
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;
  // Side Up/Down switch the Todoist filter (Filter 1 / Filter 2) on this screen
  // (see switchFilter() and this file's own header comment) rather than the
  // app-jump shortcut every other screen's side buttons have. Guarded by a
  // fresh-press check the same way Right1/Right2 are above, in case one was
  // already held down when some other gesture left this screen.
  bool upPressSeen = false;
  bool downPressSeen = false;

  // A task whose Select menu is still to be opened; consumed by the first loop().
  std::string pendingOptionsTaskId;

  // millis() at onEnter(), so loop() can briefly ignore a Right1 release
  // right after landing here -- see RIGHT1_ENTRY_GRACE_MS's own comment.
  unsigned long enteredAtMs = 0;
};
