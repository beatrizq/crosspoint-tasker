#pragma once
#include <ArduinoJson.h>
#include <CompanionMood.h>
#include <PersistableStore.h>

#include <cstdint>

/**
 * @brief Persisted companion progress: the day ledger plus lifetime totals.
 *
 * Only earned data lives here. Which companion is active and whether the
 * feature is on at all are user preferences and live in CrossPointSettings,
 * mirroring the CrossPointSettings/CrossPointState split.
 *
 * All day-rollover and streak logic is in lib/Companion so it stays host
 * testable; this class is the JSON envelope around it.
 */
class CompanionState : public PersistableStore<CompanionState> {
  CompanionState() = default;

  friend class PersistableStore<CompanionState>;

 public:
  companion::DayLedger ledger;
  // Local day number the companion was first ever enabled, for the settings
  // screen's "active for" display. companion::DayLedger::NEVER until then.
  // Stamped once and never moved, even if the companion is later disabled and
  // re-enabled -- it answers "how long has this device had a companion", not
  // "how long has it been on right now".
  int32_t activatedDay = companion::DayLedger::NEVER;

  static const char* getFilePath() { return "/.crosspoint/companion.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Credits today's completed tasks into the ledger. `localDay`
  // is ignored (no ledger update) when clockValid is false, since day
  // arithmetic would be meaningless without a real calendar day to key it to.
  // `thresholds` only matters for its satisfiedPoints -- the "qualifying day"
  // bar creditQualifyingDay() checks against -- so this stays in step with
  // whatever bar evaluate() is using for the same day; the caller is expected
  // to already have it clamped (see CompanionTracker::thresholdsFromSettings).
  // Returns true when something changed and the caller should persist.
  bool recordActivity(int32_t localDay, bool clockValid, uint16_t tasksCompletedToday,
                      const companion::MoodThresholds& thresholds = {});

  // Clears everything earned back to a companion that has never done
  // anything: the day-qualifying marker and the activation date -- an explicit override of
  // activatedDay's usual "stamped once, never moved" rule, for a user who
  // wants a clean slate (repeated testing, a device changing hands, etc.).
  // Caller's job to persist and to re-stamp activatedDay if the companion is
  // still enabled (see CompanionSettingsActivity::stampActivationIfNeeded).
  // Does not touch today's live task count -- that is read fresh from its
  // own cache, never stored here.
  void reset();
};

#define COMPANION_STATE CompanionState::getInstance()
