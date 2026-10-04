#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class RecentBooksActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  // An extra stop bolted onto selectorIndex, the same idiom
  // OrganizerScreenActivity's own headerFocused uses.
  bool headerFocused = false;

  // Set when a long-press has fired; input is swallowed until Confirm is released
  // again so the release doesn't also open the book.
  bool longPressFired = false;

  // When set, Back returns to ReadMenuActivity instead of Home -- set only by
  // ActivityManager::goToRecentBooks() on behalf of ReadMenuActivity.
  bool returnToReadMenu = false;

  // Recent tab state
  std::vector<RecentBook> recentBooks;

  // Data loading
  void loadRecentBooks();

  // Show an OK/Cancel prompt to remove the given book from the Recent Books list.
  void promptRemoveBook(const std::string& path, const std::string& title);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool returnToReadMenu = false)
      : Activity("RecentBooks", renderer, mappedInput), returnToReadMenu(returnToReadMenu) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
