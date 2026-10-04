#include "LyraTheme.h"

#include <CivilTime.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/organizer/OrganizerLabels.h"
#include "components/UITheme.h"
#include "components/icons/bell80.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/bookmark.h"
#include "components/icons/calendar.h"
#include "components/icons/calendar80.h"
#include "components/icons/cover.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/read80.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/settings80.h"
#include "components/icons/tasks.h"
#include "components/icons/tasks80.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
// Shared with the selection box drawButtonGrid draws around a selected tile,
// so a selected cover, companion, and app tile all read as the same gesture.
constexpr int selectionLineWidth = 2;
constexpr int topHintButtonY = 345;

// The side-button label boxes (drawSideButtonHints) and the geometry
// getSideButtonHintsBottom() needs to say where they end.
//
// minButtonHeight is a box's floor: a short label doesn't shrink it below this,
// and it is the length the physical button itself is taken to have. The
// physical buttons don't move -- on the X3 the original minButtonHeight-tall
// box started at y=155, so that is where its centre sits, and a taller box (a
// longer label) grows symmetrically around that centre rather than downward
// from 155, so the label stays centred on the button.
constexpr int minButtonHeight = 78;
constexpr int stackPadding = 8;       // Above and below the stacked letters, inside the border
constexpr int stackLineAdvance = 20;  // Letter-to-letter distance (the font's own line height is 23)
constexpr int x3ButtonCenterY = 155 + minButtonHeight / 2;

// A box grows to fit its label's stacked letters (one per line, see
// GfxRenderer::drawTextStacked) rather than staying a fixed height, so the
// border always encloses the whole label. An empty label falls back to the
// floor, which keeps the X4 layout where it was.
int sideButtonBoxHeight(const GfxRenderer& renderer, const char* label) {
  return std::max(minButtonHeight,
                  renderer.getTextStackedHeight(SMALL_FONT_ID, label, stackLineAdvance) + 2 * stackPadding);
}
constexpr int maxListValueWidth = 200;
constexpr int mainMenuIconSize = 32;
// The tile grid has a whole tile to fill, so its artwork is larger.
constexpr int homeGridIconSize = 80;
constexpr int listIconSize = 24;
constexpr int mainMenuColumns = 2;
// The header clock's own icon, same size as a list row's own icon
// (listIconSize) -- QuickPickActivity's glance strip icons match this size
// too, and the clock is meant to read as the same family of icon+text rows.
constexpr int headerClockIconSize = listIconSize;
constexpr int headerClockIconGap = 6;
// Battery icon to match this row's own bigger text (see the header clock's
// own comment) -- not LyraMetrics::values.battery*, which stays this
// theme's smaller, general-purpose default for whatever else might use it.
// Same 4:3-ish proportions as that default, scaled up.
constexpr int headerBatteryWidth = 22;
constexpr int headerBatteryHeight = 16;

// A small clock face -- circle (outline) + hour/minute hands -- drawn from
// plain line/rounded-rect primitives rather than a bitmap: there's no
// drawCircle on GfxRenderer, but a fixed-radius rounded square at this size
// reads as a circle, and a vector icon stays crisp at a size nothing in this
// theme's own icon set was generated at. A 2px line (not the default 1px)
// so it actually reads at a glance instead of near-vanishing against the
// icon+text rows around it.
void drawClockIcon(const GfxRenderer& renderer, const int x, const int y, const int size) {
  constexpr int lineWidth = 2;
  renderer.drawRoundedRect(x, y, size, size, lineWidth, size / 2, true);
  const int centreX = x + size / 2;
  const int centreY = y + size / 2;
  renderer.drawLine(centreX, centreY, centreX, centreY - size / 2 + 2, lineWidth, true);
  renderer.drawLine(centreX, centreY, centreX + size / 4, centreY, lineWidth, true);
}
int coverWidth = 0;

