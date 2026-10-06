#include "PlannerScreenActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"

namespace {
// Hold threshold for "sync now" on the Select button (firmware convention).
constexpr unsigned long LONG_PRESS_MS = 1000;

// The dither patterns have period 2 in logical space, so a 1px rule lands on
// either an "on" or an "off" phase depending on its y parity - with an odd row
// height that made the separator appear on every other row. Two pixels covers
// both phases whatever the parity.
constexpr int SEPARATOR_HEIGHT = 2;
}  // namespace

PlannerScreenActivity::PlannerScreenActivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const int initialTab)
    : Activity(std::move(name), renderer, mappedInput), activeTab(initialTab) {}

void PlannerScreenActivity::onEnter() {
  Activity::onEnter();
  loadCaches();
  // Clamped here rather than trusted: initialTab arrives as a plain int from the
  // home tile, and a tab out of range would index the label array.
  if (activeTab < 0 || activeTab >= tabCount()) activeTab = 0;
  selectedIndex = 0;
  headerFocused = false;
  requestUpdate();
}

void PlannerScreenActivity::onExit() {
  Activity::onExit();
  // Same teardown as the KOReader sync screen: drop the association, then
  // reboot silently to home so the WiFi/TLS heap fragmentation goes with it.
  // The mode check keeps a cancelled Wi-Fi picker (radio never brought up)
  // from costing a reboot; a sync that already took the radio down reports
  // WIFI_MODE_NULL by then, so it says so itself.
  if (wifiActivated && (radioTornDown || WiFi.getMode() != WIFI_MODE_NULL)) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// -- metrics ----------------------------------------------------------------

void PlannerScreenActivity::dimText(const int x, const int y, const int fontId, const char* text,
                                    const bool ink) const {
  if (!ink || text == nullptr || text[0] == '\0') return;
  const int width = renderer.getTextWidth(fontId, text);
  const int height = renderer.getLineHeight(fontId);
  for (int py = y; py < y + height; py++) {
    for (int px = x; px < x + width; px++) {
      if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }
  }
}

int PlannerScreenActivity::titleFontId() const {
  // Small is the size these screens always drew at; Large is the only larger UI
  // font there is. The default arm also absorbs a stale persisted value from
  // when this setting had three options.
  return SETTINGS.plannerFontSize == CrossPointSettings::PLANNER_FONT_SMALL ? UI_10_FONT_ID : UI_12_FONT_ID;
}

int PlannerScreenActivity::subtitleFontId() const {
  // One step below the title, so the date stays subordinate to the event.
  return SETTINGS.plannerFontSize == CrossPointSettings::PLANNER_FONT_SMALL ? SMALL_FONT_ID : UI_10_FONT_ID;
}

int PlannerScreenActivity::rowPadding() const {
  // Proportional to the text: a fixed gap that suits 10pt leaves the rows
  // looking cramped once the font grows, which is the point of the setting.
  return std::max(6, renderer.getLineHeight(titleFontId()) * 2 / 5);
}

int PlannerScreenActivity::listRowHeight() const {
  const int titleH = renderer.getLineHeight(titleFontId());
  const int subH = rowsHaveSubtitle() ? renderer.getLineHeight(subtitleFontId()) : 0;
  return titleH + subH + rowPadding();
}

int PlannerScreenActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
}

int PlannerScreenActivity::listHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getScreenHeight() - listTop() - metrics.buttonHintsHeight - metrics.buttonHintsGap;
}

int PlannerScreenActivity::pageItems() const { return std::max(1, listHeight() / std::max(1, listRowHeight())); }

// -- tabs -------------------------------------------------------------------

void PlannerScreenActivity::setTab(const int index) {
  if (index < 0 || index >= tabCount()) return;
  activeTab = index;
}

void PlannerScreenActivity::switchTab(const int next) {
  if (activeTab == next || next < 0 || next >= tabCount()) return;
  activeTab = next;
  // Row indices mean different things per tab; start at the top of the new one.
  selectedIndex = 0;
  state = State::LIST;
  statusMessage = nullptr;
  onTabChanged();
}

// -- sync -------------------------------------------------------------------

