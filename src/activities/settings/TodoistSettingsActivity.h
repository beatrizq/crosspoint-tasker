#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings submenu for the Todoist integration: enter or clear the API token,
 * plus the hint for where syncing happens (the Today screen, not here).
 */
class TodoistSettingsActivity final : public Activity {
 public:
  explicit TodoistSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TodoistSettings", renderer, mappedInput) {}

  // Nickname, API Token, Filter 1, Filter 2, Clear Token, and the non-interactive
  // sync hint row.
  static constexpr int MENU_ITEMS = 6;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void handleSelection();
  // Puts the wallpaper and the sleep screen mode back as they were before the

  // SettingsActivity dispatches this screen on Confirm going down, so that
  // same press can still be seen here once this screen is already active.
  // Unswallowed, it reads as a fresh Confirm-press on row 0 (Nickname) and
  // opens the keyboard before the user has touched anything. Armed in
  // onEnter() only when Confirm is still physically down at that point, and
  // cleared once it is released.
  bool swallowConfirmRelease = false;
};