const uint8_t* iconForName(UIIcon icon, int size) {
  if (size == 80) {
    // The home grid's own size, drawn from Lucide line art (ISC, vendored in
    // freeink-sdk/libs/assets/Icons/lucide) rather than the upscaled 32px set:
    // a tile this size shows every jagged edge of a doubled bitmap.
    switch (icon) {
      case UIIcon::Book:
        return Read80Icon;
      case UIIcon::Tasks:
        return Tasks80Icon;
      case UIIcon::Calendar:
        return Calendar80Icon;
      case UIIcon::Bell:
        return Bell80Icon;
      case UIIcon::Settings:
        return Settings80Icon;
      default:
        return nullptr;
    }
  } else if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      default:
        return nullptr;
    }
  } else if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::Recent:
        return RecentIcon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Transfer:
        return TransferIcon;
      case UIIcon::Library:
        return LibraryIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      case UIIcon::Bookmark:
        return BookmarkIcon;
      case UIIcon::Tasks:
        return TasksIcon;
      case UIIcon::Calendar:
        return CalendarIcon;
      default:
        return nullptr;
    }
  }
  return nullptr;
}
}  // namespace

void LyraTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  if (charging) {
    // Solid fill when charging so lightning bolt is visible
    renderer.fillRect(rect.x + 2, rect.y + 2, rect.width - 5, rect.height - 4);
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(rect.x + 2, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(rect.x + 6, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(rect.x + 10, rect.y + 2, 3, rect.height - 4);
    }
  }
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                           const bool showRule, const bool includeStatusRow) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  // A second, embedded header drawn mid-screen (QuickPickActivity's own
  // scaled-down Tasks section below the companion figure) skips this whole
  // clock/date/battery row -- it already has one at the top of the screen,
  // and repeating it here would just duplicate that chrome.
  if (includeStatusRow) {
    // Drawn directly (not via drawBatteryRight(), which hardcodes SMALL_FONT_ID
    // for its own percentage text) so the percentage matches this row's own
    // bigger UI_10_FONT_ID, the same reasoning the clock text below already
    // gets -- and the icon itself is sized up to match (headerBatteryWidth/
    // Height, see its own comment).
    const bool showBatteryPercentage =
        SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
    const int batteryX = rect.x + rect.width - 12 - headerBatteryWidth;
    const int batteryTextY = rect.y + 5;
    if (showBatteryPercentage) {
      const auto percentageText = std::to_string(powerManager.getBatteryPercentage()) + "%";
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, percentageText.c_str());
      renderer.drawText(UI_10_FONT_ID, batteryX - textWidth - batteryPercentSpacing, batteryTextY,
                        percentageText.c_str());
    }
    // Centred against the percentage text's own line height, the same
    // convention the glance strip's bullet/text rows already use -- the old
    // fixed "+6" sat the icon visibly lower than the text next to it.
    const int batteryIconY = batteryTextY + (renderer.getLineHeight(UI_10_FONT_ID) - headerBatteryHeight) / 2;
    drawBatteryOutline(renderer, batteryX, batteryIconY, headerBatteryWidth, headerBatteryHeight);
    fillBatteryIcon(renderer, Rect{batteryX, batteryIconY, headerBatteryWidth, headerBatteryHeight},
                    powerManager.getBatteryPercentage());

    // Clock, mirroring the battery on the opposite corner. Silently absent when
    // there is no usable time yet (no hardware RTC and never NTP-synced this
    // power session) rather than showing a stale or garbage value. Today's date
    // rides alongside it, middle-dot separated (same glyph and spacing
    // QuickPickActivity's own age/highscore status line used to use), in the
    // same "Mon 17 Aug" format Tasks/Calendar already use for
    // their own header date (organizer::formatDayLabel) -- silently dropped
    // along with the time when the clock isn't usable yet, same as the time
    // itself. UI_10_FONT_ID and the clock icon match QuickPickActivity's own
    // glance-strip rows (icon + slightly larger text than the old SMALL_FONT_ID
    // reading), so this row reads as the same family across every screen that
    // calls drawHeader(), not just the companion screen.
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
      char headerClock[32];
      strlcpy(headerClock, timeBuf, sizeof(headerClock));

      uint16_t year = 0;
      uint8_t month = 0;
      uint8_t day = 0;
      uint8_t hour = 0;
      uint8_t minute = 0;
      if (halClock.getUtcDateTime(year, month, day, hour, minute)) {
        char dateBuf[16];
        organizer::formatDayLabel(civil::packDate(year, month, day), dateBuf, sizeof(dateBuf));
        snprintf(headerClock, sizeof(headerClock), "%s  \xC2\xB7  %s", timeBuf, dateBuf);
      }
      const int clockIconX = rect.x + LyraMetrics::values.contentSidePadding;
      drawClockIcon(renderer, clockIconX, rect.y + 5, headerClockIconSize);
      renderer.drawText(UI_10_FONT_ID, clockIconX + headerClockIconSize + headerClockIconGap, rect.y + 5, headerClock,
                        true);
    }
  }

  int maxTitleWidth = title != nullptr ? renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD) : 0;
  int maxSubtitleWidth =
      subtitle != nullptr ? renderer.getTextWidth(SMALL_FONT_ID, subtitle, EpdFontFamily::REGULAR) : 0;

  // Available space is the distance between the side paddings, and a with side padding between title and subtitle.
  const int availableSpace = rect.width - LyraMetrics::values.contentSidePadding * 3;

  if (maxTitleWidth + maxSubtitleWidth > availableSpace) {
    if ((maxTitleWidth > availableSpace / 2) && (maxSubtitleWidth > availableSpace / 2)) {
      // Both are wider then half the space, truncate both.
      maxTitleWidth = availableSpace / 2;
      maxSubtitleWidth = availableSpace / 2;
    } else {
      // Truncate the the longest one
      if (maxTitleWidth > maxSubtitleWidth) {
        maxTitleWidth = availableSpace - maxSubtitleWidth;
      } else {
        maxSubtitleWidth = availableSpace - maxTitleWidth;
      }
    }
  }

  if (title) {
    // No status row reserved above when includeStatusRow is false -- the
    // title sits near the top of `rect` instead of leaving that space blank.
    const int titleY = includeStatusRow ? rect.y + LyraMetrics::values.batteryBarHeight + 3 : rect.y + 3;
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, maxTitleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding, titleY, truncatedTitle.c_str(),
                      true, EpdFontFamily::BOLD);
    if (showRule) {
      renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width - 1, rect.y + rect.height - 3, 3, true);
    }
  }

  if (subtitle) {
    // Same +7 offset from the title's own baseline either way, so the two
    // stay on one visual row regardless of includeStatusRow -- matches the
    // original rect.y+43 (title) / rect.y+50 (subtitle) relationship.
    const int subtitleY = includeStatusRow ? rect.y + 50 : rect.y + 10;
    auto truncatedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxSubtitleWidth, EpdFontFamily::REGULAR);
    int truncatedSubtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedSubtitle.c_str());
    renderer.drawText(SMALL_FONT_ID,
                      rect.x + rect.width - LyraMetrics::values.contentSidePadding - truncatedSubtitleWidth, subtitleY,
                      truncatedSubtitle.c_str(), true);
  }
}

void LyraTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;
  int rightSpace = LyraMetrics::values.contentSidePadding;
  if (rightLabel) {
    auto truncatedRightLabel =
        renderer.truncatedText(SMALL_FONT_ID, rightLabel, maxListValueWidth, EpdFontFamily::REGULAR);
    int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - LyraMetrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 7, truncatedRightLabel.c_str());
    rightSpace += rightLabelWidth + hPaddingInSelection;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_10_FONT_ID, label, rect.width - LyraMetrics::values.contentSidePadding - rightSpace, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, truncatedLabel.c_str(), true, EpdFontFamily::REGULAR);

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

namespace {
// Advance of one Lyra tab: the label, the pill padding either side, and the gap
// that follows it.
int lyraTabAdvance(const GfxRenderer& renderer, const TabInfo& tab, const int hPadding) {
  return renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR) + 2 * hPadding +
         LyraMetrics::values.tabSpacing;
}

int lyraMarkerReserve(const GfxRenderer& renderer) {
  return renderer.getTextWidth(UI_10_FONT_ID, BaseTheme::TAB_MORE_BEFORE, EpdFontFamily::REGULAR) +
         renderer.getTextWidth(UI_10_FONT_ID, BaseTheme::TAB_MORE_AFTER, EpdFontFamily::REGULAR) +
         LyraMetrics::values.tabSpacing * 2;
}
}  // namespace

void LyraTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;

  // Grey whether or not the bar has focus: only the selected tab's pill (below)
  // changes between the two, so the bar keeps one steady background.
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);

  if (tabs.empty()) {
    renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
    return;
  }

  // The Calendar screen draws a tab per selected calendar, so the bar can be
  // handed more tabs than fit; only the window around the active one is drawn.
  std::vector<int> widths;
  widths.reserve(tabs.size());
  int active = 0;
  for (size_t i = 0; i < tabs.size(); i++) {
    widths.push_back(lyraTabAdvance(renderer, tabs[i], hPaddingInSelection));
    if (tabs[i].selected) active = static_cast<int>(i);
  }
  const TabWindow window =
      tabWindow(widths, rect.width - LyraMetrics::values.contentSidePadding * 2, lyraMarkerReserve(renderer), active);

  if (window.moreBefore) {
    renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, TAB_MORE_BEFORE, true, EpdFontFamily::REGULAR);
    currentX +=
        renderer.getTextWidth(UI_10_FONT_ID, TAB_MORE_BEFORE, EpdFontFamily::REGULAR) + LyraMetrics::values.tabSpacing;
  }

  for (int i = window.first; i < window.first + window.count; i++) {
    const auto& tab = tabs[static_cast<size_t>(i)];
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR);

    if (tab.selected) {
      // The selected tab is the same rounded pill either way. With the bar
      // focused it is solid black; with focus elsewhere it keeps that shape as
      // just a border, the bar's own grey background showing through inside.
      // 2px clear above and below the pill, between it and the lines that
      // bound the bar (the rule above it, drawLine() below): the pill spans
      // rows 2..height-4, the bar's own bottom line sits on row height-1.
      const int pillY = rect.y + 2;
      const int pillWidth = textWidth + 2 * hPaddingInSelection;
      const int pillHeight = rect.height - 5;
      if (selected) {
        renderer.fillRoundedRect(currentX, pillY, pillWidth, pillHeight, cornerRadius, Color::Black);
      } else {
        renderer.drawRoundedRect(currentX, pillY, pillWidth, pillHeight, selectionLineWidth, cornerRadius, true);
      }
    }

    renderer.drawText(UI_10_FONT_ID, currentX + hPaddingInSelection, rect.y + 6, tab.label, !(tab.selected && selected),
                      EpdFontFamily::REGULAR);

    currentX += textWidth + LyraMetrics::values.tabSpacing + 2 * hPaddingInSelection;
  }

  if (window.moreAfter) {
    renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, TAB_MORE_AFTER, true, EpdFontFamily::REGULAR);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

bool LyraTheme::tabIndexFromPoint(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                                  const int x, const int y, int& index) const {
  if (tabs.empty() || y < rect.y || y >= rect.y + rect.height) {
    return false;
  }

  // Measured and windowed exactly as drawTabBar does, so a tap lands on the tab
  // that is actually under it once the bar is scrolled.
  std::vector<int> widths;
  widths.reserve(tabs.size());
  int active = 0;
  for (size_t i = 0; i < tabs.size(); i++) {
    widths.push_back(lyraTabAdvance(renderer, tabs[i], hPaddingInSelection));
    if (tabs[i].selected) active = static_cast<int>(i);
  }
  const TabWindow window =
      tabWindow(widths, rect.width - LyraMetrics::values.contentSidePadding * 2, lyraMarkerReserve(renderer), active);

  int currentX = rect.x + LyraMetrics::values.contentSidePadding;

  if (window.moreBefore) {
    const int markerWidth = renderer.getTextWidth(UI_10_FONT_ID, TAB_MORE_BEFORE, EpdFontFamily::REGULAR);
    // The marker steps the window one tab that way, so it is a control rather
    // than dead space.
    if (x >= rect.x && x < currentX + markerWidth) {
      index = window.first - 1;
      return true;
    }
    currentX += markerWidth + LyraMetrics::values.tabSpacing;
  }

  for (int i = window.first; i < window.first + window.count; i++) {
    const int textWidth =
        renderer.getTextWidth(UI_10_FONT_ID, tabs[static_cast<size_t>(i)].label, EpdFontFamily::REGULAR);
    const int tabWidth = textWidth + 2 * hPaddingInSelection;
    const int left = (i == window.first && !window.moreBefore) ? rect.x : currentX - LyraMetrics::values.tabSpacing / 2;
    const int right = currentX + tabWidth + LyraMetrics::values.tabSpacing / 2;
    if (x >= left && x < right) {
      index = i;
      return true;
    }
    currentX += tabWidth + LyraMetrics::values.tabSpacing;
  }

  if (window.moreAfter && x >= currentX) {
    index = window.first + window.count;
    return true;
  }

  return false;
}

