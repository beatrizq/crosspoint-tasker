#pragma once

#include <string>

/**
 * Installs a user-picked image as the sleep screen's Custom wallpaper.
 *
 * The image is copied to COVER_PATH, not to the user's own CUSTOM_PATH:
 * SleepActivity's CUSTOM mode shows the cover when it exists and falls back to
 * CUSTOM_PATH, so picking an image from the viewer ("Set Cover") never
 * overwrites a sleep_custom.bmp the user put on the card themselves. To go
 * back to sleep_custom.bmp, delete the cover file.
 */
namespace plannerSleepScreen {

// Where the image picked with "Set Cover" lives, next to the app's own settings.
inline constexpr char COVER_PATH[] = "/.crosspoint/sleep_cover.bmp";

// The user's own Custom image, placed on the card root by hand.
inline constexpr char CUSTOM_PATH[] = "/sleep_custom.bmp";

/**
 * Copies sourcePath to COVER_PATH and switches the sleep mode to CUSTOM.
 *
 * Returns false if sourcePath could not be read or the copy failed part-way.
 * CUSTOM_PATH is never touched either way.
 */
bool installCustomWallpaper(const std::string& sourcePath);

}  // namespace plannerSleepScreen
