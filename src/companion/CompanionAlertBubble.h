#pragma once

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <cstddef>
#include <cstdint>

struct BleNotificationEntry;

/**
 * @brief Which of BLE_NOTIFICATIONS' entries the companion's bubble is
 * currently showing, if any.
 *
 * Plain RAM-only singleton, like CompanionTracker -- the bubble's own "which
 * one, and how many have been dismissed from here" state is soft and
 * per-session, not worth persisting.
 *
 * The bubble always shows the newest alert BLE_NOTIFICATIONS hasn't been
 * dismissed from here (see current()); dismiss() steps to the next-newest,
 * and so on until none are left. A brand new alert arriving always takes
 * over again, ahead of anything already dismissed -- see current()'s own
 * "unread count went up" check. Visiting the dedicated Alerts screen clears
 * everything from here too, for free: that screen's own onEnter() calls
 * BLE_NOTIFICATIONS.markAllRead(), and current() treats an unread count of 0
 * the same as "nothing left to show".
 */
class CompanionAlertBubble {
 public:
  static CompanionAlertBubble& getInstance() {
    static CompanionAlertBubble instance;
    return instance;
  }

  CompanionAlertBubble(const CompanionAlertBubble&) = delete;
  CompanionAlertBubble& operator=(const CompanionAlertBubble&) = delete;

  // The alert the bubble should show right now, or nullptr when there is
  // none left to show. Safe to call every render -- idempotent as long as
  // BLE_NOTIFICATIONS' own unread count hasn't changed since the last call.
  const BleNotificationEntry* current();

  // Steps to the next-newest undismissed alert (or to none, once the last
  // one is dismissed). A no-op when current() is already nullptr.
  void dismiss();

 private:
  CompanionAlertBubble() = default;

  size_t dismissedCount = 0;
  uint8_t lastSeenUnread = 0;
};

#define ALERT_BUBBLE CompanionAlertBubble::getInstance()

#endif  // ENABLE_BLE_NOTIFY_SPIKE
