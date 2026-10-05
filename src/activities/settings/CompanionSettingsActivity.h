#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the organizing companion: its nickname, mood display,
 * and mood-ladder thresholds. Reached from the Organizer tab like the other
 * integrations (Todoist, Google Calendar), even though there is nothing to
 * sync here -- everything it controls is local, not an account to connect.
 *
 * The screen itself is Home (see ActivityManager::goHome()), so it is always
 * there; "Show companion" only hides the figure and its speech bubble on it.
 */
class CompanionSettingsActivity final : public Activity {
 public:
  explicit CompanionSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CompanionSettings", renderer, mappedInput) {}

  // Show companion, Nickname, Sleep start, Sleep end, Amazed at, Happy at,
  // Satisfied at, Neglected after, Reset.
  static constexpr int MENU_ITEMS = 9;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  // Opens the numeric picker for one of the mood-ladder thresholds
  // (selectedIndex identifies which). The picker's own min/max already
  // reflect the current value of the field it is paired with (happyPoints
  // must stay above satisfiedPoints and below amazedPoints; all and
  // neglectedDays must stay >= 1),
  // so the result is saved as-is -- see CrossPointSettings.h's comment on
  // companionHappyPoints for why that pairing exists.
  void offerThresholdPicker(int selectedIndex);
  // Stamps CompanionState::activatedDay the first time this is called with
  // no activation ever recorded -- whether that is because the companion
  // was just reset, or because it was already active from before this field
  // existed. No-op once activatedDay is already set, or when the clock has
  // no reading yet.
  void stampActivationIfNeeded();

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  int selectedIndex = 0;

  // SettingsActivity dispatches this screen on Confirm going down, so the
  // matching release lands here instead, once this screen is already active.
  // Unswallowed, it reads as a fresh Confirm-release on row 0 (Nickname) and
  // opens the nickname editor before the user has touched anything. Armed in
  // onEnter() only when Confirm is still physically down at that point.
  bool swallowConfirmRelease = false;
  // The Reset row's confirmation popup (and the Sleep start/end and
  // threshold sub-screens) answer on the button going down, so a Confirm/
  // Back release can still land back here once this screen is active again.
  // Same idea as swallowConfirmRelease above, armed instead from each of
  // those own result handlers.
  bool swallowBackRelease = false;
};