void PlannerScreenActivity::tearDownRadio() {
  // Through WiFi.mode(WIFI_OFF) rather than by stopping the driver directly.
  // A bare stop leaves the Arduino layer believing the radio is still running:
  // the flag it gates esp_wifi_start() on stays set, and WiFi.mode() then sees
  // the mode it was asked for and returns early. The next sync in the same
  // session scans and connects against a stopped driver - the saved network
  // fails, no networks are found - and only a reboot clears it. Going through
  // WiFi.mode() stops and deinitialises the driver, which hands back more heap
  // than a bare stop, and lets the next sync bring it up from scratch.
  WiFi.mode(WIFI_OFF);
  radioTornDown = true;
}

void PlannerScreenActivity::failSync(const char* message) {
  {
    RenderLock lock(*this);
    state = State::FAILED;
    statusMessage = message;
  }
  requestUpdate(true);
}

void PlannerScreenActivity::runSync(std::function<void()> work) {
  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  requestUpdate();

  // Past this point every path uses WiFi, so onExit() owes a teardown.
  wifiActivated = true;

  // Same reasoning as SyncAllActivity's own pause() call: free NimBLE's
  // ~55KB init-time heap reservation before WiFi/TLS need their own headroom.
  // No matching resume(): onExit() below always reboots once wifiActivated is
  // set, and BleNotifyRelay::begin() re-advertises fresh on the next boot.
  BleNotifyRelay::pause();

  if (WiFi.status() == WL_CONNECTED) {
    work();
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this, work = std::move(work)](const ActivityResult& result) {
                           if (result.isCancelled) {
                             failSync(tr(STR_WIFI_CONN_FAILED));
                             return;
                           }
                           work();
                         });
}

void PlannerScreenActivity::finishSync(const char* failureMessage) {
  {
    RenderLock lock(*this);
    if (failureMessage == nullptr) {
      state = State::LIST;
      statusMessage = nullptr;
      selectedIndex = rowCount() > 0 ? 1 : 0;
    } else {
      state = State::FAILED;
      statusMessage = failureMessage;
    }
  }
  requestUpdate(true);
}

// -- input ------------------------------------------------------------------