int LyraTheme::getListRowStep(bool hasSubtitle) const {
  int rowHeight = (hasSubtitle) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  return rowHeight;
}

int LyraTheme::getListPageItems(int contentHeight, bool hasSubtitle) const {
  const int rowStep = getListRowStep(hasSubtitle);
  if (rowStep <= 0) return 1;
  return std::max(1, contentHeight / rowStep);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue, bool highlightValue,
                         const std::function<bool(int index)>& rowDimmed) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  int pageItems = rowHeight > 0 ? std::max(1, rect.height / rowHeight) : 1;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(
        rect.x + LyraMetrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
        contentWidth - LyraMetrics::values.contentSidePadding * 2, rowHeight, cornerRadius, Color::LightGray);
  }

  int textX = rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection;
  int textWidth = contentWidth - LyraMetrics::values.contentSidePadding * 2 - hPaddingInSelection * 2;
  int iconSize;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSize : listIconSize;
    textX += iconSize + hPaddingInSelection;
    textWidth -= iconSize + hPaddingInSelection;
  }

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int rowTextWidth = textWidth;

    // Draw name
    int valueWidth = 0;
    std::string valueText = "";
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxListValueWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPaddingInSelection;
      rowTextWidth -= valueWidth;
    }

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 7, item.c_str(), true);

    // Apply checkerboard dither to create gray text effect for dimmed items
    if (rowDimmed && rowDimmed(i) && i != selectedIndex) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, item.c_str());
      const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
      for (int py = itemY + 7; py < itemY + 7 + lineH; py++)
        for (int px = textX; px < textX + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, iconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, rect.x + LyraMetrics::values.contentSidePadding + hPaddingInSelection,
                          itemY + iconY, iconSize);
      }
    }

    if (rowSubtitle != nullptr) {
      // Draw subtitle
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
      renderer.drawText(SMALL_FONT_ID, textX, itemY + 30, subtitle.c_str(), true);
    }

    // Draw value
    if (!valueText.empty()) {
      if (i == selectedIndex && highlightValue) {
        renderer.fillRoundedRect(
            rect.x + contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueWidth, itemY,
            valueWidth + hPaddingInSelection, rowHeight, cornerRadius, Color::Black);
      }

      int valueY = itemY + 6;
      if (rowSubtitle != nullptr) {
        valueY = itemY + 16;
      }
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - LyraMetrics::values.contentSidePadding - valueWidth,
                        valueY, valueText.c_str(), !(i == selectedIndex && highlightValue));
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  // X3 has wider screen in portrait (528 vs 480), use more spacing
  constexpr int x4ButtonPositions[] = {58, 146, 254, 342};
  constexpr int x3ButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = gpio.deviceIsX3() ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius, Color::White);
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               Color::White);
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn,
                                    const bool topSelected, const bool bottomSelected) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonMargin = 0;

  // A selected label is drawn inverted -- white letters on a black fill of the
  // same shape as its border -- so it reads as the current choice.
  if (gpio.deviceIsX3()) {
    // X3 layout: Up on left side, Down on right side, positioned higher --
    // both boxes centred on x3ButtonCenterY (see its own comment above).

    if (topBtn != nullptr && topBtn[0] != '\0') {
      const int height = sideButtonBoxHeight(renderer, topBtn);
      const int top = x3ButtonCenterY - height / 2;
      if (topSelected) {
        renderer.fillRoundedRect(buttonMargin, top, buttonWidth, height, cornerRadius, false, true, false, true,
                                 Color::Black);
      }
      renderer.drawRoundedRect(buttonMargin, top, buttonWidth, height, 1, cornerRadius, false, true, false, true, true);
      renderer.drawTextStacked(SMALL_FONT_ID, buttonMargin + buttonWidth / 2, x3ButtonCenterY, topBtn, stackLineAdvance,
                               /*black=*/!topSelected);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int height = sideButtonBoxHeight(renderer, bottomBtn);
      const int top = x3ButtonCenterY - height / 2;
      const int rightX = screenWidth - buttonWidth;
      if (bottomSelected) {
        renderer.fillRoundedRect(rightX, top, buttonWidth, height, cornerRadius, true, false, true, false,
                                 Color::Black);
      }
      renderer.drawRoundedRect(rightX, top, buttonWidth, height, 1, cornerRadius, true, false, true, false, true);
      renderer.drawTextStacked(SMALL_FONT_ID, rightX + buttonWidth / 2, x3ButtonCenterY, bottomBtn, stackLineAdvance,
                               /*black=*/!bottomSelected);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    const int x = screenWidth - buttonWidth;
    const int topHeight = sideButtonBoxHeight(renderer, topBtn);
    const int bottomHeight = sideButtonBoxHeight(renderer, bottomBtn);
    // The second box starts below the first one's own (variable) height plus
    // the 5px gap between them.
    const int bottomTop = topHintButtonY + topHeight + 5;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      if (topSelected) {
        renderer.fillRoundedRect(x, topHintButtonY, buttonWidth, topHeight, cornerRadius, true, false, true, false,
                                 Color::Black);
      }
      renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, topHeight, 1, cornerRadius, true, false, true, false,
                               true);
      renderer.drawTextStacked(SMALL_FONT_ID, x + buttonWidth / 2, topHintButtonY + topHeight / 2, topBtn,
                               stackLineAdvance, /*black=*/!topSelected);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      if (bottomSelected) {
        renderer.fillRoundedRect(x, bottomTop, buttonWidth, bottomHeight, cornerRadius, true, false, true, false,
                                 Color::Black);
      }
      renderer.drawRoundedRect(x, bottomTop, buttonWidth, bottomHeight, 1, cornerRadius, true, false, true, false,
                               true);
      renderer.drawTextStacked(SMALL_FONT_ID, x + buttonWidth / 2, bottomTop + bottomHeight / 2, bottomBtn,
                               stackLineAdvance, /*black=*/!bottomSelected);
    }
  }
}

