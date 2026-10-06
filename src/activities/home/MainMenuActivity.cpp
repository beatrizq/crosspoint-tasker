#include "MainMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/icons/read80.h"
#include "components/icons/tasks80.h"
#include "fontIds.h"

namespace {
// The status bar's own vertical extent, as QuickPickActivity lays it out (see
// its HEADER_CONTENT_HEIGHT / HEADER_HIGHLIGHT_HEIGHT): the theme's header rect
// is sized for a full title band this screen does not draw, so the layout below
// uses the height of just the clock/battery row.
constexpr int STATUS_BAR_HEIGHT = 36;
constexpr int STATUS_BAR_HIGHLIGHT_HEIGHT = STATUS_BAR_HEIGHT - 6;
// Between the status bar and the first tile.
constexpr int STATUS_BAR_GAP = 10;

// The tile artwork: the source bitmaps are 80x80, drawn at up to this size.
constexpr int SOURCE_ICON_SIZE = 80;
constexpr int MAX_ICON_SIZE = 160;
// The label sits this far under its icon, and the selection outline stands this
// far (times 4 on the sides, as the old home grid did) clear of the pair.
constexpr int LABEL_GAP = 12;
constexpr int SELECTION_PADDING = 8;
constexpr int SELECTION_LINE_WIDTH = 2;
constexpr int SELECTION_CORNER_RADIUS = 6;

// Draws the 80x80 1-bpp icon `bitmap` (MSB first, bit 0 = ink, the format
// GfxRenderer::drawIcon() takes) at `size` x `size` with its top-left at (x, y),
// using the same orientation mapping as drawIcon(). Each output pixel samples the
// source bilinearly (8-bit fixed point, no floats on this core) and is inked when
// at least half of the sample is ink, so the edges of the line art stay smooth
// instead of stepping at the scale factor.
void drawScaledIcon(const GfxRenderer& renderer, const uint8_t* bitmap, const int x, const int y, const int size) {
  constexpr int ROW_BYTES = (SOURCE_ICON_SIZE + 7) / 8;
  const auto inkAt = [bitmap](const int row, const int col) -> int {
    if (row < 0 || col < 0 || row >= SOURCE_ICON_SIZE || col >= SOURCE_ICON_SIZE) return 0;
    const uint8_t byte = bitmap[row * ROW_BYTES + (col >> 3)];
    return ((byte >> (7 - (col & 7))) & 1) == 0 ? 1 : 0;
  };

  // Source position of each output pixel's centre, in 1/256ths of a source pixel.
  const auto sourcePos = [size](const int out) -> int { return ((2 * out + 1) * SOURCE_ICON_SIZE * 128) / size - 128; };

  for (int row = 0; row < size; row++) {
    const int fy = sourcePos(row);
    const int y0 = fy >> 8;  // arithmetic shift: floors for the (small) negative edge
    const int wy1 = fy & 255;
    const int wy0 = 256 - wy1;
    for (int col = 0; col < size; col++) {
      const int fx = sourcePos(col);
      const int x0 = fx >> 8;
      const int wx1 = fx & 255;
      const int wx0 = 256 - wx1;
      const int weight = wy0 * (wx0 * inkAt(y0, x0) + wx1 * inkAt(y0, x0 + 1)) +
                         wy1 * (wx0 * inkAt(y0 + 1, x0) + wx1 * inkAt(y0 + 1, x0 + 1));
      // The four weights sum to 256 * 256 = 65536, reached when every neighbour is ink.
      if (weight >= 32768) renderer.drawPixel(x + (size - 1 - row), y + col, true);
    }
  }
}
}  // namespace

void MainMenuActivity::onEnter() {
  Activity::onEnter();
  focus = Stop::Read;
  settingsPressSeen = false;
  swallowConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Right2);
  requestUpdate();
}

MainMenuActivity::TileLayout MainMenuActivity::tileLayout() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + STATUS_BAR_HEIGHT + STATUS_BAR_GAP;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.buttonHintsGap;
  return TileLayout{top, std::max(0, (bottom - top) / 2)};
}

