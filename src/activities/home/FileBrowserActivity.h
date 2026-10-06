#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via
  // ActivityResult; PickImage = filter to .bmp only and return path via ActivityResult (e.g. a
  // companion mood wallpaper).
  enum class Mode { Books, PickFirmware, PickImage };

 private:
  // Deletion
  bool removeDirFile(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  // An extra stop bolted onto selectorIndex, the same idiom
  // PlannerScreenActivity's own headerFocused uses -- Books mode only (see
  // loop()'s own comment): a firmware/image picker is a narrow, single-
  // purpose flow with no reason to reach Sync All from it.
  bool headerFocused = false;

  bool lockLongPressBack = false;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;
  // Books mode only: when set, Back at root returns to ReadMenuActivity
  // instead of Home -- set only by ActivityManager::goToFileBrowser() on
  // behalf of ReadMenuActivity; every other caller leaves this false.
  bool returnToReadMenu = false;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books, bool returnToReadMenu = false)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        returnToReadMenu(returnToReadMenu),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
