#pragma once

#include <string>

/**
 * Installs a user-picked image as the sleep screen's Custom wallpaper.
 *
 * The image is copied to WALLPAPER_PATH, not /sleep.bmp: SleepActivity's
 * CUSTOM mode shows the wallpaper when it exists and falls back to the user's
 * own /sleep.bmp, so picking an image from the viewer ("Set Cover") never
 * overwrites a sleep.bmp the user put on the card themselves. To go back to
 * sleep.bmp, delete the wallpaper file.
 */
namespace organizerSleepScreen {

// Where the picked wallpaper lives, next to the app's own settings.
inline constexpr char WALLPAPER_PATH[] = "/.crosspoint/sleep_cover.bmp";

/**
 * Copies sourcePath to WALLPAPER_PATH and switches the sleep mode to CUSTOM.
 *
 * Returns false if sourcePath could not be read or the copy failed part-way.
 * /sleep.bmp is never touched either way.
 */
bool installCustomWallpaper(const std::string& sourcePath);

}  // namespace organizerSleepScreen
