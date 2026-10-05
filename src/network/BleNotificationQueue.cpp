#include "BleNotificationQueue.h"

#ifdef ENABLE_BLE_NOTIFY_SPIKE

#include <algorithm>
#include <cstring>

namespace {
// Sender/title/content are drawn as plain single-line text (see
// BleNotificationsActivity's list row and BleNotificationDetailActivity's own
// rendering) -- a real Android notification body can legitimately contain a
// literal control character (a multi-line message preview's embedded '\n'),
// which has no glyph in any of this firmware's fonts. GfxRenderer's codepoint
// loop then logs "No glyph for codepoint N" and silently skips it for every
// one in the string -- harmless, but noisy, and the newline doesn't start a
// new line in a single-line drawer either, so it may as well not be there.
// Replacing every C0 control byte (< ' ') with a plain space here, once, at
// the point a notification enters the queue, keeps every future read clean
// without each display needing its own sanitizing pass. UTF-8-safe: every
// continuation/lead byte of a multi-byte sequence is >= 0x80, so this can
// never touch the middle of one.
void sanitizeControlChars(char* text) {
  for (char* p = text; *p != '\0'; p++) {
    if (static_cast<unsigned char>(*p) < ' ') *p = ' ';
  }
}
}  // namespace

void BleNotificationQueue::push(const uint32_t id, const bool isCall, const char* sender, const char* title,
                                const char* content, const uint8_t hour, const uint8_t minute) {
  // Sanitized up front, before the dedup check below as well as storage:
  // dedup compares these against already-stored (and thus already-sanitized)
  // entries, so comparing against the raw strings here would never match a
  // resend of the same notification whenever it contains a control character.
  BleNotificationEntry incoming{};
  strlcpy(incoming.sender, sender != nullptr ? sender : "", sizeof(incoming.sender));
  strlcpy(incoming.title, title != nullptr ? title : "", sizeof(incoming.title));
  strlcpy(incoming.content, content != nullptr ? content : "", sizeof(incoming.content));
  sanitizeControlChars(incoming.sender);
  sanitizeControlChars(incoming.title);
  sanitizeControlChars(incoming.content);

  // A BLE reconnect can resend every notification still active on the phone,
  // not just what arrived since the last connection (Gadgetbridge itself has
  // no "already sent to this device" tracking) -- skip one already held
  // rather than showing the same phone notification twice. id 0 is calls'
  // own sentinel (see BleNotificationEntry's own field comment), never a
  // real dedup key, and only entries still in the visible window (fill, not
  // the full ring) count: one that already aged out is shown again as new.
  //
  // The id check alone isn't reliable enough in practice -- with frequent
  // reconnects, some resends have shown up as multiple (up to 4) visible
  // copies, meaning Gadgetbridge doesn't always reassign the same id to the
  // same logical notification across a resync replay. Falls back to matching
  // on sender+title+content: a real notification's own content, not id, is
  // what actually identifies it as "the same one" to the reader, and this
  // catches an id-mismatched resend the first check misses. Content still
  // matches this way even for the (rare) two different real notifications
  // with the same sender/title/content -- a false-positive dedup there is a
  // fine trade against the far more common few-times-over duplicate.
  if (!isCall) {
    for (size_t i = 0; i < fill; i++) {
      const BleNotificationEntry& existing = getEntry(i);
      if (existing.isCall) continue;
      if (id != 0 && existing.id == id) return;
      if (strcmp(existing.sender, incoming.sender) == 0 && strcmp(existing.title, incoming.title) == 0 &&
          strcmp(existing.content, incoming.content) == 0) {
        return;
      }
    }
  }

  // The slot being reused no longer holds what was dismissed there.
  dismissedMask = static_cast<uint8_t>(dismissedMask & ~(1u << pos));
  BleNotificationEntry& e = entries[pos];
  e.id = id;
  e.isCall = isCall;
  strlcpy(e.sender, incoming.sender, sizeof(e.sender));
  strlcpy(e.title, incoming.title, sizeof(e.title));
  strlcpy(e.content, incoming.content, sizeof(e.content));
  e.hour = hour;
  e.minute = minute;

  pos = static_cast<uint8_t>((pos + 1) % CAPACITY);
  if (fill < CAPACITY) fill++;
  if (unreadCount < CAPACITY) unreadCount++;
}

