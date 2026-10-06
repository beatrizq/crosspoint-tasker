#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "GCalEvent.h"

namespace freeink {
class SecureHttpClient;
}

/**
 * HTTPS client for the Google Calendar API v3.
 *
 * Endpoints used:
 *   GET /calendar/v3/users/me/calendarList          - calendars to choose from
 *   GET /calendar/v3/calendars/{id}/events          - the Calendar screen's list
 *
 * Authentication: Authorization: Bearer <access token from GCalAuth>
 *
 * Both requests carry a `fields=` mask so Google returns only what the list
 * draws. An unmasked events reply carries descriptions, attendees, reminders,
 * conferencing and HTML links, which is many times the payload for data this
 * screen discards - and every byte of it would cross a TLS session on a device
 * with ~90KB of free heap.
 */
class GCalClient {
 public:
  enum Error {
    OK = 0,
    NO_TOKEN,
    NETWORK_ERROR,
    AUTH_FAILED,  // 401/403: access token rejected, caller should re-link
    SERVER_ERROR,
    PARSE_ERROR,
    LOW_MEMORY,
  };

  struct CalendarInfo {
    std::string id;
    std::string summary;
    bool primary = false;
  };

  // A reader cannot usefully show more calendars than this in a picker, and the
  // cap bounds the fetch.
  static constexpr size_t MAX_CALENDARS_LISTED = 32;

  /** Lists the calendars on the linked account, for the selection screen. */
  static Error fetchCalendars(const std::string& accessToken, std::vector<CalendarInfo>& outCalendars);

  /**
   * Fetches events for one calendar between two packed dates (inclusive of
   * fromDate, exclusive of the day after toDate).
   *
   * singleEvents=true has Google expand recurring events into individual
   * instances server-side, so nothing here interprets RRULE, EXDATE or
   * RECURRENCE-ID. orderBy=startTime gives them already sorted.
   *
   * Appends to outEvents rather than clearing it, so several calendars can be
   * merged into one list; stops at GCAL_MAX_EVENTS.
   *
   * `http`: caller-owned connection, shared across a sync's per-calendar calls
   * so SecureHttpClient's own keep-alive can actually take effect (see
   * plannerSync::runCalendar()) - this function neither constructs nor ends
   * it.
   */
  static Error fetchEvents(freeink::SecureHttpClient& http, const std::string& accessToken,
                           const std::string& calendarId, uint16_t fromDate, uint16_t toDate,
                           std::vector<GCalEvent>& outEvents);

  /** Diagnostic message for logs. User-facing text is translated by the caller. */
  static const char* errorString(Error error);

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
};
