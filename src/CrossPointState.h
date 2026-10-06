#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  // Mirrors lastSleepFromReader for the companion's own screen, so waking
  // from sleep can reopen it instead of dropping to Home -- there is no RTTI
  // in this build to safely downcast the current Activity and check that
  // reactively at sleep time, so main.cpp's own isQuickPickActivity() check
  // stamps this here instead.
  bool lastSleepFromQuickPick = false;

  // A running focus session, mirrored on start (plannerActions::beginFocusSession)
  // and cleared once consumed (FocusSessionActivity, once the countdown
  // elapses or turns out to have nothing to time against) so a reboot mid-session
  // resumes the lock instead of losing it -- the whole point of the lock is that
  // it survives a reboot, not just a sleep/wake. Checked directly against the
  // wall clock at boot rather than via a lastSleepFromX flag like the quick-pick
  // fields above: a stale flag from a session that already finished needs telling
  // apart from a live one, and the end time itself already does that.
  bool focusSessionActive = false;
  std::string focusSessionText;
  std::string focusSessionItemId;
  // Absolute end time as (UTC day number * 1440) + minute-of-day, comparable
  // across a reboot without any calendar bookkeeping -- see
  // companion::localDayNumber() and plannerActions::computeFocusSessionEnd().
  int32_t focusSessionEndAbsMinutes = 0;
  // The same end time as a UTC hour/minute, kept only for display -- formatted
  // into the user's local time the way the status bar clock is (see
  // HalClock::formatHourMinute()).
  uint8_t focusSessionEndHour = 0;
  uint8_t focusSessionEndMinute = 0;

  // A running timed Focus/Break session (see CompanionSessionActivity, which
  // nothing starts any more -- these stay false) -- same resume story as
  // focusSessionActive above, kept as its own separate set of fields rather
  // than reusing those since this session has no task item attached,
  // just a fixed companion::Mood override.
  bool companionSessionActive = false;
  uint8_t companionSessionMood = 0;  // companion::Mood::Focus or ::Break, cast to uint8_t
  int32_t companionSessionEndAbsMinutes = 0;
  uint8_t companionSessionEndHour = 0;
  uint8_t companionSessionEndMinute = 0;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Returns true if idx was shown within the last checkCount picks.
  // Walks backwards from the most recently written slot.
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
};

// Helper macro to access state
#define APP_STATE CrossPointState::getInstance()
