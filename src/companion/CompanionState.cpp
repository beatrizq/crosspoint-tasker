#include "CompanionState.h"

void CompanionState::toJson(JsonDocument& doc) const {
  doc["lastQualifyingDay"] = ledger.lastQualifyingDay;
  doc["previousQualifyingDay"] = ledger.previousQualifyingDay;
  doc["activatedDay"] = activatedDay;
}

bool CompanionState::fromJson(JsonVariantConst doc) {
  ledger.lastQualifyingDay = doc["lastQualifyingDay"] | companion::DayLedger::NEVER;
  ledger.previousQualifyingDay = doc["previousQualifyingDay"] | companion::DayLedger::NEVER;
  activatedDay = doc["activatedDay"] | companion::DayLedger::NEVER;
  return true;
}

bool CompanionState::recordActivity(const int32_t localDay, const bool clockValid, const uint16_t tasksCompletedToday,
                                    const companion::MoodThresholds& thresholds) {
  if (!clockValid) return false;

  return companion::creditQualifyingDay(ledger, localDay, tasksCompletedToday, thresholds);
}

void CompanionState::reset() {
  ledger = companion::DayLedger{};
  activatedDay = companion::DayLedger::NEVER;
}
