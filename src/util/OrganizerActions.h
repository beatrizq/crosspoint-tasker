#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;
class MappedInputManager;

/**
 * The cache-mutating core of "complete a task", shared by TasksActivity's own
 * row action and QuickPickActivity's Options menu.
 *
 * Each screen still owns what is particular to it - TasksActivity's tab
 * rebuild and selection clamp - this only holds the steps that must not drift
 * out of sync between call sites: mutate the cache, persist it, and credit the
 * companion.
 *
 * Callers are responsible for holding a RenderLock across the call, exactly
 * as they already do around TodoistTaskCache mutations made directly.
 */
namespace organizerActions {

// Drops the task at `cacheIndex` and queues it for the next sync. No-op for
// an out-of-range index.
void completeTask(size_t cacheIndex);

// Sets the task at `cacheIndex`'s due date to `newDueDays` (packed days since
// 2000-01-01, see TodoistDate.h) immediately, and queues the change for the
// next sync. No-op for an out-of-range index. Does not credit the companion -
// a reschedule is not progress the way a completion is.
void rescheduleTask(size_t cacheIndex, uint16_t newDueDays);

// The end time for a focus session starting now and running `durationMinutes`,
// as (endAbsMinutes: a UTC day-number*1440 + minute-of-day, comparable across
// a reboot with no calendar bookkeeping) and (endHourUtc/endMinuteUtc: the
// same moment as a UTC hour/minute, for display -- see
// HalClock::formatHourMinute()). Returns false, leaving the out-params
// untouched, if there is no valid clock to time the session against.
bool computeFocusSessionEnd(int durationMinutes, int32_t& endAbsMinutes, uint8_t& endHourUtc, uint8_t& endMinuteUtc);

// Starts a locked focus session for (text, itemId) running
// `durationMinutes` from now, replacing the current activity with
// FocusSessionActivity. No-op if there is no valid clock to time it against
// (see computeFocusSessionEnd()) -- the caller's screen is simply left as is.
void beginFocusSession(const std::string& text, const std::string& itemId, int durationMinutes, GfxRenderer& renderer,
                       MappedInputManager& mappedInput);

// Durations a focus session can be started for, in minutes -- index-matched
// with focusSessionDurationOptions()'s labels below.
inline constexpr int FOCUS_SESSION_DURATIONS_MINUTES[] = {5, 10, 20, 40};
inline constexpr int FOCUS_SESSION_DURATIONS_COUNT =
    sizeof(FOCUS_SESSION_DURATIONS_MINUTES) / sizeof(FOCUS_SESSION_DURATIONS_MINUTES[0]);

// "10 min" / "20 min" / "40 min", translated -- the options every "Focus
// session" entry point offers via an OptionsMenuActivity.
std::vector<std::string> focusSessionDurationOptions();

}  // namespace organizerActions
