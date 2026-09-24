#pragma once

#include <CompanionMood.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/organizer/HabitTabModel.h"
#include "activities/organizer/TaskTabModel.h"

/**
 * The companion's own screen -- and, since ActivityManager::goHome() now
 * constructs this instead of HomeActivity, the app's actual Home. Every
 * "Home"/"Apps" button anywhere in the app lands here; the old app-tile grid
 * (HomeActivity) is left fully intact but unreachable. A big pose of the
 * companion's figure, its mood, and a speech bubble -- the newest BLE alert
 * not yet dismissed from here (see CompanionAlertBubble), or a plain
 * mood-flavored idle line once there's none left. Never a task/habit
 * suggestion (that's what the embedded Tasks section below is for), and the
 * companion no longer reacts to anything else (a completion, an event, a
 * budget change, ...) -- alerts and idle are the only two things the bubble
 * ever shows. A glance strip sits right under the header -- today's own
 * Google Calendar events, bulleted (see todaysEvents()'s own comment; there
 * can be more than one, so this is a plain bullet, not that one app's own
 * icon), collapsing to nothing when there is genuinely nothing today rather
 * than spending space on a "nothing today" line. The YNAB "Wants" balance is
 * temporarily out of this strip (see render()'s own comment;
 * glanceBudgetWants() is still here for when it comes back) -- the newest BLE
 * alert now belongs to the bubble instead of this strip permanently (see the
 * speech bubble description above); glanceNewestAlert() is unused for that
 * reason, not temporarily, but is left alongside glanceBudgetWants() for the
 * same "kept, not deleted" reasoning. The header itself carries no title or
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
 * scaled-down rendering of the *real* Tasks screen itself: a single thin
 * rule marking where it starts (no title of its own, and no Highscore any
 * more -- the companion's best-ever single-day total is only used for the
 * Milestone mood now, see DayLedger::bestDayPoints), and its own tab bar
 * (Overdue/Today/Upcoming/No date/Logs, whichever have rows
 * -- there is no "All" tab; the first visible one is the default -- see
 * activities/organizer/TaskTabModel.h, shared with the real TasksActivity
 * screen so the two never disagree about which task is in which tab),
 * confined to the space budget below the companion figure
 * (COMPANION_BUDGET_PERCENT). Acting on a row here updates the mood right
 * where it's shown, without leaving to the real screen. The side Up/Down
 * buttons (labelled Tasks/Habits, see below) swap that whole section for a
 * scaled-down rendering of the real *Habits* screen -- under the same rule,
 * its area tab bar (one per Habitify area / No area / Logs, see
 * activities/organizer/HabitTabModel.h, shared with HabitsActivity the same
 * way TaskTabModel is with TasksActivity) and habit rows -- in the same
 * space. Tasks is the default on every entry. Note that this screen has no
 * button that opens the real Tasks/Habits/Budget screens: the side buttons
 * that used to cycle apps now switch sections, and Right1 on the tab bar is
 * Random.
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
 * Focus enum) cycles through four stops in one continuous circular loop, in
 * visual top-to-bottom order -- the header, the glance strip, the companion
 * figure, then the embedded Tasks section -- wrapping back to the header
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
 *     variant, since drawHeader() has no such parameter. Right1 opens
 *     Settings; Right2 is Sync All (activityManager.goToSyncAll()) instead
 *     of Clear/Select, since there's no per-row action to offer here.
 *   - Glance strip: the whole strip is one stop, not one per event -- there
 *     can be several bulleted events, but Right1's own action (open
 *     Calendar) is the same regardless of which one is showing. Inverted the
 *     same way the header is when focused. Skipped by Left1/Left2 entirely
 *     when there is genuinely nothing today (see todaysEvents()'s own
 *     comment) -- an empty strip has nothing to land on, same reasoning the
 *     old mood-label stop used to skip itself when hidden.
 *   - Companion figure: the bubble itself inverts (black background, white
 *     text) rather than a selection-box outline around the figure -- the
 *     bubble is the thing being acted on here, so the highlight belongs on
 *     it directly. Right2 is Dismiss when an alert is currently showing
 *     (steps to the next-newest one not yet dismissed, see
 *     CompanionAlertBubble), and does nothing when the bubble is showing its
 *     idle line instead -- there's nothing to dismiss then. Right1 opens the
 *     Alerts screen (only a real destination in builds with
 *     ENABLE_BLE_NOTIFY_SPIKE defined; a no-op otherwise).
 *   - Embedded section (Tasks or Habits, whichever is showing): the tab bar
 *     itself (see tabBarFocused's own comment) is always the first stop --
 *     "the start of the section" -- reachable regardless of whether the
 *     active tab has any rows; Right2 there cycles to the next of the
 *     currently-visible tabs (same convention every other tabbed screen in
 *     this app uses: OrganizerScreenActivity's own "index 0 is the tab
 *     bar"), and Right1 is Random: it drops onto a random row of the active
 *     tab (nothing to randomize on a Logs tab, so it is blank there). From a
 *     row, Right1 is Back instead: it returns the cursor to that tab bar --
 *     the same two-level Back the real Tasks/Habits/Calendar/Budget/Settings
 *     screens now have. Past the tab bar, each tab's own rows are actionable
 *     -- Right2 opens the same Options popup a real row's own action does
 *     (see showTaskRowOptions()/showHabitRowOptions()). On a Logs tab, Right2
 *     is Clear on a Cached row (undoes the local completion, same as the old
 *     standalone Logs screen).
 *
 * Side Up/Down are overridden on this screen only: everywhere else in the
 * app they jump to the previous/next app in the home grid's own order (see
 * OrganizerScreenActivity/SettingsActivity's own identical block), but here
 * they are labelled Tasks/Habits and choose which section shows below the
 * companion (switchSection()), from whichever focus stop the cursor is on --
 * independent of Left1/Left2/Right1/Right2 above. This is the one screen
 * from which the app-jump shortcut is not reachable at all. The section
 * below the companion never starts above the bottom of the longest of
 * those two labels' boxes plus a little spacing (SECTION_SIDE_BUTTON_GAP),
 * so the labels never sit beside its tab bar or rows; it sits lower than
 * that whenever the companion above it (calendar events, a longer bubble)
 * takes more room.
 */
