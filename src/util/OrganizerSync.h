#pragma once

#include <cstdint>
#include <string>

/**
 * The network half of each organizer screen's sync, callable on its own.
 *
 * Each of Tasks and Calendar used to own its whole sync: the
 * requests, applying the result to its cache, and the radio either side. That
 * left no way to sync more than one at a time, and the home screen's "sync
 * everything" needs exactly that - two services over one Wi-Fi association
 * rather than two.
 *
 * So the request-and-apply part lives here and the radio does not. Bringing the
 * association up and taking it down belongs to the caller, which is what lets a
 * caller syncing both pay for it once. Nothing here renders, holds screen
 * state, or reports progress; the caller owns all of that.
 *
 * These block for as long as the requests take - tens of seconds over a slow
 * link - and reset the task watchdog as they go, exactly as the per-screen syncs
 * did. Call them from the main task with the radio already up.
 */
namespace organizerSync {

enum class Service : uint8_t {
  Tasks = 0,
  Calendar = 1,
};

constexpr int SERVICE_COUNT = 2;

inline Service serviceAt(const int index) { return static_cast<Service>(index); }

/** The service's name, translated, for a progress row. */
const char* name(Service service);

/**
 * Whether the service has enough set up to be worth a request.
 *
 * A caller syncing everything skips what is not configured rather than
 * reporting it as a failure: an unconfigured integration is not a broken one.
 */
bool isConfigured(Service service);

/**
 * Runs the service's requests and applies the result to its cache, saving it.
 *
 * Returns nullptr on success, or a translated reason on failure. The cache is
 * mutated under a RenderLock, so a paint cannot catch it half-updated.
 *
 * Does not touch the radio, and does not snapshot the sleep screen - that one
 * belongs to the Tasks screen, which is the only caller with the task list in
 * its framebuffer to snapshot.
 */
const char* run(Service service);

/**
 * Local "YYYY-MM-DD" for an already-known UTC y/m/d/h/m reading, using the
 * same offset math resolveTodayDate() applies to a fresh NTP result -- but
 * without doing any NTP call of its own. Used at boot, before any network
 * activity, to decide whether a cache's stale daily completion log should be
 * cleared without forcing a sync. Returns "" if the offset produces an
 * out-of-range date (matching daylessness elsewhere in this file).
 */
std::string localIsoDateFromUtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

/**
 * Today, packed, in local time -- civil::NO_DATE if the clock isn't usable
 * yet (no hardware RTC and never NTP-synced this power session). The same
 * halClock::getUtcDateTime() + localIsoDateFromUtc() + civil::dateFromIso()
 * sequence CompanionTracker::resolveLocalDayAndMinute() and main.cpp's own
 * boot-time check already spell out by hand, for a caller that just needs
 * today's date and none of the rollover side effects those two also trigger.
 */
uint16_t todayLocalDate();

}  // namespace organizerSync
