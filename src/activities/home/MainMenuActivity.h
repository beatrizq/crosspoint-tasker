#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * The app's main screen: the status bar (the theme's clock/date/battery row) and
 * two large tiles stacked vertically -- Reader and Planner. Reader opens the Read menu;
 * Planner opens the Companion screen (QuickPickActivity).
 *
 * Focus is one continuous loop of three stops -- the status bar, Read, Planner --
 * wrapping at both ends (Left2/side Down forward, Left1/side Up the reverse).
 * Right2 acts on the focused stop: Sync All on the status bar, otherwise opens the
 * tile. Right1 is Settings from anywhere on this screen, since there is nowhere
 * left to "go back" to -- this screen is Home.
 *
 * The tile artwork is the 80px Read and Tasks line icons the old home grid used,
 * drawn larger: the bitmaps are scaled up with bilinear sampling and thresholded
 * back to one bit (see drawScaledIcon() in the .cpp) rather than doubled pixel for
 * pixel, which would show every stair-step of the original.
 */
class MainMenuActivity final : public Activity {
 public:
  explicit MainMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MainMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  // The stops of the focus loop, in visual top-to-bottom order.
  enum class Stop : uint8_t { StatusBar, Read, Planner };
  static constexpr int STOP_COUNT = 3;

  void activateFocused();
  // Geometry shared by render() and the touch hit-testing in loop(), so the two
  // can never disagree about where a tile is.
  struct TileLayout {
    int top;     // y of the first tile's band
    int height;  // each tile's band height; the second starts at top + height
  };
  TileLayout tileLayout() const;

  ButtonNavigator buttonNavigator;
  Stop focus = Stop::Read;
  // Home can be entered while Right1 is still held (e.g. leaving Settings with
  // it): ignore that stale release until a fresh press is seen here.
  bool settingsPressSeen = false;
  // Right2 can likewise still be down from the press that opened this screen.
  bool swallowConfirmRelease = false;
};