void MainMenuActivity::activateFocused() {
  switch (focus) {
    case Stop::StatusBar:
      // Returns here once the sync is done; goHome() is the one mechanism every
      // other screen's own Sync All return uses too.
      activityManager.goToSyncAll([] { activityManager.goHome(); });
      break;
    case Stop::Read:
      activityManager.goToReadMenu();
      break;
    case Stop::Planner:
      activityManager.goToCompanion();
      break;
  }
}

void MainMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    focus = static_cast<Stop>((static_cast<int>(focus) + 1) % STOP_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    focus = static_cast<Stop>((static_cast<int>(focus) + STOP_COUNT - 1) % STOP_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) settingsPressSeen = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  // Right1 is Settings from every stop -- there is nowhere to go back to from
  // Home. settingsPressSeen guards against the stale release of the press that
  // closed the previous screen.
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1) && settingsPressSeen) {
    activityManager.goToSettings();
    return;
  }

  // A tap on a tile selects and opens it.
  const TileLayout layout = tileLayout();
  const int width = renderer.getScreenWidth();
  if (mappedInput.wasTapInRect(0, layout.top, width, layout.height)) {
    focus = Stop::Read;
    activateFocused();
    return;
  }
  if (mappedInput.wasTapInRect(0, layout.top + layout.height, width, layout.height)) {
    focus = Stop::Planner;
    activateFocused();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    activateFocused();
  }
}

void MainMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  // The status bar is the theme's own clock/date/battery row: a null title and
  // subtitle drop the title band and leave just that row (see QuickPickActivity's
  // render() for the per-theme details).
  const Rect headerRect{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, /*title=*/nullptr, /*subtitle=*/nullptr, /*showRule=*/true);
  // Hovering it: a true pixel invert over just the clock/battery row.
  if (focus == Stop::StatusBar) {
    renderer.invertRect(headerRect.x, headerRect.y, headerRect.width, STATUS_BAR_HIGHLIGHT_HEIGHT);
  }

  const TileLayout layout = tileLayout();
  const int labelHeight = renderer.getLineHeight(UI_12_FONT_ID);
  // As large as the artwork goes, but never so large the icon, its label and the
  // selection outline around both do not fit the tile's band.
  const int fitIconSize = layout.height - LABEL_GAP - labelHeight - SELECTION_PADDING * 4;
  const int iconSize = std::max(SOURCE_ICON_SIZE, std::min(MAX_ICON_SIZE, fitIconSize));

  struct Tile {
    Stop stop;
    const uint8_t* icon;
    const char* label;
  };
  const Tile tiles[] = {
      {Stop::Read, Read80Icon, tr(STR_MENU_READER)},
      {Stop::Planner, Tasks80Icon, tr(STR_MENU_PLANNER)},
  };

  for (int i = 0; i < 2; i++) {
    const bool selected = focus == tiles[i].stop;
    const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, tiles[i].label, pageWidth - 32, style);
    const int labelWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), style);

    // The icon and its label, centred together in the tile's band.
    const int contentHeight = iconSize + LABEL_GAP + labelHeight;
    const int contentTop = layout.top + i * layout.height + (layout.height - contentHeight) / 2;

    if (selected) {
      // An outline, not a fill: the artwork is line work, and inverting a tile
      // this size is a lot of ink to move on every selection change.
      const int boxWidth = std::max(iconSize, labelWidth) + SELECTION_PADDING * 4;
      const int boxHeight = contentHeight + SELECTION_PADDING * 2;
      renderer.drawRoundedRect((pageWidth - boxWidth) / 2, contentTop - SELECTION_PADDING, boxWidth, boxHeight,
                               SELECTION_LINE_WIDTH, SELECTION_CORNER_RADIUS, true);
    }

    drawScaledIcon(renderer, tiles[i].icon, (pageWidth - iconSize) / 2, contentTop, iconSize);
    renderer.drawText(UI_12_FONT_ID, (pageWidth - labelWidth) / 2, contentTop + iconSize + LABEL_GAP, label.c_str(),
                      true, style);
  }

  // Right1 is Settings from every stop; Right2 is Sync All on the status bar and
  // Select on a tile.
  const auto labels =
      mappedInput.mapLabels(tr(STR_SETTINGS_TITLE), focus == Stop::StatusBar ? tr(STR_SYNC_ALL) : tr(STR_SELECT),
                            tr(STR_DIR_PREV), tr(STR_DIR_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
