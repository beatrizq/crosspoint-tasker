#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GCalEventCache.h>
#include <GfxRenderer.h>
#include <HabitifyHabitCache.h>
#include <HalStorage.h>
#include <I18n.h>
#include <TodoistTaskCache.h>
#include <Utf8.h>
#include <Xtc.h>
#include <YnabAccountCache.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "QuickPickActivity.h"
#include "RecentBooksStore.h"
#include "companion/CompanionRenderer.h"
#include "companion/CompanionTracker.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HomeAppOrder.h"
#ifdef ENABLE_BLE_NOTIFY_SPIKE
#include "network/BleNotificationQueue.h"
#endif

namespace {
// The home entry each app opens. Kept here rather than in the app table so
// util/HomeAppOrder.h does not have to include the activity layer.
HomeMenuItem homeMenuItemFor(const homeAppOrder::AppId id) {
  switch (id) {
    case homeAppOrder::AppId::Read:
      return HomeMenuItem::READ_MENU;
    case homeAppOrder::AppId::Tasks:
      return HomeMenuItem::TASKS;
    case homeAppOrder::AppId::Calendar:
      return HomeMenuItem::CALENDAR;
    case homeAppOrder::AppId::Budget:
      return HomeMenuItem::BUDGET;
    case homeAppOrder::AppId::Habits:
      return HomeMenuItem::HABITS;
    case homeAppOrder::AppId::Notifications:
      return HomeMenuItem::NOTIFICATIONS;
    case homeAppOrder::AppId::Companion:
      return HomeMenuItem::COMPANION_SCREEN;
    case homeAppOrder::AppId::Settings:
      return HomeMenuItem::SETTINGS_MENU;
  }
  return HomeMenuItem::NONE;
}
}  // namespace

int HomeActivity::getMenuItemCount() const { return static_cast<int>(entries.size()); }

int HomeActivity::leadingRecentCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Nothing to lead with when this theme's cover card isn't drawn on Home at
  // all (see ThemeMetrics::homeShowsCoverCard) -- these entries exist only to
  // be the thing the card represents, and a theme with no card has nowhere
  // for them to be selected from.
  if (!metrics.homeShowsCoverCard) return 0;
  return metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size());
}

int HomeActivity::menuTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!metrics.homeShowsCoverCard) return metrics.homeTopPadding + metrics.verticalSpacing;
  return metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
}

void HomeActivity::buildEntries() {
  entries.clear();
  entries.reserve(8);

  const auto& metrics = UITheme::getInstance().getMetrics();
  if (metrics.homeShowsCoverCard && !metrics.homeContinueReadingInMenu) {
    // Cover-tile themes: the recent books own the leading slots and are drawn
    // by the tile rather than the menu, but they are still entries, so one
    // list describes the whole screen. Skipped when this theme's cover card
    // doesn't draw on Home at all (see leadingRecentCount()) -- with no card
    // to represent them, these would otherwise be selectable slots nothing
    // draws, rather than absent as reading itself now is on the Read tile.
    for (int i = 0; i < static_cast<int>(recentBooks.size()); i++) {
      entries.push_back({nullptr, Book, HomeMenuItem::NONE, i});
    }
  }

  // Themes without a cover tile carry reading itself as the leading entry, which
  // stands in for the Read tile rather than sitting beside it.
  const bool resumeLeads = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  if (resumeLeads) {
    entries.push_back({tr(STR_MENU_READ), Book, HomeMenuItem::NONE, 0});
  }

  // The tiles, in the order the user arranged them on the App Order screen. Read
  // is one of them, so it moves with the rest - the cover card above opens the
  // last book, and the Read tile opens everything else about books, which is a
  // subject like any other.
  int order[homeAppOrder::APP_COUNT];
  homeAppOrder::parse(SETTINGS.homeAppOrder, order);
  for (const int index : order) {
    const auto& app = homeAppOrder::appAt(index);
    // Skipped only when the resume entry above already speaks for reading;
    // adding it again would draw it twice.
    if (resumeLeads && app.id == homeAppOrder::AppId::Read) continue;
#ifndef ENABLE_BLE_NOTIFY_SPIKE
    // Only a real, working tile in builds with the BLE spike compiled in (see
    // BleNotifyRelay's own doc comment) -- kept in homeAppOrder's table
    // regardless so the app-id numbering and persisted order format stay
    // identical across build flavors, but never rendered as a tile here.
    if (app.id == homeAppOrder::AppId::Notifications) continue;
#endif
    // Settings is reached by pressing Right1 instead (see loop()'s own
    // comment) -- never a grid tile of its own any more.
    if (app.id == homeAppOrder::AppId::Settings) continue;
    // The companion's label prefers its character's own built-in name over
    // this table's generic appName ("Companion") when no nickname is set --
    // see CompanionTracker::displayName() and AppId::Companion's own comment.
    // Every other app keeps this table's own displayName() lookup.
    const char* label =
        app.id == homeAppOrder::AppId::Companion ? CompanionTracker::displayName() : homeAppOrder::displayName(app.id);
    entries.push_back({label, app.icon, homeMenuItemFor(app.id), -1});
  }
}