const BleNotificationEntry& BleNotificationQueue::getEntry(const size_t indexFromNewest) const {
  return entries[slotFromNewest(indexFromNewest)];
}

size_t BleNotificationQueue::getPendingCount() const {
  size_t count = 0;
  for (size_t i = 0; i < unreadCount; i++) {
    if ((dismissedMask & (1u << slotFromNewest(i))) == 0) count++;
  }
  return count;
}

const BleNotificationEntry* BleNotificationQueue::getPendingEntry(const size_t k) const {
  size_t seen = 0;
  for (size_t i = 0; i < unreadCount; i++) {
    const uint8_t slot = slotFromNewest(i);
    if ((dismissedMask & (1u << slot)) != 0) continue;
    if (seen == k) return &entries[slot];
    seen++;
  }
  return nullptr;
}

void BleNotificationQueue::dismissPending(const size_t k) {
  size_t seen = 0;
  for (size_t i = 0; i < unreadCount; i++) {
    const uint8_t slot = slotFromNewest(i);
    if ((dismissedMask & (1u << slot)) != 0) continue;
    if (seen == k) {
      dismissedMask = static_cast<uint8_t>(dismissedMask | (1u << slot));
      saveToFile();
      return;
    }
    seen++;
  }
}

void BleNotificationQueue::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (uint8_t i = 0; i < CAPACITY; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = entries[i].id;
    o["isCall"] = entries[i].isCall;
    o["sender"] = entries[i].sender;
    o["title"] = entries[i].title;
    o["content"] = entries[i].content;
    o["hour"] = entries[i].hour;
    o["minute"] = entries[i].minute;
  }
  doc["pos"] = pos;
  doc["fill"] = fill;
  doc["unreadCount"] = unreadCount;
  doc["dismissedMask"] = dismissedMask;
}

bool BleNotificationQueue::fromJson(JsonVariantConst doc) {
  JsonArrayConst arr = doc["entries"];
  const size_t actualCount =
      arr.isNull() ? 0 : std::min(static_cast<size_t>(arr.size()), static_cast<size_t>(CAPACITY));
  for (size_t i = 0; i < actualCount; i++) {
    JsonObjectConst o = arr[i];
    entries[i].id = o["id"] | static_cast<uint32_t>(0);
    entries[i].isCall = o["isCall"] | false;
    strlcpy(entries[i].sender, o["sender"] | "", sizeof(entries[i].sender));
    strlcpy(entries[i].title, o["title"] | "", sizeof(entries[i].title));
    strlcpy(entries[i].content, o["content"] | "", sizeof(entries[i].content));
    entries[i].hour = o["hour"] | static_cast<uint8_t>(0);
    entries[i].minute = o["minute"] | static_cast<uint8_t>(0);
  }
  for (size_t i = actualCount; i < CAPACITY; i++) entries[i] = BleNotificationEntry{};

  pos = doc["pos"] | static_cast<uint8_t>(0);
  if (pos >= CAPACITY) pos = 0;
  fill = doc["fill"] | static_cast<uint8_t>(0);
  fill = static_cast<uint8_t>(std::min(static_cast<int>(fill), static_cast<int>(actualCount)));
  unreadCount = doc["unreadCount"] | static_cast<uint8_t>(0);
  unreadCount = std::min(unreadCount, fill);
  dismissedMask = doc["dismissedMask"] | static_cast<uint8_t>(0);
  return true;
}

#endif  // ENABLE_BLE_NOTIFY_SPIKE
