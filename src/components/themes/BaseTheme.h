#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GfxRenderer;
struct RecentBook;

struct Rect {
  int x;
  int y;
  int width;
  int height;

  explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0) : x(x), y(y), width(width), height(height) {}
};

struct TabInfo {
  const char* label;
  bool selected;
};

// Height a "focused header" invert-highlight uses, on every screen that has
// one -- deliberately independent of that screen's own (taller)
// metrics.headerHeight, which is sized for a full title+subtitle band.
// Inverting the whole band reads as a big, blocky fill; this hugs just the
// title row instead, the same thinness QuickPickActivity's own header focus
// originated with (its HEADER_HIGHLIGHT_HEIGHT), so every screen's header
// highlight reads as the same weight regardless of how tall its own header
// actually is.
constexpr int HEADER_FOCUS_HIGHLIGHT_HEIGHT = 30;

struct ThemeMetrics {
  int batteryWidth;
  int batteryHeight;

  int topPadding;
  int batteryBarHeight;
  int headerHeight;
  int verticalSpacing;

  int previewPadding;
  int previewHeightPercent;

  int contentSidePadding;
  int listRowHeight;
  int listWithSubtitleRowHeight;
  int menuRowHeight;
  int menuSpacing;

  int tabSpacing;
  int tabBarHeight;

  int scrollBarWidth;
  int scrollBarRightOffset;

  int homeTopPadding;
  int homeCoverHeight;
  int homeCoverTileHeight;
  int homeRecentBooksCount;
  bool homeContinueReadingInMenu;
  // False when a theme's own drawRecentBookCover() has moved off Home
  // entirely (see ReadMenuActivity, which calls the same per-theme function
  // for its own leading row instead) -- distinct from
  // homeContinueReadingInMenu, which is about whether the *menu* carries a
  // "Read"/"Continue reading" entry, not about whether the cover card itself
  // draws.
  bool homeShowsCoverCard;
  int homeMenuTopOffset;
  // Home menu as tiles rather than rows: columns across, and the height of one
  // tile including its label. Zero columns keeps the list.
  //
  // The column count decides the row count too - the entries wrap into
  // ceil(count / columns) rows, which then share whatever height is left under
  // the cover card. Both the drawing and the tile hit-testing read this one
  // value, so they cannot disagree about the shape.
  int homeGridColumns;
  int homeGridTileHeight;

  int buttonHintsHeight;
  int sideButtonHintsWidth;

  int progressBarHeight;
  int progressBarMarginTop;
  int statusBarHorizontalMargin;
  int statusBarVerticalMargin;
  int keyboardKeyHeight;
  int keyboardKeySpacing;
  bool keyboardCenteredText;
  int keyboardVerticalOffset;
  int keyboardTextFieldWidthPercent;
  int keyboardWidthPercent;

  float popupTopOffsetRatio;
  int popupMarginX;
  int popupMarginY;
  int popupFrameThickness;
  int popupCornerRadius;
  bool popupTextBold;
  bool popupTextInverted;
  int popupTextBaselineOffsetY;
  int popupProgressBarHeight;
  bool popupProgressDrawOutline;
  bool popupProgressClampPercent;
  bool popupProgressFillInverted;
  bool popupProgressOutlineInverted;

  int optionPopupItemSpacing;
  int optionPopupInnerPadding;
  int optionPopupSelectionHPadding;
  int optionPopupSelectionVPadding;
  int optionPopupTitleGap;
  bool optionPopupUseSmallFont;
  bool optionPopupOptionFontBold;
  int optionPopupSelectionRadius;
  bool optionPopupSelectionLight;
  bool optionPopupDrawAllRows;
  int optionPopupDialogSideMargin;
  bool optionPopupTitleSeparator;

  int textFieldHorizontalPadding;
  int textFieldNormalThickness;
  int textFieldCursorThickness;
  int textFieldLineEndOffset;
};

enum UIIcon {
  None = 0,
  Folder,
  Text,
  Image,
  Book,
  File,
  Recent,
  Settings,
  Transfer,
  Library,
  Wifi,
  Hotspot,
  Bookmark,
  Tasks,
  Calendar,
  Budget,
  Habits,
  Bell
};

// Default theme implementation (Classic Theme)
// Additional themes can inherit from this and override methods as needed

namespace BaseMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 45,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 30,
                                 .listWithSubtitleRowHeight = 50,
                                 .menuRowHeight = 45,
                                 .menuSpacing = 8,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 400,
                                 .homeCoverTileHeight = 400,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeShowsCoverCard = true,
                                 .homeMenuTopOffset = 10,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 48,
                                 .keyboardKeySpacing = 0,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -13,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.075f,
                                 .popupMarginX = 15,
                                 .popupMarginY = 15,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = true,
                                 .popupTextInverted = true,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = false,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = true,
                                 .popupProgressOutlineInverted = true,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 16,
                                 .optionPopupSelectionHPadding = 8,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupTitleGap = 10,
                                 .optionPopupUseSmallFont = true,
                                 .optionPopupOptionFontBold = true,
                                 .optionPopupSelectionRadius = 0,
                                 .optionPopupSelectionLight = false,
                                 .optionPopupDrawAllRows = false,
                                 .optionPopupDialogSideMargin = 20,
                                 .optionPopupTitleSeparator = true,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0};
}

class BaseTheme {
 public:
  virtual ~BaseTheme() = default;