void HomeActivity::drawCompanionIcon(const Rect bounds) const {
  if (bounds.width <= 0 || bounds.height <= 0) return;

  const auto id = CompanionTracker::activeId();
  const auto mood = COMPANION.currentMood();

  // Whole-pixel scales only: fractional scaling would smear the baked
  // dither. Static, unlike Home's old companion column -- there is no bubble
  // or label competing for room here, and a walk cycle would read as busy
  // packed this small among five other still icons.
  constexpr int MAX_SCALE = 8;
  int scale = 1;
  for (int candidate = MAX_SCALE; candidate >= 1; candidate--) {
    if (companion::poseWidth(candidate) <= bounds.width && companion::poseHeight(candidate) <= bounds.height) {
      scale = candidate;
      break;
    }
  }
  const int spriteX = bounds.x + (bounds.width - companion::poseWidth(scale)) / 2;
  const int spriteY = bounds.y + (bounds.height - companion::poseHeight(scale)) / 2;
  companion::drawPose(renderer, id, mood, spriteX, spriteY, scale);
}

void HomeActivity::activateCompanion() {
  startActivityForResult(std::make_unique<QuickPickActivity>(renderer, mappedInput), [](const ActivityResult&) {});
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  buildEntries();

  // One I2C read to resolve the calendar day, so currentMood() is cheap from the
  // render path. Here rather than in render() for exactly that reason.
  COMPANION.refreshForDisplay();
  // Idle-tick baseline for loop()'s own re-check -- seeded here (not left at 0)
  // so the first idle tick doesn't immediately redo what onEnter() just did.
  lastCompanionRefreshMs = millis();
  lastCompanionMood = COMPANION.currentMood();

  selectorIndex = 0;
  headerFocused = false;
  if (initialMenuItem != HomeMenuItem::NONE) {
    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
      if (entries[i].item == initialMenuItem) {
        selectorIndex = i;
        break;
      }
    }
  }

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  // Re-checks the companion's mood (in particular, whether it's now inside its
  // sleep window) on an idle timer -- see lastCompanionRefreshMs's own comment
  // in the header for why this doesn't unconditionally repaint. Only costs a
  // repaint on the tick where the mood actually changes; every other tick is
  // one I2C read plus an enum compare.
  constexpr unsigned long COMPANION_REFRESH_INTERVAL_MS = 60000;
  if (millis() - lastCompanionRefreshMs >= COMPANION_REFRESH_INTERVAL_MS) {
    lastCompanionRefreshMs = millis();
    COMPANION.refreshForDisplay();
    const auto mood = COMPANION.currentMood();
    if (mood != lastCompanionMood) {
      lastCompanionMood = mood;
      requestUpdate();
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();

  const int menuCount = static_cast<int>(entries.size());

  auto activateSelection = [this] {
    if (selectorIndex < 0 || selectorIndex >= static_cast<int>(entries.size())) return;
    const HomeEntry& entry = entries[selectorIndex];

    if (entry.recentIndex >= 0 && entry.recentIndex < static_cast<int>(recentBooks.size())) {
      onSelectBook(recentBooks[entry.recentIndex].path);
      return;
    }
    switch (entry.item) {
      case HomeMenuItem::READ_MENU:
        activityManager.goToReadMenu();
        break;
      case HomeMenuItem::TASKS:
        activityManager.goToTasks();
        break;
      case HomeMenuItem::CALENDAR:
        activityManager.goToCalendar();
        break;
      case HomeMenuItem::BUDGET:
        activityManager.goToBudget();
        break;
      case HomeMenuItem::HABITS:
        activityManager.goToHabits();
        break;
#ifdef ENABLE_BLE_NOTIFY_SPIKE
      case HomeMenuItem::NOTIFICATIONS:
        activityManager.goToBleNotifications();
        break;
#endif
      case HomeMenuItem::COMPANION_SCREEN:
        activateCompanion();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    if (headerFocused) {
      headerFocused = false;
      selectorIndex = 0;
    } else if (selectorIndex == menuCount - 1) {
      headerFocused = true;
    } else {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    if (headerFocused) {
      headerFocused = false;
      selectorIndex = menuCount - 1;
    } else if (selectorIndex == 0) {
      headerFocused = true;
    } else {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    }
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    // A swipe is a page-jump, not a single step, so it always leaves the
    // header stop (if it was focused) -- same reasoning as
    // OrganizerScreenActivity's own swipe handling.
    headerFocused = false;
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    headerFocused = false;
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) backPressSeen = true;

  // Back is otherwise unused on the home menu, so it doubles as the direct
  // shortcut to Settings -- Settings no longer has a tile of its own (see
  // buildEntries()). Sync All moves to Right2 while the header is focused
  // instead (see below). backPressSeen guards against the stale release of
  // the Back press that closed the previous activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Right1) && backPressSeen) {
    onSettingsOpen();
    return;
  }

  int tx = 0;
  int ty = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTouchDown(tx, ty) && tx >= 0 && tx < renderer.getScreenWidth() &&
      ty >= metrics.homeTopPadding && ty < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    if (selectorIndex != 0) {
      selectorIndex = 0;
      requestUpdate();
    }
    return;
  }

  if (!recentBooks.empty() &&
      mappedInput.wasTapInRect(0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight)) {
    selectorIndex = 0;
    activateSelection();
    return;
  }

  const int gridTop = menuTop();
  const int leadingRecents = leadingRecentCount();
  const int renderedMenuCount = static_cast<int>(entries.size()) - leadingRecents;

  // Down highlights the entry under the finger, a tap opens it. Shared by both
  // layouts so the grid and the list behave identically to the touch.
  auto handleMenuTouch = [this, leadingRecents, &activateSelection](MappedInputManager::RowTouch touch,
                                                                    int renderedIndex) {
    const int touchedIndex = renderedIndex + leadingRecents;
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
      return;
    }
    selectorIndex = touchedIndex;
    activateSelection();
  };

  if (metrics.homeGridColumns > 0) {
    // Tiles: one hit-test per column band, since the shared helper walks rows.
    const int columns = metrics.homeGridColumns;
    const int tileWidth = renderer.getScreenWidth() / columns;
    const int gridRows = (renderedMenuCount + columns - 1) / columns;
    const int menuHeight = renderer.getScreenHeight() - gridTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    // The same step the theme drew with: the rows share whatever height is left.
    const int tileStep = GUI.getGridRowStep(menuHeight, renderedMenuCount);
    for (int column = 0; column < columns; column++) {
      int gridRow = -1;
      const auto tileTouch = mappedInput.rowTouch(gridRow, gridTop, tileStep, gridRows, column * tileWidth,
                                                  (column + 1) * tileWidth, tileStep);
      if (tileTouch == MappedInputManager::RowTouch::None) continue;
      const int renderedIndex = gridRow * columns + column;
      // The last row can be short; its empty cells are not entries.
      if (renderedIndex >= renderedMenuCount) return;
      handleMenuTouch(tileTouch, renderedIndex);
      return;
    }
  } else {
    int menuRow = -1;
    const auto menuTouch = mappedInput.rowTouch(menuRow, gridTop, metrics.menuRowHeight + metrics.menuSpacing,
                                                renderedMenuCount, 0, INT32_MAX, metrics.menuRowHeight);
    if (menuTouch != MappedInputManager::RowTouch::None) {
      handleMenuTouch(menuTouch, menuRow);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (headerFocused) {
      activityManager.goToSyncAll([] { activityManager.goHome(); });
      return;
    }
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const Rect headerRect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding};
  GUI.drawHeader(renderer, headerRect,
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);
  // Hovering the header (see headerFocused's own comment): a true pixel
  // invert, the same technique QuickPickActivity's own header focus uses.
  if (headerFocused) {
    renderer.invertRect(headerRect.x, headerRect.y, headerRect.width,
                        std::min(HEADER_FOCUS_HIGHLIGHT_HEIGHT, headerRect.height));
  }

  // Themes whose cover card has moved to the top of the Read menu instead
  // (see ReadMenuActivity, which calls drawRecentBookCover() itself) don't
  // draw one here at all.
  if (metrics.homeShowsCoverCard) {
    bool bufferRestored = coverBufferStored && restoreCoverBuffer();

    // Record the tile rect so storeCoverBuffer (called from the theme) knows
    // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
    // instead of the 48 KB full framebuffer the previous bind captured.
    coverRectX = 0;
    coverRectY = metrics.homeTopPadding;
    coverRectW = pageWidth;
    coverRectH = metrics.homeCoverTileHeight;

    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                            recentBooks, headerFocused ? -1 : selectorIndex, coverRendered, coverBufferStored,
                            bufferRestored, std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  // The menu draws the entries the cover tile does not own.
  const int leadingRecents = leadingRecentCount();
  const int renderedCount = static_cast<int>(entries.size()) - leadingRecents;
  const auto& rows = entries;

  const int gridTop = menuTop();
  const int menuHeight = pageHeight - gridTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const Rect menuRect{0, gridTop, pageWidth, menuHeight};
  GUI.drawButtonGrid(
      renderer, menuRect, renderedCount, headerFocused ? -1 : selectorIndex - leadingRecents,
      [&rows, leadingRecents](int index) {
        const char* label = rows[index + leadingRecents].label;
        return std::string(label != nullptr ? label : "");
      },
      [&rows, leadingRecents](int index) { return rows[index + leadingRecents].icon; },
      // Notification-style counts: tasks due today or overdue, habits not yet
      // done today, today's events, today's transactions. Zero means no badge
      // (checked by the theme).
      [&rows, leadingRecents](int index) -> int {
        switch (rows[index + leadingRecents].item) {
          case HomeMenuItem::TASKS:
            return static_cast<int>(TODOIST_TASKS.getDueTodayOrOverdueCount());
          case HomeMenuItem::HABITS: {
            const auto& habits = HABITIFY_HABITS.getHabits();
            return static_cast<int>(
                std::count_if(habits.begin(), habits.end(), [](const HabitifyHabit& h) { return !h.isComplete(); }));
          }
          case HomeMenuItem::CALENDAR:
            return static_cast<int>(GCAL_EVENTS.getTodayCount());
          case HomeMenuItem::BUDGET:
            return static_cast<int>(YNAB_ACCOUNTS.getTodayTransactionCount());
#ifdef ENABLE_BLE_NOTIFY_SPIKE
          case HomeMenuItem::NOTIFICATIONS:
            return static_cast<int>(BLE_NOTIFICATIONS.getUnreadCount());
#endif
          // COMPANION_SCREEN's badge is drawn separately, after its
          // (enlarged) sprite -- see render()'s own comment below for why.
          default:
            return 0;
        }
      });

  // The companion's tile gets its own current pose instead of the static
  // UIIcon::None its entry carries (see buildEntries()) -- drawn after the
  // grid itself, centred on the same point the icon rect the theme left
  // blank for it is centred on, so the pose reads as this tile's own artwork
  // rather than something painted over a real icon.
  for (int i = 0; i < static_cast<int>(entries.size()); i++) {
    if (entries[i].item != HomeMenuItem::COMPANION_SCREEN) continue;
    const Rect iconRect = GUI.getGridTileIconRect(renderer, menuRect, renderedCount, i - leadingRecents);
    // A sprite scaled to literally fit inside that 80x80-ish icon box reads
    // noticeably smaller and thinner than the Lucide line art the other
    // tiles draw there (which fills the box edge to edge): the source sprite
    // is only 34x30, so the largest whole-pixel scale under 80px is 68x60.
    // Given more room instead -- most of the tile's own width (there is far
    // more of it spare than a normal icon needs) and a modest vertical bump
    // (checked against LyraMetrics::values.homeGridTileHeight's own margin
    // around the icon+label block, which comfortably absorbs it) -- centred
    // on iconRect's own centre point, so a bigger pose still lands where a
    // normal icon's would.
    const int columns = std::max(1, metrics.homeGridColumns);
    const int tileWidth = menuRect.width / columns;
    constexpr int SIDE_MARGIN = 24;
    constexpr int VERTICAL_SLACK = 24;
    const int centreX = iconRect.x + iconRect.width / 2;
    const int centreY = iconRect.y + iconRect.height / 2;
    const int budgetWidth = std::max(iconRect.width, tileWidth - SIDE_MARGIN * 2);
    const int budgetHeight = iconRect.height + VERTICAL_SLACK;
    const Rect spriteBudget{centreX - budgetWidth / 2, centreY - budgetHeight / 2, budgetWidth, budgetHeight};
    drawCompanionIcon(spriteBudget);

    // Today's points (tasks + habits combined) -- the same figure the mood
    // ladder itself is evaluated against, so the badge reads as "how the
    // companion's day is going" rather than a to-do count the way the other
    // tiles' badges are. Drawn here, after the sprite, rather than through
    // drawButtonGrid()'s own badgeCount callback: that draws before this
    // loop runs, straddling the icon rect's corner at its nominal 80x80 size,
    // which the enlarged sprite above would then paint straight over. Same
    // position convention (straddling the icon's own top-right corner), just
    // measured from the icon rect the sprite was actually centred on, not
    // the enlarged budget, so the badge sits where the other tiles' own
    // badges do rather than drifting outward with the bigger sprite.
    const int points = static_cast<int>(COMPANION.pointsToday());
    if (points > 0) {
      const std::string badgeText = std::to_string(points);
      const int badgeTextWidth = renderer.getTextWidth(SMALL_FONT_ID, badgeText.c_str());
      const int badgeTextHeight = renderer.getTextHeight(SMALL_FONT_ID);
      const int badgeX = iconRect.x + iconRect.width - badgeTextWidth / 2;
      const int badgeY = iconRect.y - badgeTextHeight / 2;
      renderer.drawText(SMALL_FONT_ID, badgeX, badgeY, badgeText.c_str());
    }
    break;
  }

  // Right1 is always Settings now (see loop()'s own comment); Right2 is Sync
  // All only while the header is focused, Select otherwise.
  const auto labels = mappedInput.mapLabels(tr(STR_SETTINGS_TITLE), headerFocused ? tr(STR_SYNC_ALL) : tr(STR_SELECT),
                                            tr(STR_DIR_PREV), tr(STR_DIR_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
