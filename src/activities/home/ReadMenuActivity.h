#pragma once
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

struct RecentBook;

/**
 * The Read tile's screen: the ways into a book, in one list.
 *
 * The home screen is four tiles and a companion column with no cover card of
 * its own any more -- the card lives here instead, as the leading slot (see
 * recentBooks and render()'s call to the theme's own drawRecentBookCover()),
 * so the three library destinations below it (browse the card, pick up
 * something recent, or send a file over) sit together with the obvious thing
 * rather than beside it on Home.
 */
class ReadMenuActivity final : public Activity {
 public:
  explicit ReadMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Entry {
    const char* label;  // Translated
    UIIcon icon;
    HomeMenuItem item;
  };

  // Built in onEnter: the list is walked every loop() and must not allocate
  // there. Only the OPDS entry is conditional.
  void buildEntries();
  void activateSelected();

  ButtonNavigator buttonNavigator;
  // Side Up/Down jump to the previous/next app in the home grid's own order
  // -- the same shortcut every other app screen has (see
  // OrganizerScreenActivity/QuickPickActivity's own identical block).
  // Row-paging (buttonNavigator's onNext/onPrevious) is still reachable via
  // front Left1/Left2, which NavNext/NavPrevious alias to alongside side
  // Up/Down -- see MappedInputManager::mapButton()'s own NavNext/NavPrevious
  // case. Guarded by a fresh-press check the same way every other screen's
  // is, in case one was already held down when some other gesture left this
  // screen.
  bool upPressSeen = false;
  bool downPressSeen = false;
  std::vector<Entry> entries;
  // The single most recent book, when there is one -- drawn as a leading row
  // ahead of entries[] (selectedIndex 0), one cycle position of its own the
  // same way HomeActivity's companion slot is. Loaded fresh in onEnter()
  // rather than passed in: nothing hands this screen anything today (see
  // ActivityManager::goToReadMenu()), and re-deriving it here is cheap.
  std::vector<RecentBook> recentBooks;
  int selectedIndex = 0;
  // An extra stop bolted onto selectedIndex, the same idiom
  // OrganizerScreenActivity's own headerFocused uses: "previous" from
  // selectedIndex == 0 lands here instead of wrapping to the last entry,
  // "next" from here returns to selectedIndex == 0, "previous" from here
  // wraps to the last entry. Right2 here opens Sync All.
  bool headerFocused = false;
};
