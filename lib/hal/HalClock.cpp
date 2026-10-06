#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

namespace {
// Floor a synced system clock could plausibly report; the un-synced default
// (seconds since boot, counted from the Unix epoch) reads as 1970 and is
// always well below this, so it is a reliable "has this actually been set"
// check without depending on SNTP's own sync-status flag (see the comment on
// getUtcDateTime() for why that flag cannot be trusted here).
constexpr int MIN_PLAUSIBLE_YEAR = 2024;
}  // namespace

HalClock halClock;  // Singleton instance

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (_available) {
    const unsigned long now = millis();
    if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
      hour = _cachedHour;
      minute = _cachedMinute;
      return true;
    }

    Rtc::DateTime dt;
    if (_sdkRtc.now(dt)) {
      _cachedHour = dt.hour;
      _cachedMinute = dt.minute;
      _lastPollMs = now;
      _hasCachedTime = true;
      hour = _cachedHour;
      minute = _cachedMinute;
      return true;
    }
    if (_hasCachedTime) {
      _lastPollMs = now;
      hour = _cachedHour;
      minute = _cachedMinute;
      return true;
    }
    return false;
  }

  // Most boards (X3/X4 included) have no hardware RTC, so this is the status
  // bar clock's only source there. Same system clock and same plausible-year
  // check as getUtcDateTime() -- see its comment for why. No I2C involved, so
  // none of the poll-interval caching above is needed here.
  const time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  if (timeinfo.tm_year + 1900 < MIN_PLAUSIBLE_YEAR) return false;
  hour = static_cast<uint8_t>(timeinfo.tm_hour);
  minute = static_cast<uint8_t>(timeinfo.tm_min);
  return true;
}

bool HalClock::getUtcDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const {
  if (_available) {
    Rtc::DateTime dt;
    if (_sdkRtc.now(dt)) {
      year = dt.year;
      month = dt.month;
      day = dt.day;
      hour = dt.hour;
      minute = dt.minute;
      return true;
    }
  }

  // Most boards (X3/X4 included) have no battery-backed RTC chip, so
  // _available is false and the branch above never runs there. The system
  // clock -- set once via SNTP, either by syncFromNTP() or as a side effect
  // of any planner sync (see PlannerSync::resolveTodayDate()) -- is the
  // only UTC source those boards have. sntp_get_sync_status() is NOT used to
  // gate this: it lives in normal RAM, which a deep-sleep wake reruns setup()
  // over and resets to "unsynced", even though the RTC-backed time offset
  // ESP-IDF maintains underneath time() survives that same wake untouched. A
  // plausible year is what actually distinguishes "really set at some point"
  // from the un-synced post-boot default of 1970.
  const time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  const int y = timeinfo.tm_year + 1900;
  if (y < MIN_PLAUSIBLE_YEAR) return false;

  year = static_cast<uint16_t>(y);
  month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
  day = static_cast<uint8_t>(timeinfo.tm_mday);
  hour = static_cast<uint8_t>(timeinfo.tm_hour);
  minute = static_cast<uint8_t>(timeinfo.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  uint8_t h, m;
  if (!getTime(h, m)) return false;
  return formatHourMinute(buf, bufSize, h, m, utcOffsetQuarterHoursBiased, use12Hour);
}

bool HalClock::formatHourMinute(char* buf, size_t bufSize, const uint8_t hour, const uint8_t minute,
                                uint8_t utcOffsetQuarterHoursBiased, const bool use12Hour) {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(hour) * 60 + static_cast<int>(minute) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now = time(nullptr);
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);

      if (!_available) {
        // No hardware RTC to write: configTzTime() above already set the
        // system clock, which is the only UTC source getTime()/getUtcDateTime()
        // read from on these boards (see their fallback comment).
        LOG_INF("CLK", "System clock synced to %04u-%02u-%02u %02u:%02u:%02u UTC", timeinfo.tm_year + 1900,
                timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
      }

      Rtc::DateTime dt;
      dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
      dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
      dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
      dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
      dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
      dt.second = static_cast<uint8_t>(timeinfo.tm_sec);
      dt.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
      if (_sdkRtc.set(dt)) {
        _lastPollMs = 0;
        _cachedHour = dt.hour;
        _cachedMinute = dt.minute;
        _hasCachedTime = true;
        LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                dt.second);
        return true;
      }
      return false;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
