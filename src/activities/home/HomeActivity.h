#pragma once
#include <CompanionMood.h>

#include <functional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  // An extra stop bolted onto selectorIndex, the same idiom
  // PlannerScreenActivity's own headerFocused uses: "previous" from
  // selectorIndex == 0 lands here instead of wrapping to the last entry,
  // "next" from here returns to selectorIndex == 0, "previous" from here
  // wraps to the last entry. Right2 here opens Sync All.
  bool headerFocused = false;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  // Home can be entered while Back is still held (e.g. leaving Settings with
  // Back): ignore that stale release until a fresh press is seen here.
  bool backPressSeen = false;
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  // One row of the home screen, in the order it is drawn.
  //
  // This replaces the index arithmetic that used to be spelled out in three
  // places - a count, a menu-item-to-index map and its inverse - which had to
  // be kept in step by hand, and which fixed the recent books at the front.
  // Order is now whatever buildEntries() appends.
  struct HomeEntry {
    const char* label;  // Translated; unused for cover-tile entries
    UIIcon icon;
    HomeMenuItem item;  // NONE when the entry opens a book
    int recentIndex;    // >= 0 when the entry opens recentBooks[recentIndex]
  };

  std::vector<HomeEntry> entries;

  // Rebuilt in onEnter, once the recent books and the OPDS check are in: the
  // list is walked every loop() and must not allocate there.
  void buildEntries();
  // Rows the cover tile owns, which the menu below it does not draw. Zero on
  // themes that carry reading as a menu entry instead.
  int leadingRecentCount() const;
  // Top of the grid/menu, shared by loop()'s touch hit-testing and render()'s
  // own drawing so the two can never disagree about where it starts.
  // Reserves room for the cover card's own band only on a theme that actually
  // draws one there (metrics.homeShowsCoverCard) -- on one that does not
  // (companion-less Home used to fill that reserved band regardless with its
  // own column; now nothing does, so reserving it here would just be a blank
  // gap above the grid).
  int menuTop() const;

  // Re-checks the companion's sleep-window mood on an idle timer (see loop())
  // rather than only on screen entry -- otherwise sitting on Home past the
  // configured sleep time never shows its grid tile as asleep until you leave
  // and come back. Does NOT force a repaint on every tick --
  // COMPANION_REFRESH_INTERVAL_MS only re-reads the clock (one cheap I2C
  // transaction) and compares moods, calling requestUpdate() solely when the
  // result actually changed.
  unsigned long lastCompanionRefreshMs = 0;
  companion::Mood lastCompanionMood = companion::Mood::Happy;

  // Draws the companion's own current pose (mood and character both live) as
  // its grid tile's icon, in place of the static UIIcon a normal app tile
  // gets, fit and centred within `bounds` -- render() hands this a rect
  // centred on (but usually larger than) GUI.getGridTileIconRect()'s own
  // answer, since the sprite scaled to literally match that box reads
  // smaller and thinner than the icons that actually fill it (see render()'s
  // own comment). No label, no bubble: the tile's label is drawn generically
  // like every other tile's (see buildEntries()), and there is no room here
  // for more than the appearance itself.
  void drawCompanionIcon(Rect bounds) const;
  // Opens QuickPickActivity. No-op when the companion is off (unreachable in
  // practice: buildEntries() already leaves its tile out of the grid then).
  void activateCompanion();

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