class QuickPickActivity final : public Activity {
 public:
  QuickPickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QuickPick", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isQuickPickActivity() const override { return true; }

 private:
  // Declared ahead of its first use in the method declarations below.
  enum class Section { Tasks, Habits };

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

  // Habits row action -- mirrors HabitsActivity's own showRowOptions() and the
  // actions it leads to (Log a number, Complete, Focus session), resolved
  // against a habit id for the same reason the task actions above are.
  void showHabitRowOptions(size_t cacheIndex);
  void logHabitRow(const std::string& habitId);
  void completeHabitRow(const std::string& habitId);
  void offerFocusSessionForHabit(const std::string& habitId);
  // Right2 on a Cached Logs row of the Habits section: undoes today's local
  // completion, the same action HabitsActivity's own Logs tab offers. No-op on
  // a Synced row or when the active tab isn't Logs.
  void clearSelectedHabitLogRow();

  // Which embedded section is showing and its row/tab facts, so the input and
  // render code can treat Tasks and Habits alike wherever they behave alike.
  // embeddedRow() is the active section's own row cursor.
  int embeddedRowCount() const;
  bool embeddedOnLogs() const;
  int& embeddedRow() { return section == Section::Tasks ? taskSelectedRow : habitSelectedRow; }
  int embeddedRow() const { return section == Section::Tasks ? taskSelectedRow : habitSelectedRow; }

  // Shows `next` in the embedded section (the side Up/Down buttons, labelled
  // Tasks/Habits). Lands on that section's tab bar, whichever focus stop the
  // cursor was on. No-op if it is already showing.
  void switchSection(Section next);

  // Moves the active section to the previous (delta -1) or next (delta +1)
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

