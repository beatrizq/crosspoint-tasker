#pragma once
#include <HabitifyClient.h>

#include <cstddef>
#include <string>
#include <vector>

#include "HabitTabModel.h"
#include "OrganizerScreenActivity.h"

/**
 * The Habits screen: today's habits and how far each has got.
 *
 * A row is the habit's name with its progress hard right as "x/y" - 1/3 - the
 * same layout the Budget screen gives a category and its balance, because it
 * answers the same shape of question at a glance.
 *
 * Select opens a number entry for the selected habit, defaulting to 1 so a
 * single Confirm press behaves exactly like the old one-tap "+1". It is
 * deliberately not Habitify's own "mark complete" endpoint, which fills the
 * whole goal at once: the point of showing 1/3 is being able to log the
 * second and the third separately, in whatever amount was actually done. The
 * entry only touches the cache, so it works with the radio off and the number
 * moves immediately; the accumulated amount is pushed on the next sync, one
 * request per habit however many separate amounts were logged in between,
 * before the journal is re-fetched.
 *
 * Tabs are one per Habitify Area that has a habit in it, plus a leading All --
 * the same "built from what is actually there" shape TasksActivity's tabs
 * have, but keyed by an open-ended area id rather than a fixed enum: areas are
 * the user's own data, not a set this app defines. The tab set and row
 * rules live in HabitTabModel, shared with QuickPickActivity's embedded copy
 * of this screen so the two never disagree. An area's habits do not
 * move between tabs by completing or logging them (unlike a task's due date),
 * so unlike TasksActivity, completion/logging never has to rebuild the tab
 * bar -- only loading the cache and finishing a sync do, since those are the
 * only two things that can change which areas exist.
 *
 * A trailing Logs tab (habitTabModel::LOGS_AREA_ID, a sentinel no real area id can collide
 * with) always shows too: today's completed habits, each row tagged Cached
 * (still a locally-logged completion, not yet pushed -- Right2/Select undoes
 * it) or Synced (a sync already confirmed it, nothing local left to undo) --
 * absorbed from the old standalone Logs screen, which combined this with
 * Tasks' own completions; TasksActivity's own Logs tab is the task half.
 */
class HabitsActivity final : public OrganizerScreenActivity {
 public:
  // selectHabitId, when non-empty, selects that habit's row on first paint
  // instead of row 0. One-shot -- cleared once applied.
  explicit HabitsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string selectHabitId = "")
      : OrganizerScreenActivity("Habits", renderer, mappedInput, /*initialTab=*/0),
        selectHabitId(std::move(selectHabitId)) {}

  void onEnter() override;

 protected:
  const char* screenTitle() const override;
  homeAppOrder::AppId appId() const override { return homeAppOrder::AppId::Habits; }
  int tabCount() const override { return static_cast<int>(visibleAreaIds.size()); }
  const char* tabLabel(int index) const override;
  void formatStatus(char* out, size_t outSize) const override;
  int rowCount() const override;
  void drawRow(const RowLayout& layout) const override;
  const char* emptyMessage() const override;
  const char* syncingMessage() const override;
  void startSync() override;

  // True only on the Logs tab (today's completed habits) -- the one tab
  // whose rows carry a second, Cached/Synced line.
  bool rowsHaveSubtitle() const override { return isLogsTab(tab()); }
  const char* rowConfirmLabel() const override;
  void onRowConfirm() override;
  void loadCaches() override;
  HomeMenuItem homeItem() const override { return HomeMenuItem::HABITS; }

 private:
  bool isLogsTab(int index) const { return habitTabModel::isLogsAreaId(areaIdAt(index)); }

  // The area id at `index`, or "" (All) when out of range.
  const std::string& areaIdAt(int index) const;
  // The area id the active tab holds.
  const std::string& currentAreaId() const { return areaIdAt(tab()); }

  // Right2/Confirm on a Cached Logs row: undoes today's local completion via
  // HabitifyHabitCache::undoLocalCompletion(), the same action the old
  // standalone Logs screen offered. No-op on a Synced row (nothing local
  // left to undo) or when the current tab isn't Logs.
  void clearSelectedLogRow();

  // Recomputes which area tabs have habits, keeping the active area selected
  // where it survives and falling back to All where it does not (e.g. an area
  // deleted in Habitify itself since the last sync).
  void rebuildTabs();

  // The cache index behind a visible row of the active tab, or -1 (see
  // habitTabModel::cacheIndexForRow()).
  int cacheIndexForRow(int row) const;

  // See the constructor comment. Consumed and cleared in onEnter().
  std::string selectHabitId;

  // Tabs currently on screen, in display order. Always leads with "" (All).
  std::vector<std::string> visibleAreaIds{""};

  // Select opens this first, rather than completeSelectedHabit() directly --
  // see rowConfirmLabel()/onRowConfirm().
  void showRowOptions();
  // Opens the number entry; performIncrement() is what actually moves the number.
  void completeSelectedHabit();
  void performIncrement(int cacheIndex, float amount);
  // The Options menu's "Complete" entry opens this: marks the habit done
  // directly, via organizerActions::completeHabit() - works even for a
  // goal-less habit completeSelectedHabit()'s number entry cannot touch.
  void markSelectedHabitComplete();
  void performSync();
  // The Options menu's "Focus session" entry opens this: a duration picker,
  // then organizerActions::beginFocusSession() for the same habit.
  void offerFocusSession(int cacheIndex);
};
