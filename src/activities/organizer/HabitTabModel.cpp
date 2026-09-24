#include "HabitTabModel.h"

#include <HabitifyHabitCache.h>
#include <HabitifyStore.h>
#include <I18n.h>

#include <cstdio>

namespace habitTabModel {

bool matchesArea(const std::string& areaId, const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return false;
  if (areaId.empty() || isLogsAreaId(areaId)) return false;
  if (areaId == NO_AREA_ID) return habits[cacheIndex].areaId.empty();
  return habits[cacheIndex].areaId == areaId;
}

bool isVisible(const size_t cacheIndex) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  if (cacheIndex >= habits.size()) return false;
  if (HABITIFY_STORE.getHideCompleted() && habits[cacheIndex].isComplete()) return false;
  return true;
}

int countForArea(const std::string& areaId) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  int count = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (matchesArea(areaId, i)) count++;
  }
  return count;
}

int rowCountFor(const std::string& areaId) {
  const auto& habits = HABITIFY_HABITS.getHabits();
  int count = 0;
  if (isLogsAreaId(areaId)) {
    for (const auto& habit : habits) {
      if (habit.isComplete()) count++;
    }
    return count;
  }
  for (size_t i = 0; i < habits.size(); i++) {
    if (isVisible(i) && matchesArea(areaId, i)) count++;
  }
  return count;
}

int logEntryIndexForRow(const int row) {
  if (row < 0) return -1;
  const auto& habits = HABITIFY_HABITS.getHabits();
  int seen = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (!habits[i].isComplete()) continue;
    if (seen == row) return static_cast<int>(i);
    seen++;
  }
  return -1;
}

int cacheIndexForRow(const std::string& areaId, const int row) {
  if (row < 0) return -1;
  if (isLogsAreaId(areaId)) return logEntryIndexForRow(row);
  const auto& habits = HABITIFY_HABITS.getHabits();
  int seen = 0;
  for (size_t i = 0; i < habits.size(); i++) {
    if (!isVisible(i) || !matchesArea(areaId, i)) continue;
    if (seen == row) return static_cast<int>(i);
    seen++;
  }
  return -1;
}

int rebuildVisibleAreas(const std::string& wanted, std::vector<std::string>& visibleAreaIds) {
  visibleAreaIds.clear();
  visibleAreaIds.reserve(HABITIFY_HABITS.getAreas().size() + 2);
  // Each area earns its place by having a habit, same as the Tasks date tabs.
  for (const auto& area : HABITIFY_HABITS.getAreas()) {
    if (countForArea(area.id) > 0) visibleAreaIds.push_back(area.id);
  }
  // With no All tab to catch them, habits assigned to no area get a tab of
  // their own -- only when there are any.
  if (countForArea(NO_AREA_ID) > 0) visibleAreaIds.push_back(NO_AREA_ID);
  // Logs always shows -- it's a log view, not a filter, so an empty state
  // ("nothing completed today yet") is itself useful feedback rather than
  // noise to hide.
  visibleAreaIds.push_back(LOGS_AREA_ID);

  for (size_t i = 0; i < visibleAreaIds.size(); i++) {
    if (visibleAreaIds[i] == wanted) return static_cast<int>(i);
  }
  return 0;
}

const char* tabLabel(const std::string& areaId) {
  if (isLogsAreaId(areaId)) return tr(STR_LOGS);
  if (areaId == NO_AREA_ID) return tr(STR_HABITS_TAB_NO_AREA);
  return HABITIFY_HABITS.getAreaName(areaId);
}

void formatProgress(const HabitifyHabit& habit, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  // %g rather than %f: a count-based habit reads "1/3", not "1.000000/3.000000",
  // and a distance habit still shows its fraction as "2.5/5".
  if (!habit.hasTarget()) {
    // No goal means no denominator to show; the accumulated figure is all there
    // is to say about it.
    snprintf(out, outSize, "%g", static_cast<double>(habit.shownCurrent()));
    return;
  }
  // completedByStatus can go true (Complete tapped locally, or Habitify's own
  // status already says done) before current/pending's own arithmetic has
  // caught up to target -- show target/target rather than a fraction that
  // would still read as short right next to the row's own "done" styling (see
  // isComplete()). The underlying figures are untouched; this only affects
  // what gets drawn.
  const float shown =
      habit.completedByStatus && habit.shownCurrent() < habit.target ? habit.target : habit.shownCurrent();
  snprintf(out, outSize, "%g/%g", static_cast<double>(shown), static_cast<double>(habit.target));
}

}  // namespace habitTabModel
