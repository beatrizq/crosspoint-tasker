#include "HomeAppOrder.h"

#include <cstdio>

#include "CrossPointSettings.h"

namespace homeAppOrder {
namespace {

// Built-in order. Read leads because the cover card above it opens the last book
// and this opens everything else about books; the integrations follow.
constexpr AppInfo APPS[APP_COUNT] = {
    {AppId::Read, StrId::STR_MENU_READ, UIIcon::Book},
    {AppId::Tasks, StrId::STR_TODOIST, UIIcon::Tasks},
    {AppId::Calendar, StrId::STR_GOOGLE_CALENDAR, UIIcon::Calendar},
    {AppId::Notifications, StrId::STR_BLE_NOTIFICATIONS, UIIcon::Bell},
    // None: drawn dynamically instead (see AppId::Companion's own comment).
    {AppId::Companion, StrId::STR_COMPANION, UIIcon::None},
    {AppId::Settings, StrId::STR_SETTINGS_TITLE, UIIcon::Settings},
};

}  // namespace

const AppInfo& appAt(const int index) {
  if (index < 0 || index >= APP_COUNT) return APPS[0];
  return APPS[index];
}

char* nicknameField(const AppId id, size_t& outSize) {
  switch (id) {
    case AppId::Tasks:
      outSize = sizeof(SETTINGS.tasksNickname);
      return SETTINGS.tasksNickname;
    case AppId::Calendar:
      outSize = sizeof(SETTINGS.calendarNickname);
      return SETTINGS.calendarNickname;
    case AppId::Read:
      // Read is not an integration - it has no account, no settings screen of its
      // own, and so nothing to rename it from.
      break;
    case AppId::Notifications:
      // Same reasoning as Read: not an account, nothing to nickname.
      break;
    case AppId::Companion:
      outSize = sizeof(SETTINGS.companionNickname);
      return SETTINGS.companionNickname;
    case AppId::Settings:
      // Not an integration either - the Settings screen itself, nothing to
      // rename it from.
      break;
  }
  outSize = 0;
  return nullptr;
}

const char* displayName(const AppId id) {
  size_t size = 0;
  const char* nickname = nicknameField(id, size);
  if (nickname != nullptr && nickname[0] != '\0') return nickname;
  return I18N.get(APPS[static_cast<int>(id) < APP_COUNT ? static_cast<int>(id) : 0].appName);
}

void parse(const char* stored, int (&out)[APP_COUNT]) {
  bool placed[APP_COUNT] = {};
  int count = 0;

  if (stored != nullptr) {
    for (const char* c = stored; *c != '\0' && count < APP_COUNT; c++) {
      // Single digits, so no separator to skip and no number to accumulate. An
      // id past the end of the table is an app removed since this was written.
      if (*c < '0' || *c > '9') continue;
      const int index = *c - '0';
      if (index >= APP_COUNT) continue;
      if (placed[index]) continue;  // repeated id; the first wins
      out[count++] = index;
      placed[index] = true;
    }
  }

  // Whatever the string did not name goes on the end in built-in order. This is
  // what makes a new app appear for someone who already had an order stored,
  // rather than vanishing from their home screen.
  for (int i = 0; i < APP_COUNT && count < APP_COUNT; i++) {
    if (!placed[i]) out[count++] = i;
  }
}

void format(const int (&order)[APP_COUNT], char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  size_t written = 0;
  for (int i = 0; i < APP_COUNT && written + 1 < outSize; i++) {
    const int index = order[i];
    if (index < 0 || index >= APP_COUNT) continue;
    out[written++] = static_cast<char>('0' + index);
  }
  out[written] = '\0';
}

AppId adjacentVisibleApp(const AppId current, const bool forward) {
  int order[APP_COUNT];
  parse(SETTINGS.homeAppOrder, order);

  // Same visibility gates HomeActivity::buildEntries() applies -- an app that
  // is not really a selectable tile right now should not be a stop on this
  // cycle either.
  AppId visible[APP_COUNT];
  int count = 0;
  for (const int index : order) {
    const auto& app = appAt(index);
#ifndef ENABLE_BLE_NOTIFY_SPIKE
    if (app.id == AppId::Notifications) continue;
#endif
    // Never a grid tile any more (see HomeActivity::buildEntries()), so never
    // a stop on this cycle either.
    if (app.id == AppId::Settings) continue;
    visible[count++] = app.id;
  }
  if (count == 0) return current;

  int currentIndex = 0;
  for (int i = 0; i < count; i++) {
    if (visible[i] == current) {
      currentIndex = i;
      break;
    }
  }
  const int next = forward ? (currentIndex + 1) % count : (currentIndex + count - 1) % count;
  return visible[next];
}

}  // namespace homeAppOrder