  // Recomputes both sections' tab sets (visibleTabs/activeKind, see
  // TaskTabModel::rebuildVisibleTabs; visibleAreaIds/activeAreaId, see
  // HabitTabModel::rebuildVisibleAreas) and clamps each row cursor into its
  // rebuilt tab's row count. Called from onEnter() and afterRowAction().
  void rebuildEmbeddedTabs();

  // The embedded Tasks section's own row renderer, confined to the given
  // (top, height) band below the companion figure -- same convention this
  // screen's own tab bar/header already use for staying inside that budget.
  void renderTasksTab(int top, int height) const;
  // Same, for the Habits section: a habit's name with its progress hard right
  // (or the Cached/Synced two-line style on the Logs tab).
  void renderHabitsTab(int top, int height) const;

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
  // glanceBudgetWants() is temporarily unused (see render()'s own comment)
  // -- kept, not deleted, since the glance strip is meant to grow back to
  // include it. glanceNewestAlert() is unused for a different, permanent
  // reason: the newest alert is the bubble's job now (see this file's own
  // header comment) -- kept alongside its sibling rather than deleted for
  // the same reason.
  std::string glanceBudgetWants() const;
  std::string glanceNewestAlert() const;

  // Row cursor into the embedded Tasks section's active tab -- a row index
  // into whichever list backs activeKind (the live task cache for the five
  // real kinds, TODOIST_TASKS.getCompletedTodayEntries() for LOGS), not a
  // cache index itself. Reset by rebuildEmbeddedTabs() whenever the tab set
  // changes.
  int taskSelectedRow = 0;
  // Row cursor into the Habits section's active tab -- same idea as
  // taskSelectedRow, for HabitTabModel's rows.
  int habitSelectedRow = 0;
  // Which of the two embedded sections below the companion is showing --
  // always Tasks on entry; switched by the side Up/Down buttons.
  Section section = Section::Tasks;
  // Which of this screen's four stops has focus (see this file's own header
  // comment for the full loop order). Header, Glance and Companion are
  // single stops with no cursor of their own -- only the embedded section
  // (whichever of Tasks/Habits is showing) needs its row cursor and
  // tabBarFocused.
  enum class Focus { Embedded, Header, Glance, Companion };
  Focus focus = Focus::Companion;

  // Tabs currently visible in the embedded section, and which one is active
  // -- same shape as TasksActivity's own visibleTabs/currentKind(), kept in
  // sync with it via the shared TaskTabModel (see this file's own header
  // comment).
  std::vector<taskTabModel::TaskTabKind> visibleTabs{taskTabModel::TaskTabKind::OVERDUE};
  taskTabModel::TaskTabKind activeKind = taskTabModel::TaskTabKind::OVERDUE;
  // The Habits section's equivalents: the area ids of the tabs on screen
  // (habitTabModel::NO_AREA_ID for habits with no area, LOGS_AREA_ID the
  // trailing Logs tab) and which one is active. "" means nothing chosen yet,
  // which resolves to the first tab, the same one HabitsActivity opens on.
  std::vector<std::string> visibleAreaIds{""};
  std::string activeAreaId;
  // While focus == Embedded: whether the tab bar itself, not a row, has the
  // highlight -- "the start of the embedded section" (see this file's own
  // header comment), and always reachable there regardless of whether the
  // active tab has any rows (mirrors OrganizerScreenActivity's own "index 0
  // is the tab bar" convention, just as a separate bool instead of folding
  // it into the row cursor's own index space). Shared by both sections: only
  // one is ever showing.
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
  // Side Up/Down switch the embedded section to Tasks/Habits on this screen
  // (see switchSection() and this file's own header comment) rather than the
  // app-jump shortcut every other screen's side buttons have. Guarded by a
  // fresh-press check the same way Right1/Right2 are above, in case one was
  // already held down when some other gesture left this screen.
  bool upPressSeen = false;
  bool downPressSeen = false;

  // millis() at onEnter(), so loop() can briefly ignore a Right1 release
  // right after landing here -- see RIGHT1_ENTRY_GRACE_MS's own comment.
  unsigned long enteredAtMs = 0;
};
