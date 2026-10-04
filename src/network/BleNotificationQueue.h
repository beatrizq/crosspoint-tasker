#pragma once

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <cstdint>

// One relayed notification or call, as shown in the queue: "title: content" on
// its own line, sender as a subordinate (dimmed) line under it, sourced from
// Gadgetbridge's own src/title/body fields for a notification.
struct BleNotificationEntry {
  uint32_t id = 0;       // Gadgetbridge's own notification id, for de-duplicating a resend on reconnect. 0 for calls.
  bool isCall = false;   // true: an incoming call (sender/title/content below hold call fields). false: a notification.
  char sender[40] = {};  // Notification: src (app name, e.g. "WhatsApp"). Call: tr(STR_BLE_INCOMING_CALL).
  char title[64] = {};   // Notification: title. Call: caller name, or the number if no name was sent.
  char content[96] = {};  // Notification: body (message text). Call: number, or "" when title already holds it.
  uint8_t hour = 0;       // UTC hour this device received it (see HalClock::formatHourMinute for display).
  uint8_t minute = 0;     // UTC minute this device received it.
};

/**
 * Last few BLE notifications/calls relayed by Gadgetbridge, for a quick glance
 * without picking up the phone -- see BleNotifyRelay for where these arrive.
 *
 * A fixed-size ring buffer (oldest overwritten first), not an unbounded list:
 * this is a glance queue, not a synced notification center. Deliberately does
 * not track Gadgetbridge's "notify-" (remote dismiss) messages -- removing an
 * arbitrary entry from a ring buffer means shifting every entry after it, and
 * a stale entry here simply ages out on its own once CAPACITY more arrive.
 *
 * Persisted like every other Home-badge data source (TodoistTaskCache,
 * GCalEventCache, ...): SyncAllActivity reboots the device on exit
 * whenever WiFi was activated, so an in-RAM-only queue (and its unread badge)
 * would silently reset on every sync otherwise.
 */
class BleNotificationQueue : public PersistableStore<BleNotificationQueue> {
  BleNotificationQueue() = default;
  ~BleNotificationQueue() = default;

  friend class PersistableStore<BleNotificationQueue>;

 public:
  static constexpr uint8_t CAPACITY = 8;

  static const char* getFilePath() { return "/.crosspoint/ble_notifications.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Adds one entry, overwriting the oldest once CAPACITY is reached, and
  // counts it unread. sender/title/content are copied (truncated to the field
  // sizes above), so the caller's buffers need not outlive the call. A no-op
  // if id already matches an entry still held (see id's own field comment):
  // a Bangle.js-protocol reconnect can resend everything still active on the
  // phone, not just what arrived since the last connection, and this is the
  // only guard against the same phone notification showing twice.
  void push(uint32_t id, bool isCall, const char* sender, const char* title, const char* content, uint8_t hour,
            uint8_t minute);

  size_t getCount() const { return fill; }
  // 0 = most recently received. Caller must keep index < getCount().
  const BleNotificationEntry& getEntry(size_t indexFromNewest) const;

  uint8_t getUnreadCount() const { return unreadCount; }
  void markAllRead() { unreadCount = 0; }

  // Dismiss: empties the queue and clears the badge. The entries themselves
  // are left as-is (only the counters reset) -- harmless, since every read
  // path is bounded by fill/pos, and the next push() overwrites them anyway.
  void clearAll() {
    pos = 0;
    fill = 0;
    unreadCount = 0;
  }

 private:
  BleNotificationEntry entries[CAPACITY] = {};
  uint8_t pos = 0;          // next write slot
  uint8_t fill = 0;         // valid entries (0..CAPACITY)
  uint8_t unreadCount = 0;  // entries added (or persisted) since the last markAllRead()
};

#define BLE_NOTIFICATIONS BleNotificationQueue::getInstance()

#endif  // ENABLE_BLE_NOTIFY_SPIKE
