#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/HomeAppOrder.h"

/**
 * Arranges the apps on the home grid.
 *
 * Select picks a row up, Up and Down walk it through the list, Select puts it
 * down. Pick-up-and-move rather than swap-two because "put Tasks first" should
 * be one gesture and not four, and rather than drag because a drag on e-ink
 * repaints the whole list for every pixel of travel.
 *
 * While a row is held the button hints change to say so - it is the only thing on
 * screen that distinguishes moving from browsing, since the row itself is drawn
 * the same way the selection always is.
 *
 * The order is written to CrossPointSettings on the way out rather than on every
 * move: SD writes are the expensive part and a move is several presses.
 */
class AppOrderActivity final : public Activity {
 public:
  explicit AppOrderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppOrder", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Moves the held row one place, carrying the selection with it. No-op at the
  // ends, so holding Down at the bottom does not silently wrap the app to the top.
  void moveHeld(int delta);
  // Carries the held row to `target` by walking it, so touch and buttons agree.
  void moveHeldTo(int target);

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  // True while a row is picked up, so Up/Down move it instead of the cursor.
  bool holding = false;
  bool dirty = false;
  // Guards against the Confirm press that opened this screen (from the
  // Settings row that leads here) still being down on entry: unswallowed,
  // its own release would read as a fresh Confirm here and immediately pick
  // up row 0, before the user ever meant to start moving anything. Armed in
  // onEnter() only when Confirm is still physically down at that point, same
  // pattern as CompanionSettingsActivity's own swallowConfirmRelease.
  bool swallowConfirmRelease = false;

  // Indices into the app table, in display order.
  int order[homeAppOrder::APP_COUNT] = {};
};