int LyraTheme::getSideButtonHintsBottom(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  // Only the X3 has the two buttons on opposite edges at one shared height, so
  // only there is there a single line the content can sit below. The X4 stacks
  // them one under the other on a single side, far lower down.
  if (gpio.hasTouch() || !gpio.deviceIsX3()) {
    return 0;
  }
  // Neither button has a label, so neither box is drawn and there is no line to
  // sit below.
  const bool noTop = topBtn == nullptr || topBtn[0] == '\0';
  const bool noBottom = bottomBtn == nullptr || bottomBtn[0] == '\0';
  if (noTop && noBottom) {
    return 0;
  }
  const int tallest = std::max(sideButtonBoxHeight(renderer, topBtn), sideButtonBoxHeight(renderer, bottomBtn));
  const int top = x3ButtonCenterY - tallest / 2;
  return top + tallest;
}

void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  // No longer called from Home -- Home used to share this band with a
  // companion column beside the cover, which is why the cover card's own
  // title/author text was dropped from here (see below); the companion has
  // since moved to its own home grid tile, but the card itself stayed at the
  // width that move already gave it. ReadMenuActivity calls this directly for
  // its own leading row instead, at
  // whatever rect it hands in, so the two places show the exact same "cover
  // together with author and title" rendering rather than a second one
  // invented just for the menu.
  const int tileY = rect.y;
  const bool hasContinueReading = !recentBooks.empty();
  // Drawn smaller than the card actually allows, and centred in the
  // difference, so the selection frame below has real margin to sit in --
  // full size left only ~8px of slack, not enough for the grey fill to read
  // clearly. The thumbnail itself is still generated/cached at the full
  // homeCoverHeight (see getCoverThumbPath below); only how it is drawn here
  // shrinks, via drawBitmap's own scale-to-fit path.
  constexpr int coverShrink = 20;
  const int drawnCoverHeight = LyraMetrics::values.homeCoverHeight - coverShrink;
  const int coverYOffset = coverShrink / 2;
  if (coverWidth == 0) {
    coverWidth = static_cast<int>(drawnCoverHeight * 0.6f);
  }

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    RecentBook book = recentBooks[0];
    const bool bookSelected = (selectorIndex == 0);

    // Grey background behind the whole row when selected -- same fill and
    // the same contentSidePadding inset drawButtonMenu's own selected row
    // uses, so this reads as one more row in the list it sits above rather
    // than a different selection idiom. Drawn before the cover art below, so
    // the art paints over its own footprint on top of it rather than being
    // erased by it.
    if (bookSelected) {
      const int fillX = rect.x + LyraMetrics::values.contentSidePadding;
      const int fillWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
      renderer.fillRoundedRect(fillX, rect.y, fillWidth, rect.height, cornerRadius, Color::LightGray);
    }

    if (!coverRendered) {
      std::string coverPath = book.coverBmpPath;
      bool hasCover = true;
      int tileX = LyraMetrics::values.contentSidePadding;
      if (coverPath.empty()) {
        hasCover = false;
      } else {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, LyraMetrics::values.homeCoverHeight);

        // First time: load cover from SD and render
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // Scaled from the thumbnail's native size (generated at the full
            // homeCoverHeight) down to drawnCoverHeight, aspect preserved --
            // coverWidth has to reflect the *drawn* size, not the source
            // bitmap's, since the border/selection frame/companion column
            // below all key off it.
            coverWidth = static_cast<int>(
                bitmap.getWidth() * (static_cast<float>(drawnCoverHeight) / static_cast<float>(bitmap.getHeight())));
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection + coverYOffset,
                                coverWidth, drawnCoverHeight);
          } else {
            hasCover = false;
          }
          file.close();
        }
      }

      // Draw either way
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection + coverYOffset, coverWidth,
                        drawnCoverHeight, true);

      if (!hasCover) {
        // Render empty cover
        renderer.fillRect(tileX + hPaddingInSelection,
                          tileY + hPaddingInSelection + coverYOffset + drawnCoverHeight / 3, coverWidth,
                          2 * drawnCoverHeight / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + 24, tileY + hPaddingInSelection + coverYOffset + 24,
                          32);
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    const int tileX = LyraMetrics::values.contentSidePadding;

    // Title and author, beside the cover -- dropped from here back when Home
    // still called this, to leave the companion column room beside it (see
    // this function's own comment above). ReadMenuActivity hands this a
    // full-width band with no column competing for it, so there is room for
    // them again.
    const int textX = tileX + hPaddingInSelection + coverWidth + LyraMetrics::values.contentSidePadding;
    const int textRight = rect.x + rect.width - LyraMetrics::values.contentSidePadding;
    const int textWidth = textRight - textX;
    if (textWidth > 0) {
      const auto lines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 3);
      const auto truncatedAuthor =
          book.author.empty() ? std::string{} : renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);

      int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * static_cast<int>(lines.size());
      if (!truncatedAuthor.empty()) totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2;
      int textY = tileY + hPaddingInSelection + coverYOffset + (drawnCoverHeight - totalTextHeight) / 2;

      for (const auto& line : lines) {
        renderer.drawText(UI_12_FONT_ID, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
        textY += renderer.getLineHeight(UI_12_FONT_ID);
      }
      if (!truncatedAuthor.empty()) {
        textY += renderer.getLineHeight(UI_10_FONT_ID) / 2;
        renderer.drawText(UI_10_FONT_ID, textX, textY, truncatedAuthor.c_str(), true);
      }
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void LyraTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  constexpr int padding = 48;
  renderer.drawText(UI_12_FONT_ID, rect.x + padding,
                    rect.y + rect.height / 2 - renderer.getLineHeight(UI_12_FONT_ID) - 2, tr(STR_NO_OPEN_BOOK), true,
                    EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + padding, rect.y + rect.height / 2 + 2, tr(STR_START_READING), true);
}

