#pragma once
#include <ArduinoJson.h>
#include <Epub/ReaderRenderSpec.h>
#include <PersistableStore.h>

#include <cstdint>

class CrossPointSettings : public PersistableStore<CrossPointSettings> {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  friend class PersistableStore<CrossPointSettings>;

 public:
  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    COVER_CUSTOM = 4,
    BLANK = 5,
    QUICK_RESUME = 6,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };

  enum STATUS_BAR_PROGRESS_BAR {
    BOOK_PROGRESS = 0,
    CHAPTER_PROGRESS = 1,
    HIDE_PROGRESS = 2,
    STATUS_BAR_PROGRESS_BAR_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR_THICKNESS {
    PROGRESS_BAR_THIN = 0,
    PROGRESS_BAR_NORMAL = 1,
    PROGRESS_BAR_THICK = 2,
    STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT
  };
  enum STATUS_BAR_TITLE { BOOK_TITLE = 0, CHAPTER_TITLE = 1, HIDE_TITLE = 2, STATUS_BAR_TITLE_COUNT };
  enum XTC_STATUS_BAR_MODE {
    XTC_STATUS_BAR_HIDE = 0,
    XTC_STATUS_BAR_BOTTOM = 1,
    XTC_STATUS_BAR_TOP = 2,
    XTC_STATUS_BAR_MODE_COUNT
  };

  enum STATUS_BAR_CLOCK_MODE {
    STATUS_BAR_CLOCK_HIDE = 0,
    STATUS_BAR_CLOCK_RIGHT = 1,
    STATUS_BAR_CLOCK_LEFT = 2,
    STATUS_BAR_CLOCK_MODE_COUNT
  };

  enum ORIENTATION {
    PORTRAIT = 0,       // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,   // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,       // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3,  // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button layout options (legacy)
  // Default: Right1, Right2, Left1, Left2
  // Swapped: Left1, Left2, Right1, Right2
  enum FRONT_BUTTON_LAYOUT {
    RIGHT1_RIGHT2_LEFT1_LEFT2 = 0,
    LEFT1_LEFT2_RIGHT1_RIGHT2 = 1,
    LEFT1_RIGHT1_RIGHT2_LEFT2 = 2,
    RIGHT1_RIGHT2_LEFT2_LEFT1 = 3,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping).
  //
  // These raw indices (0-3) are HalGPIO::BTN_BACK/BTN_CONFIRM/BTN_LEFT/
  // BTN_RIGHT -- freeink-sdk's own names, unrelated to this device's actual
  // physical layout (see MappedInputManager's own header comment for the
  // Left1/Left2/Right1/Right2 scheme these hardware indices are the default
  // values for). Every theme's drawButtonHints() draws its btn1/btn2 in a
  // left-hand screen group and btn3/btn4 in a right-hand one (BaseTheme,
  // LyraTheme, RoundedRaffTheme all agree), and MappedInputManager::
  // mapFrontLabels() builds those four labels in fixed raw-hardware order
  // (BACK, CONFIRM, LEFT, RIGHT) -- so hw index 0/1 render in the left-hand
  // group and 2/3 in the right-hand one. FRONT_HW_LEFT1/LEFT2 must therefore
  // default to 0/1 and FRONT_HW_RIGHT1/RIGHT2 to 2/3, confirmed against the
  // real device: the reverse (this enum's own previous values) put Right1/
  // Right2 ("Apps"/"Select") on the physical left pair and Left1/Left2 ("Up"/
  // "Down") on the physical right pair. A user's own remap (ButtonRemapActivity
  // captures a literal press via getPressedFrontButton(), not one of these
  // symbols) is unaffected either way.
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_LEFT1 = 0,
    FRONT_HW_LEFT2 = 1,
    FRONT_HW_RIGHT1 = 2,
    FRONT_HW_RIGHT2 = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Up = Previous, Down = Next
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, SIDE_BUTTON_LAYOUT_COUNT };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName)
  enum FONT_FAMILY { NOTOSERIF = 0, NOTOSANS = 1, FONT_FAMILY_COUNT };
  static constexpr uint8_t LEGACY_OPENDYSLEXIC = 2;
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Reader font size is a point size, not an enum slot — see fontPointSize.
  // Legacy 1.4-and-earlier files stored a 0..3 SMALL/MEDIUM/LARGE/EXTRA_LARGE
  // slot; fromJson() folds that range up (see LEGACY_FONT_SIZE_MAX).
  static constexpr uint8_t LEGACY_FONT_SIZE_MAX = 3;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 14;
  enum LINE_COMPRESSION { TIGHT = 0, NORMAL = 1, WIDE = 2, LINE_COMPRESSION_COUNT };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };

  // E-ink refresh frequency (pages between full refreshes)
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_FREQUENCY_COUNT
  };

  // Short power button press actions
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    FORCE_REFRESH = 3,
    FOOTNOTES = 4,
    // A genuine short press (released before the ordinary hold-to-sleep
    // duration) sleeps the device using Quick Resume specifically, regardless
    // of the chosen Sleep Screen mode; holding it past that duration still
    // sleeps normally, with whichever Sleep Screen mode is set -- see the
    // release-triggered branch in main.cpp's loop() and
    // enterDeepSleep()'s own forceQuickResume parameter.
    SHORT_PWRBTN_QUICK_RESUME = 5,
    SHORT_PWRBTN_COUNT
  };

  // Long-press Confirm action while reading an EPUB. The setting cycles through these values.
  // Persisted in settings.json by index: any new function (e.g. dictionary, bookmark) MUST use a
  // value >= 2 and be appended at the END of the enumValues array in SettingsList.h, otherwise the
  // stored indices shift and existing saves are silently misinterpreted.
  enum LONG_PRESS_MENU_FUNCTION {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LONG_PRESS_MENU_FUNCTION_COUNT
  };

  // Per-app display names. Empty means "use the app's own name", which is what
  // every one of these ships as; a value here replaces it on the home grid and on
  // the app's own screen, while Settings keeps listing the service so an app is
  // always findable by the name its account is with. Persisted via category-less
  // SettingInfo::Strings in SettingsList.h - edited from each app's settings
  // screen, so they stay out of the on-device Settings list.
  //
  // 24 bytes each: a name longer than that is truncated by the tile anyway.
  char tasksNickname[24] = "";
  char calendarNickname[24] = "";
  // Same idea, for the companion -- falls back to its own built-in character
  // name (CompanionTracker::displayName()) rather than an app's service name,
  // since the companion has no account behind it to keep listing.
  char companionNickname[24] = "";
  // Home grid app order: one digit per app id, left to right (see
  // util/HomeAppOrder.h). Persisted via a category-less SettingInfo::String in
  // SettingsList.h, so it stays out of the on-device Settings screen - it is
  // edited from the App Order screen. An unreadable value falls back to the
  // built-in order rather than emptying the home grid.
  char homeAppOrder[16] = "01234";
  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long press behavior
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    OFF = 0,
    CHAPTER_SKIP = 1,
    ORIENTATION_CHANGE = 2,
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };

  // UI Theme
  enum UI_THEME { CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3 };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  enum TILT_PAGE_TURN { TILT_OFF = 0, TILT_NORMAL = 1, TILT_NVERTED = 2, TILT_PAGE_TURN_COUNT };

  enum TOUCH_READER_CONTROLS { TOUCH_READER_OFF = 0, TOUCH_READER_ON = 1, TOUCH_READER_CONTROLS_COUNT };

  // Which manual/automatic sleeps use Quick Resume (a fast partial refresh that
  // keeps the current screen visible with a moon icon) instead of the chosen
  // Sleep Screen mode's own image. Applies only when Sleep Screen itself is not
  // already set to Quick Resume outright, where every sleep already does this
  // regardless (see enterDeepSleep()'s own isQuickResumeSleep).
  // Which sleep image an automatic, inactivity-timeout sleep uses -- the manual,
  // power-button case is a separate setting (shortPwrBtn's own SHORT_PWRBTN_QUICK_RESUME
  // value). Not consulted at all when Sleep Screen itself is already set to
  // Quick Resume, where every sleep already does this regardless (see
  // enterDeepSleep()'s own isQuickResumeSleep).
  enum TIMEOUT_SLEEP_SCREEN { TIMEOUT_SLEEP = 0, TIMEOUT_QUICK_RESUME = 1, TIMEOUT_SLEEP_SCREEN_COUNT };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  // Font size for the Organizer screens (tasks and calendar). One setting for
  // both tabs: they are read the same way, at the same distance, and splitting
  // them would be two settings answering one question.
  enum ORGANIZER_FONT_SIZE { ORGANIZER_FONT_SMALL = 0, ORGANIZER_FONT_LARGE, ORGANIZER_FONT_SIZE_COUNT };
  uint8_t organizerFontSize = ORGANIZER_FONT_SMALL;

  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Rotates the sleep screen 180 degrees, for a device held or mounted upside
  // down (see SleepActivity::onEnter()). Not the same as the cover filter's
  // Inverted option above, which flips black and white rather than the
  // orientation.
  uint8_t sleepScreenInvert = 0;
  // Status bar settings
  uint8_t statusBarChapterPageCount = 1;
  uint8_t statusBarBookProgressPercentage = 1;
  uint8_t statusBarProgressBar = HIDE_PROGRESS;
  uint8_t statusBarProgressBarThickness = PROGRESS_BAR_NORMAL;
  uint8_t statusBarTitle = CHAPTER_TITLE;
  uint8_t statusBarBattery = 1;
  uint8_t xtcStatusBarMode = XTC_STATUS_BAR_HIDE;
  // Clock display in status bar (X3 only, requires DS3231 RTC)
  uint8_t statusBarClock = STATUS_BAR_CLOCK_HIDE;
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t.
  // Value 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
  // Quarter-hour granularity supports oddball zones like Nepal (+5:45) and Chatham (+12:45).
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  uint8_t textAntiAliasing = 1;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts (front layout retained for migration only)
  uint8_t frontButtonLayout = RIGHT1_RIGHT2_LEFT1_LEFT2;
  uint8_t sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonFollowOrientation = 0;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonRight1 = FRONT_HW_RIGHT1;
  uint8_t frontButtonRight2 = FRONT_HW_RIGHT2;
  uint8_t frontButtonLeft1 = FRONT_HW_LEFT1;
  uint8_t frontButtonLeft2 = FRONT_HW_LEFT2;
  // Reader font settings
  uint8_t fontFamily = NOTOSERIF;
  // Point size of the reader font. Only sizes the active family actually ships
  // are selectable; SdCardFontSystem::ensureLoaded() snaps this to the nearest
  // available size (and persists the snap) whenever the family changes.
  uint8_t fontPointSize = DEFAULT_FONT_POINT_SIZE;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes). Legacy sleepTimeout enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  uint8_t hyphenationEnabled = 0;

  // Reader screen margin settings
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 40;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 5;
  uint8_t screenMargin = SCREEN_MARGIN_MIN;
  // OPDS download destination folder ("" = SD root). Global; edited from the
  // OPDS server list. Persisted via a category-less SettingInfo::String in
  // SettingsList.h, so it stays out of the on-device Settings screen.
  char opdsDownloadFolder[64] = "";
  // On-disk filename format for OPDS downloads (0=Author-Title default, 1=Title-Author,
  // 2=Title). See OpdsFilenameFormat. Persisted via a category-less SettingInfo::Enum,
  // edited from the OPDS server list; hidden from the on-device Settings screen.
  uint8_t opdsFilenameFormat = 0;
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Long-press page turn button behavior
  uint8_t longPressButtonBehavior = OFF;
  // Long-press Confirm function in EPUB reader (cycles through LONG_PRESS_MENU_FUNCTION values).
  // Defaults to Disabled so shortcut-based bookmark toggling remains opt-in.
  uint8_t longPressMenuFunction = LP_MENU_DISABLED;
  // UI Theme
  uint8_t uiTheme = LYRA;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // Index into companion::COMPANION_SPRITES (0 = the first companion).
  // Persisted numerically, so sprites/order.txt is append-only.
  uint8_t companionId = 0;
  // Whether the Companion screen draws the companion figure and its speech
  // bubble. Off hands that space to the Tasks section below it and drops the
  // figure from the focus loop; the alerts and calendar rows above are unaffected.
  uint8_t showCompanion = 1;
  // Sleep window the companion shows the Sleeping mood during, local wall-clock
  // time. Default 22:00-07:00. May wrap past midnight (start > end); start ==
  // end means the window never applies (24 awake hours, not 24 asleep).
  uint8_t companionSleepStartHour = 22;
  uint8_t companionSleepStartMinute = 0;
  uint8_t companionSleepEndHour = 7;
  uint8_t companionSleepEndMinute = 0;
  // Mood ladder tuning -- see MoodThresholds in lib/Companion/CompanionMood.h.
  // Tasks completed today needed for the top tier, Amazed. Must stay above
  // companionHappyPoints or Amazed becomes unreachable.
  uint8_t companionAmazedPoints = 8;
  // Tasks completed today needed for the Happy tier.
  // Must stay above companionSatisfiedPoints (and below companionAmazedPoints)
  // or Happy becomes unreachable -- CompanionSettingsActivity clamps these on
  // every edit, so they only ever land here already valid; CompanionTracker
  // clamps again on read as a backstop against a hand-edited settings.json.
  uint8_t companionHappyPoints = 3;
  // Tasks completed today needed to count as "did something"
  // (Satisfied) rather than starting the Cranky/Neglected decay. Must stay
  // >= 1 -- 0 would mean every day, even an empty one, already qualifies,
  // making the decay tiers unreachable.
  uint8_t companionSatisfiedPoints = 1;
  // Quiet days (no qualifying day) at or above which the mood bottoms out at
  // Neglected rather than Cranky. Must stay >= 1, for the same reachability
  // reason as companionSatisfiedPoints.
  uint8_t companionNeglectedDays = 3;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // Dictionary folder name under /dictionaries (empty = no dictionary)
  char dictionaryName[32] = "";
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on)
  uint8_t removeReadBooksFromRecents = 0;
  // Move epub to /Read/ folder on SD card when finished (0 = disabled, 1 = enabled)
  uint8_t moveFinishedToReadFolder = 0;
  // Short press Back goes to file browser instead of home (0 = disabled, 1 = enabled)
  uint8_t backShortToFileBrowser = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Tilt-based page turning (X3 only — requires QMI8658 IMU)
  uint8_t tiltPageTurn = TILT_OFF;
  // Touch screen reader zones/gestures on boards with a touch controller.
  uint8_t touchReaderControls = TOUCH_READER_ON;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  // See TIMEOUT_SLEEP_SCREEN's own comment for what each value means.
  uint8_t timeoutSleepScreen = TIMEOUT_SLEEP;

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  int getReaderFontId() const;

  // Drop the SD font selection and fall back to the built-in family. The reader
  // point size comes back into BUILTIN_READER_POINT_SIZES with it, since that is
  // the only set a built-in family ships — otherwise the settings UI would keep
  // offering a size nothing renders at. Both fields are persisted in one write.
  void clearSdFontFamily();

  // Resolved status-bar composition. Consumers read the spec; only settings
  // editors read the raw fields.
  //
  // Deliberately NOT built under storeMutex: every field it reads is a single
  // byte, so a concurrent settings write can never produce a corrupt value —
  // only a snapshot mixing pre- and post-change fields. That costs at most one
  // e-ink frame drawn with a mixed status bar, which self-corrects on the next
  // refresh. Locking here would instead put a mutex on the render path and
  // stall it behind the SD write inside saveToFile(). Don't add one back.
  struct StatusBarSpec {
    bool showChapterPageCount = false;
    bool showBookProgressPercent = false;
    uint8_t titleMode = HIDE_TITLE;  // STATUS_BAR_TITLE
    bool showBattery = false;
    bool showBatteryPercent = false;
    uint8_t clockMode = STATUS_BAR_CLOCK_HIDE;  // STATUS_BAR_CLOCK_MODE
    bool clock12h = false;
    uint8_t clockUtcOffsetQ = 48;             // 48 = UTC+0
    uint8_t progressBarMode = HIDE_PROGRESS;  // STATUS_BAR_PROGRESS_BAR
    uint8_t progressBarHeightPx = 0;          // (thickness+1)*2; 0 when the bar is hidden
    uint8_t xtcMode = XTC_STATUS_BAR_HIDE;    // XTC_STATUS_BAR_MODE

    bool showsProgressBar() const { return progressBarMode != HIDE_PROGRESS; }
    bool showsTitle() const { return titleMode != HIDE_TITLE; }
    bool showsClock() const { return clockMode != STATUS_BAR_CLOCK_HIDE; }
    // Visibility of the text lane. Clock hardware presence is the caller's
    // concern: pass halClock.isAvailable(), or true for layout reservation.
    bool textLaneVisible(bool clockAvailable) const {
      return showChapterPageCount || showBookProgressPercent || showsTitle() || showBattery ||
             (showsClock() && clockAvailable);
    }
  };
  StatusBarSpec statusBarSpec() const;

  // Resolved text-rendering configuration for the Epub layout engine. The
  // viewport is renderer/orientation-derived, so the caller supplies it —
  // passing it in keeps a spec from ever existing in a half-filled state.
  // Unlocked for the same reason as statusBarSpec(); see the note above.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;

  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