void PlannerScreenActivity::loop() {
  if (state == State::SYNCING) return;  // ignore input while the sync blocks

  // A press seen here is a fresh one, so nothing is owed any more.
  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) swallowBackRelease = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right1)) {
    if (swallowBackRelease) {
      // The tail of the press that cancelled a popup pushed from this screen.
      // Acting on it would leave the screen entirely instead of just closing
      // the popup that press already closed.
      swallowBackRelease = false;
      return;
    }
    // Two-level Back: from a row it first surfaces the cursor to the tab bar
    // (labelled Back, see render()); only from the tab bar or the header does
    // it leave the screen (labelled Home).
    if (state == State::LIST && !headerFocused && selectedIndex > 0) {
      selectedIndex = 0;
      requestUpdate();
      return;
    }
    onGoHome(homeItem());
    return;
  }

  // A press seen here is a fresh one, so nothing is owed any more.
  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) swallowConfirmRelease = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right2)) {
    if (swallowConfirmRelease) {
      // The tail of the press that answered the confirmation prompt. Acting on
      // it would reopen the prompt, and cancelling would reopen it again.
      swallowConfirmRelease = false;
      return;
    }
    if (headerFocused) {
      // Captured by value, not this: appId() has to be read now, while this
      // screen still exists -- goToSyncAll()'s own replaceActivity() call
      // destroys it before the lambda ever runs.
      activityManager.goToSyncAll([id = appId()] { activityManager.goToApp(id); });
      return;
    }
    if (state == State::FAILED) {
      // Dismiss the failure message and fall back to whatever is cached.
      {
        RenderLock lock(*this);
        state = State::LIST;
        statusMessage = nullptr;
      }
      requestUpdate(true);
      return;
    }
    if (selectedIndex == 0) {
      if (mappedInput.getHeldTime() >= LONG_PRESS_MS || tabCount() <= 1) {
        startSync();
        return;
      }
      {
        RenderLock lock(*this);
        switchTab(nextTab());
      }
      requestUpdate(true);
      return;
    }
    if (mappedInput.getHeldTime() < LONG_PRESS_MS) onRowConfirm();
    return;
  }

  // Side Up/Down: jump to the previous/next app in the home grid's own order,
  // from wherever the cursor already is -- the same shortcut every app
  // screen has (see QuickPickActivity/SettingsActivity's own identical
  // block). Tab-switching (this screen's own tab bar) is still reachable the
  // slower way: move the selection up to the tab bar and press Select to
  // cycle it. Independent of the front buttons' own Up/Down (row paging)
  // below; a fresh press each, same guard reasoning as Back/Confirm above.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) upPressSeen = true;
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) downPressSeen = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upPressSeen) activityManager.goToApp(homeAppOrder::adjacentVisibleApp(appId(), /*forward=*/false));
    upPressSeen = false;
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downPressSeen) activityManager.goToApp(homeAppOrder::adjacentVisibleApp(appId(), /*forward=*/true));
    downPressSeen = false;
    return;
  }
  // Swallowed for as long as either is held, on the press frame and every
  // frame after: buttonNavigator's row paging below reacts to the logical
  // NavNext/NavPrevious buttons, which are side Up/Down blended with front
  // Left/Right (see MappedInputManager::mapButton). Without this, a side
  // press would page a row immediately (NavNext/NavPrevious firing on the
  // same press) and only switch the tab afterwards, on release.
  if (mappedInput.isPressed(MappedInputManager::Button::Up) ||
      mappedInput.isPressed(MappedInputManager::Button::Down)) {
    return;
  }

  if (state != State::LIST) return;

  // Index 0 is the tab bar, so the navigable range is one longer than the list.
  const int navCount = rowCount() + 1;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = listTop();
  const int height = listHeight();
  const int rowHeight = std::max(1, listRowHeight());
  const int perPage = pageItems();

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    // Tabs first: they sit above the list and share the same tap stream.
    std::vector<TabInfo> tabs;
    tabs.reserve(tabCount());
    for (int i = 0; i < tabCount(); i++) tabs.push_back(TabInfo{tabLabel(i), i == activeTab});
    int tappedTab = -1;
    const int tabTop = metrics.topPadding + metrics.headerHeight;
    if (GUI.tabIndexFromPoint(renderer, Rect{0, tabTop, renderer.getScreenWidth(), metrics.tabBarHeight}, tabs, tx, ty,
                              tappedTab)) {
      {
        RenderLock lock(*this);
        switchTab(tappedTab);
      }
      requestUpdate(true);
      return;
    }
    // Rows are hit-tested against this screen's own row height, not the theme's:
    // the list is drawn here so the font size can follow the setting.
    if (ty >= top && ty < top + height && rowCount() > 0) {
      const int pageStart = selectedRow() < 0 ? 0 : (selectedRow() / perPage) * perPage;
      const int tapped = pageStart + (ty - top) / rowHeight;
      if (tapped >= 0 && tapped < rowCount()) {
        selectedIndex = tapped + 1;
        requestUpdate();
      }
      return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    // A swipe is a page-jump, not a single step, so it always leaves the
    // header stop (if it was focused) rather than trying to fold it into
    // the page math below.
    headerFocused = false;
    selectedIndex = selectedIndex == 0 ? std::min(1, navCount - 1)
                                       : ButtonNavigator::nextPageIndex(selectedIndex, navCount, perPage);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    headerFocused = false;
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, navCount, perPage);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this, navCount] {
    if (headerFocused) {
      headerFocused = false;
      selectedIndex = 0;
    } else if (selectedIndex == navCount - 1) {
      headerFocused = true;
    } else {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, navCount);
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, navCount] {
    if (headerFocused) {
      headerFocused = false;
      selectedIndex = navCount - 1;
    } else if (selectedIndex == 0) {
      headerFocused = true;
    } else {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, navCount);
    }
    requestUpdate();
  });
}

// -- render -----------------------------------------------------------------

void PlannerScreenActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Header: the screen's name, with the active tab's own summary on the right.
  char status[64];
  status[0] = '\0';
  formatStatus(status, sizeof(status));
  const Rect headerRect{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, screenTitle(), status[0] == '\0' ? nullptr : status);
  // Hovering the header (see headerFocused's own comment): a true pixel
  // invert, the same technique QuickPickActivity's own header focus uses.
  if (headerFocused) {
    renderer.invertRect(headerRect.x, headerRect.y, headerRect.width,
                        std::min(HEADER_FOCUS_HIGHLIGHT_HEIGHT, headerRect.height));
  }

  std::vector<TabInfo> tabs;
  tabs.reserve(tabCount());
  for (int i = 0; i < tabCount(); i++) tabs.push_back(TabInfo{tabLabel(i), i == activeTab});
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedIndex == 0 && !headerFocused);

  const int top = listTop();
  const int itemCount = rowCount();

  // One centered message per non-list state; the failure message covers the
  // list until dismissed.
  if (state == State::SYNCING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, syncingMessage());
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage);
  } else if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, emptyMessage());
    if (tabCount() > 1) {
      renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + renderer.getLineHeight(UI_10_FONT_ID) * 3 / 2,
                                tr(STR_PLANNER_HOLD_TO_SYNC));
    }
  } else {
    // Drawn by the subclass rather than through GUI.drawList so the row font
    // follows SETTINGS.plannerFontSize; the theme's list draws at a fixed
    // size. The base owns the band, the fill and the separator; the subclass
    // owns what goes inside.
    const int rowHeight = std::max(1, listRowHeight());
    const int rowPad = rowPadding();
    const int perPage = pageItems();
    const int pageStart = selectedRow() < 0 ? 0 : (selectedRow() / perPage) * perPage;
    const int textX = metrics.contentSidePadding;
    const int textWidth = pageWidth - metrics.contentSidePadding * 2;

    for (int row = 0; row < perPage; row++) {
      const int index = pageStart + row;
      if (index >= itemCount) break;
      const int rowY = top + row * rowHeight;
      const bool selected = !headerFocused && index == selectedRow();

      if (selected) {
        renderer.fillRect(0, rowY, pageWidth, rowHeight);
      }

      drawRow(RowLayout{index, textX, rowY, textWidth, rowHeight, rowY + rowPad / 2, titleFontId(), subtitleFontId(),
                        // Selected rows invert: the fill is black, so the text
                        // has to be white.
                        !selected});

      // Soft rule between entries, so a wrapped title cannot be mistaken for the
      // start of the next one. Dithered rather than solid: a black hairline
      // carries more weight on e-ink than the text it is separating.
      //
      // Skipped either side of the selected row, whose fill already bounds it,
      // and after the last row on the page, where it would underline nothing.
      const bool nextSelected = (index + 1) == selectedRow();
      const bool lastOnPage = row + 1 >= perPage || index + 1 >= itemCount;
      if (!selected && !nextSelected && !lastOnPage) {
        renderer.fillRectDither(textX, rowY + rowHeight - SEPARATOR_HEIGHT, textWidth, SEPARATOR_HEIGHT,
                                Color::LightGray);
      }
    }
  }

  const char* confirmLabel;
  if (headerFocused) {
    confirmLabel = tr(STR_SYNC_ALL);
  } else if (state == State::SYNCING) {
    confirmLabel = "";
  } else if (state == State::FAILED) {
    confirmLabel = tr(STR_OK_BUTTON);
  } else if (selectedIndex == 0) {
    // With the tabs focused, Select moves to the next one - so it is labelled
    // with where it goes rather than with what it is. With nowhere to go it
    // syncs, and says so.
    confirmLabel = tabCount() > 1 ? tabLabel(nextTab()) : tr(STR_PLANNER_SYNC_NOW);
  } else if (itemCount > 0) {
    confirmLabel = rowConfirmLabel();
  } else {
    confirmLabel = "";
  }
  const bool navigable = state == State::LIST;
  // Back on a row (it surfaces to the tab bar), Home once already there or on
  // the header -- see loop()'s own Right1 handler.
  const bool rowFocused = navigable && !headerFocused && selectedIndex > 0;
  const auto labels = mappedInput.mapLabels(rowFocused ? tr(STR_BACK) : tr(STR_HOME), confirmLabel,
                                            navigable ? tr(STR_DIR_UP) : "", navigable ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