int LyraTheme::getGridRowStep(int contentHeight, int buttonCount) const {
  const int columns = LyraMetrics::values.homeGridColumns > 0 ? LyraMetrics::values.homeGridColumns : 1;
  const int rows = (buttonCount + columns - 1) / columns;
  if (rows <= 0 || contentHeight <= 0) return LyraMetrics::values.homeGridTileHeight;
  // Share the height between the rows, never dropping below the tile the
  // artwork and its label need.
  return std::max(LyraMetrics::values.homeGridTileHeight, contentHeight / rows);
}

Rect LyraTheme::tileIconRect(const GfxRenderer& renderer, const Rect rect, const int buttonCount,
                             const int index) const {
  const int columns = LyraMetrics::values.homeGridColumns > 0 ? LyraMetrics::values.homeGridColumns : 1;
  const int tileHeight = getGridRowStep(rect.height, buttonCount);
  const int tileWidth = rect.width / columns;
  // Breathing room between the artwork and its label -- kept in step with
  // drawButtonGrid()'s own labelGap, which this same value feeds into.
  constexpr int labelGap = 10;
  const int labelHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int contentHeight = homeGridIconSize + labelGap + labelHeight;

  const int column = index % columns;
  const int row = index / columns;
  const int tileX = rect.x + column * tileWidth;
  const int tileY = rect.y + row * tileHeight;
  // The tile's content is centred as a block, so a short label and a tall
  // icon stay visually anchored to each other rather than to the cell edges.
  const int contentTop = tileY + (tileHeight - contentHeight) / 2;
  const int iconX = tileX + (tileWidth - homeGridIconSize) / 2;
  return Rect{iconX, contentTop, homeGridIconSize, homeGridIconSize};
}

