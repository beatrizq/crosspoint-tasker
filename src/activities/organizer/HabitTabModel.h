#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct HabitifyHabit;

/**
 * The Habits tab set and row-resolution rules, shared between the real
 * HabitsActivity screen and QuickPickActivity's own scaled-down embedded
 * rendering of it below the companion figure -- the same extraction
 * TaskTabModel is for Tasks, so the two never disagree about which habit is
 * in which tab or which rows are visible.
 *
 * Tabs are keyed by Habitify area id (an open-ended string: areas are the
 * user's own data, not a set this app defines) rather than a fixed enum:
 * NO_AREA_ID is the tab for habits assigned to no area, LOGS_AREA_ID the
 * trailing Logs tab, and anything else a real area. There is no All tab; ""
 * is only ever "nothing chosen yet", which resolves to the first tab. A caller
 * owns its own `visibleAreaIds` vector and maps tab indexes through it, same
 * convention as TaskTabModel's `visibleTabs`.
 */
namespace habitTabModel {

// A reserved area-id string real Habitify areas can never collide with (their
// ids come from the API, plain alphanumeric) -- the sentinel for the trailing
// Logs tab, so it shares the area-id vector's index-based plumbing instead of
// needing a parallel "is this the logs tab" index space.
inline constexpr const char* LOGS_AREA_ID = "\x01__logs__";

inline bool isLogsAreaId(const std::string& areaId) { return areaId == LOGS_AREA_ID; }

// The same kind of sentinel for habits assigned to no area (their areaId is
// empty), so they stay reachable without an All tab to catch them.
inline constexpr const char* NO_AREA_ID = "\x01__none__";

// Manual log entry is capped well above anything worth tapping through by hand;
// a habit that legitimately needs more than this in one sitting is not what
// these screens are for. Shared by the real Habits screen and the embedded
// copy of it on the Companion screen.
inline constexpr int MAX_LOG_AMOUNT = 50;
inline constexpr int LOG_SMALL_STEP = 1;
inline constexpr int LOG_LARGE_STEP = 5;

// Whether the habit at `cacheIndex` (HABITIFY_HABITS.getHabits()) belongs to
// `areaId`: a real area id matches that area's own habits, NO_AREA_ID the ones
// with no area. Never true for "" or LOGS_AREA_ID.
bool matchesArea(const std::string& areaId, size_t cacheIndex);

// Whether the habit at `cacheIndex` is shown at all: the "hide completed"
// setting turns the list into what is left to do rather than a checklist of
// what is done. A habit with no goal can never read as complete, so it stays.
bool isVisible(size_t cacheIndex);

// Habits in `areaId`, ignoring the hide-completed setting: whether a tab
// exists should not flicker as habits are completed under it.
int countForArea(const std::string& areaId);

// Visible row count under `areaId`: today's completed habits for the Logs tab
// (ignoring hide-completed -- Logs is explicitly about what's done), else the
// visible habits matching the area.
int rowCountFor(const std::string& areaId);

// The HABITIFY_HABITS cache index behind visible row `row` under `areaId`, or
// -1. Handles the Logs tab too.
int cacheIndexForRow(const std::string& areaId, int row);

// Cache index behind visible Logs row `row` (completed habits, in cache
// order), or -1 out of range.
int logEntryIndexForRow(int row);

// Recomputes which area tabs have habits into `visibleAreaIds` (an area, or
// NO_AREA_ID, only when countForArea() > 0; Logs always shows), keeping
// `wanted` selected where it survives and falling back to index 0 otherwise --
// e.g. an area deleted in Habitify itself since the last sync. Returns the
// index `wanted` (or that fallback) landed at.
int rebuildVisibleAreas(const std::string& wanted, std::vector<std::string>& visibleAreaIds);

// Tab bar label for `areaId`.
const char* tabLabel(const std::string& areaId);

// Renders progress as "x/y", or as a bare count for a habit with no goal.
void formatProgress(const HabitifyHabit& habit, char* out, size_t outSize);

}  // namespace habitTabModel