  // Component drawing methods
  void drawProgressBar(const GfxRenderer& renderer, Rect rect, size_t current, size_t total) const;
  void drawBatteryLeft(const GfxRenderer& renderer, Rect rect,
                       bool showPercentage = true) const;  // Left aligned (reader mode)
  void drawBatteryRight(const GfxRenderer& renderer, Rect rect,
                        bool showPercentage = true) const;  // Right aligned (UI headers)
  virtual void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const;
  virtual void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const;
  // topSelected/bottomSelected draw that label inverted (white letters on a
  // black fill) -- how a caller marks which of the two is the current
  // selection.
  virtual void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn,
                                   bool topSelected = false, bool bottomSelected = false) const;
  // The y just below the lowest side-button box drawSideButtonHints() draws for
  // these two labels -- how far down a screen must start its content so the
  // labels never sit beside it. 0 when there is no such line: nothing is drawn
  // (touch devices), or the two buttons aren't at one shared height (the X4).
  virtual int getSideButtonHintsBottom(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const;
  virtual int getListRowStep(bool hasSubtitle) const;
  virtual int getListPageItems(int contentHeight, bool hasSubtitle) const;
  virtual void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                        const std::function<std::string(int index)>& rowTitle,
                        const std::function<std::string(int index)>& rowSubtitle = nullptr,
                        const std::function<UIIcon(int index)>& rowIcon = nullptr,
                        const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                        const std::function<bool(int index)>& rowDimmed = nullptr) const;
  // showRule: whether to draw the thin rule under the header, on the one
  // theme (Lyra) that draws one at all -- false for QuickPickActivity, whose
  // own companion-focus box highlight sits right where that rule would,
  // making the two look like a redundant double line.
  // includeStatusRow: whether to draw the clock/date/battery row a theme
  // otherwise always paints into a header rect -- false for a *second*,
  // embedded title drawn mid-screen (QuickPickActivity's own scaled-down
  // Tasks section below the companion figure), so that row isn't duplicated;
  // true (the default) preserves every call site that only ever draws one
  // header, at the top of the screen.
  virtual void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                          bool showRule = true, bool includeStatusRow = true) const;
  virtual void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                             const char* rightLabel = nullptr) const;
  virtual void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                          bool selected) const;
  virtual bool tabIndexFromPoint(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, int x, int y,
                                 int& index) const;

  // The run of tabs a bar can actually show. The Calendar screen draws one tab
  // per selected calendar, so a bar can be asked for more tabs than fit across
  // the screen; rather than clipping the overflow silently, each theme draws a
  // window around the active tab and marks the directions it has more in.
  struct TabWindow {
    int first;        // Index of the first tab to draw
    int count;        // How many to draw, starting at first
    bool moreBefore;  // Tabs exist to the left of the window
    bool moreAfter;   // Tabs exist to the right of the window
  };

  // Marker drawn where the window has more tabs beyond it. ASCII, because the UI
  // fonts are subset and cannot be relied on for the typographic guillemets.
  static constexpr const char* TAB_MORE_BEFORE = "<";
  static constexpr const char* TAB_MORE_AFTER = ">";

  /**
   * Picks the window to draw. `widths` holds each tab's full advance - its own
   * width plus the spacing that follows it - because only the theme knows how it
   * lays a tab out. `available` is the room the bar has for tabs, and `reserve`
   * the room the two markers need, taken off only when the tabs do not all fit.
   *
   * The window grows outward from `active`, right first so the bar reads
   * left-to-right, and always contains the active tab even when that one tab is
   * wider than the bar.
   */
  static TabWindow tabWindow(const std::vector<int>& widths, int available, int reserve, int active);

  virtual void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                   const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                   bool& bufferRestored, std::function<bool()> storeCoverBuffer) const;
  virtual void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const;
  // The same entries as tiles: artwork above a centred label, the selected one
  // boxed. Themes that do not draw a grid inherit the list instead.
  // Vertical step between tile rows, given the height the grid has to fill and
  // how many tiles go in it. The activity hit-tests with this too.
  virtual int getGridRowStep(int contentHeight, int buttonCount) const;
  // badgeCount, when given, returns a notification-style count for a tile (0 =
  // no badge). A theme that has no room to draw one - anything falling back to
  // drawButtonMenu's row list - is free to ignore it.
  virtual void drawButtonGrid(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon,
                              const std::function<int(int index)>& badgeCount = nullptr) const;
  // The exact rect drawButtonGrid() would draw tile `index`'s icon into, for a
  // caller that wants to overlay its own artwork there instead of a static
  // UIIcon (the companion's home tile draws its own current pose here, sized
  // to fit whatever this returns, rather than an icon bitmap). An empty rect
  // means this theme has no grid to overlay onto (drawButtonGrid() falls back
  // to the plain row list, which has no icon-sized square of its own).
  virtual Rect getGridTileIconRect(const GfxRenderer& renderer, Rect rect, int buttonCount, int index) const;
  virtual Rect drawPopup(const GfxRenderer& renderer, const char* message) const;
  virtual void drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                               int selectedIndex) const;
  virtual void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const;
  void drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage, const int pageCount,
                     std::string title, const int paddingBottom = 0, const int textYOffset = 0,
                     const bool fillMargin = true, const bool isPageBookmarked = false,
                     const bool pageCountEstimated = false) const;
  void drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const;
  virtual void drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode = false,
                             int contentStartX = 0, int contentWidth = 0) const;
  virtual bool showsFileIcons() const { return false; }

  // Shared constants and helpers for battery drawing (used by all themes)
  static constexpr int batteryPercentSpacing = 4;
  static void drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight);
  static void drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY);
};