Rect LyraTheme::getGridTileIconRect(const GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                    const int index) const {
  if (index < 0 || index >= buttonCount) return Rect{};
  return tileIconRect(renderer, rect, buttonCount, index);
}

void LyraTheme::drawButtonGrid(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon,
                               const std::function<int(int index)>& badgeCount) const {
  const int columns = LyraMetrics::values.homeGridColumns > 0 ? LyraMetrics::values.homeGridColumns : 1;
  const int tileWidth = rect.width / columns;
  constexpr int labelGap = 10;
  constexpr int selectionPadding = 8;
  const int labelHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int contentHeight = homeGridIconSize + labelGap + labelHeight;

  for (int i = 0; i < buttonCount; i++) {
    const int column = i % columns;
    const int tileX = rect.x + column * tileWidth;
    const bool selected = i == selectedIndex;

    // Same rect tileIconRect() (and so the public getGridTileIconRect(), for
    // a caller overlaying its own artwork -- see the companion's home tile)
    // computes; contentTop is this tile's icon rect y, one and the same.
    const Rect iconRect = tileIconRect(renderer, rect, buttonCount, i);
    const int contentTop = iconRect.y;

    const std::string labelStr = buttonLabel(i);
    const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, labelStr.c_str(), tileWidth - 16, style);
    const int labelWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), style);

    if (selected) {
      // An outline, not a fill: the artwork is line work, and inverting a tile
      // this size is a lot of ink to move on every selection change.
      const int boxWidth = std::max(homeGridIconSize, labelWidth) + selectionPadding * 4;
      const int boxHeight = contentHeight + selectionPadding * 2;
      renderer.drawRoundedRect(tileX + (tileWidth - boxWidth) / 2, contentTop - selectionPadding, boxWidth, boxHeight,
                               selectionLineWidth, cornerRadius, true);
    }

    if (rowIcon != nullptr) {
      const uint8_t* iconBitmap = iconForName(rowIcon(i), homeGridIconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, iconRect.x, iconRect.y, homeGridIconSize);
      }
    }

    renderer.drawText(UI_12_FONT_ID, tileX + (tileWidth - labelWidth) / 2, contentTop + homeGridIconSize + labelGap,
                      label.c_str(), true, style);

    // Drawn after the icon and the selection outline, so it always reads on
    // top rather than being cut off by either. Plain digits, no shape behind
    // them - just black text straddling the icon's top-right corner.
    const int badge = badgeCount != nullptr ? badgeCount(i) : 0;
    if (badge > 0) {
      const std::string badgeText = std::to_string(badge);
      const int badgeTextWidth = renderer.getTextWidth(SMALL_FONT_ID, badgeText.c_str());
      const int badgeTextHeight = renderer.getTextHeight(SMALL_FONT_ID);
      const int badgeX = iconRect.x + homeGridIconSize - badgeTextWidth / 2;
      const int badgeY = contentTop - badgeTextHeight / 2;
      renderer.drawText(SMALL_FONT_ID, badgeX, badgeY, badgeText.c_str());
    }
  }
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = rect.width - LyraMetrics::values.contentSidePadding * 2;
    Rect tileRect = Rect{rect.x + LyraMetrics::values.contentSidePadding,
                         rect.y + i * (LyraMetrics::values.menuRowHeight + LyraMetrics::values.menuSpacing), tileWidth,
                         LyraMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (LyraMetrics::values.menuRowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon, mainMenuIconSize);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, textX, textY, mainMenuIconSize);
        textX += mainMenuIconSize + hPaddingInSelection + 2;
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}
