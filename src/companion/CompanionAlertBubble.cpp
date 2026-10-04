#include "CompanionAlertBubble.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include "network/BleNotificationQueue.h"

const BleNotificationEntry* CompanionAlertBubble::current() {
  const uint8_t unread = BLE_NOTIFICATIONS.getUnreadCount();
  if (unread == 0) {
    // Nothing left, whether because everything here was dismissed down to
    // zero or because the Alerts screen itself cleared it -- reset so a
    // future alert starts fresh rather than staying stuck behind a stale
    // count.
    dismissedCount = 0;
    lastSeenUnread = 0;
    return nullptr;
  }
  if (unread > lastSeenUnread) {
    // At least one new alert arrived since this was last checked -- it
    // always takes over the bubble, ahead of anything already dismissed.
    dismissedCount = 0;
  }
  lastSeenUnread = unread;
  if (dismissedCount >= unread) return nullptr;
  return &BLE_NOTIFICATIONS.getEntry(dismissedCount);
}

void CompanionAlertBubble::dismiss() {
  if (dismissedCount < BLE_NOTIFICATIONS.getUnreadCount()) dismissedCount++;
}

#endif  // ENABLE_BLE_NOTIFY_SPIKE
