#include "OrganizerSleepScreen.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointSettings.h"

namespace organizerSleepScreen {
namespace {

// One SD block-aligned chunk, on the heap rather than the stack: 2KB is an
// order of magnitude past what a task stack here should carry.
constexpr size_t COPY_CHUNK = 2048;

// Switches the sleep mode to CUSTOM. No-op once already CUSTOM.
void switchToCustomMode() {
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) return;
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  SETTINGS.saveToFile();
  LOG_INF("OSLEEP", "Sleep screen mode switched to custom");
}

}  // namespace

bool installCustomWallpaper(const std::string& sourcePath) {
  bool success = false;
  HalFile inFile, outFile;
  if (Storage.openFileForRead("OSLEEP", sourcePath, inFile)) {
    if (Storage.openFileForWrite("OSLEEP", WALLPAPER_PATH, outFile)) {
      auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_CHUNK);
      if (buffer) {
        success = true;
        int bytesRead;
        while ((bytesRead = inFile.read(buffer.get(), COPY_CHUNK)) > 0) {
          if (outFile.write(buffer.get(), static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
            LOG_ERR("OSLEEP", "Short write installing %s", sourcePath.c_str());
            success = false;
            break;
          }
        }
      } else {
        LOG_ERR("OSLEEP", "OOM: %zu bytes", COPY_CHUNK);
      }
      outFile.flush();
    }
  }

  if (!success) {
    LOG_ERR("OSLEEP", "Failed to install %s as the sleep wallpaper", sourcePath.c_str());
    return false;
  }

  switchToCustomMode();
  LOG_INF("OSLEEP", "Sleep screen wallpaper installed from %s", sourcePath.c_str());
  return true;
}

}  // namespace organizerSleepScreen
