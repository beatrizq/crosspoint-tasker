#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AppOrderActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockOffsetActivity.h"
#include "ClockSyncActivity.h"
#include "CompanionSettingsActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "GCalSettingsActivity.h"
#include "HabitifySettingsActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "TodoistSettingsActivity.h"
#include "YnabSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/BleNotifyRelay.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM,
                                                              StrId::STR_CAT_ORGANIZER};

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();
  organizerSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries)) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Settings merged into "Text Settings"
      // (they stay in the shared list for the web settings API)
      if (setting.inTextSettings) continue;
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
        continue;
      }
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_ORGANIZER) {
      organizerSettings.push_back(setting);
    }
  }

  // Append device-only ACTION items
  if (!BoardConfig::hasTouch()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  // App Order leads the individual apps, as asked: it is a property of the
  // set of apps rather than of any one of them, so it comes first rather
  // than trailing after it.
  organizerSettings.push_back(SettingInfo::Action(StrId::STR_APP_ORDER, SettingAction::AppOrder));
  organizerSettings.push_back(SettingInfo::Action(StrId::STR_TODOIST, SettingAction::Todoist));
  organizerSettings.push_back(SettingInfo::Action(StrId::STR_CALENDAR, SettingAction::GoogleCalendar));
  organizerSettings.push_back(SettingInfo::Action(StrId::STR_YNAB, SettingAction::Ynab));
  organizerSettings.push_back(SettingInfo::Action(StrId::STR_HABITIFY, SettingAction::Habitify));
  organizerSettings.push_back(SettingInfo::Action(StrId::STR_COMPANION, SettingAction::Companion));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  // TODO: Touch devices need their own firmware update path/artifacts before OTA is exposed.
  if (!BoardConfig::hasTouch()) {
    systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  // Not reader-specific (see clockFormat's own comment in SettingsList.h), so
  // the offset editor and manual sync live in Display rather than nested under
  // the reader's status bar screen.
  displaySettings.push_back(SettingInfo::Action(StrId::STR_CLOCK_UTC_OFFSET, SettingAction::ClockOffset));
  displaySettings.push_back(SettingInfo::Action(StrId::STR_CLOCK_SYNC_NOW, SettingAction::ClockSync));

  // Update currentSettings pointer and count for the active category
  applyCategorySelection();
}

// The one place category index maps to a bucket. It used to be written twice -
// here and as a lambda in loop() - and adding the Organizer tab updated only
// this copy, so selecting that tab left the list showing System's entries.
void SettingsActivity::applyCategorySelection() {
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
    case 4:
      currentSettings = &organizerSettings;
      break;
    default:
      currentSettings = &displaySettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;
  headerFocused = false;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed

  // Reclaim WiFi/TLS heap fragmentation the same way every other WiFi-using
  // screen does, now that Network (like the rest of them) pauses BLE first.
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void SettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  bool hasChangedCategory = false;

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Right2)) {
    if (headerFocused) {
      activityManager.goToSyncAll([] { activityManager.goToSettings(); });
      return;
    }
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right1)) {
    // Two-level Back, the same as the organizer screens: from a setting row
    // it first surfaces the cursor to the category tab bar (labelled Back, see
    // render()); only from the tab bar or the header does it leave (labelled
    // Home).
    if (!headerFocused && selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
      return;
    }
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  // Settings is no longer one of the apps on the home grid's own cycle (see
  // HomeActivity::buildEntries()), so the side Up/Down "jump to the adjacent
  // app" shortcut every grid-tile screen has no longer has a coherent
  // meaning here -- there is no "adjacent app" to a screen that isn't part
  // of that cycle any more. Side Up/Down falls through to ordinary row
  // navigation below instead (NavNext/NavPrevious already blends them in
  // with front Left1/Left2).

  const auto& metrics = UITheme::getInstance().getMetrics();
  int tx = 0;
  int ty = 0;
  const int tabTop = metrics.topPadding + metrics.headerHeight;
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight =
      renderer.getScreenHeight() - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                                    metrics.buttonHintsHeight + metrics.buttonHintsGap);
  auto buildTabs = [&]() {
    std::vector<TabInfo> tabs;
    tabs.reserve(categoryCount);
    for (int i = 0; i < categoryCount; i++) {
      tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
    }
    return tabs;
  };
  auto settingIndexFromPoint = [&](const int x, const int y, int& settingIndex) {
    (void)x;
    if (settingsCount <= 0 || y < listTop || y >= listTop + listHeight) return false;
    const int rowStep = GUI.getListRowStep(false);
    if (rowStep <= 0) return false;
    const int pageItems = GUI.getListPageItems(listHeight, false);
    const int selectedRow = std::max(0, selectedSettingIndex - 1);
    const int pageStart = selectedRow / pageItems * pageItems;
    const int row = (y - listTop) / rowStep;
    const int touched = pageStart + row;
    if (row < 0 || row >= pageItems || touched < 0 || touched >= settingsCount) return false;
    settingIndex = touched + 1;
    return true;
  };

  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    int touchedCategory = -1;
    const auto tabs = buildTabs();
    if (GUI.tabIndexFromPoint(renderer, Rect{0, tabTop, renderer.getScreenWidth(), metrics.tabBarHeight}, tabs, tx, ty,
                              touchedCategory)) {
      if (selectedCategoryIndex != touchedCategory || selectedSettingIndex != 0) {
        selectedCategoryIndex = touchedCategory;
        selectedSettingIndex = 0;
        applyCategorySelection();
        requestUpdate();
      }
      return;
    }

    int touchedSetting = -1;
    if (settingIndexFromPoint(tx, ty, touchedSetting)) {
      if (selectedSettingIndex != touchedSetting) {
        selectedSettingIndex = touchedSetting;
        requestUpdate();
      }
      return;
    }
  }

  if (mappedInput.wasScreenTapped(tx, ty)) {
    int tappedCategory = -1;
    const auto tabs = buildTabs();
    if (GUI.tabIndexFromPoint(renderer, Rect{0, tabTop, renderer.getScreenWidth(), metrics.tabBarHeight}, tabs, tx, ty,
                              tappedCategory)) {
      selectedCategoryIndex = tappedCategory;
      selectedSettingIndex = 0;
      applyCategorySelection();
      requestUpdate();
      return;
    }

    int tappedSetting = -1;
    if (settingIndexFromPoint(tx, ty, tappedSetting)) {
      selectedSettingIndex = tappedSetting;
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  // Handle navigation
  const auto& navMetrics = UITheme::getInstance().getMetrics();
  const int settingsListHeight =
      renderer.getScreenHeight() - (navMetrics.topPadding + navMetrics.headerHeight + navMetrics.tabBarHeight +
                                    navMetrics.buttonHintsHeight + navMetrics.buttonHintsGap);
  const int settingsPageItems = GUI.getListPageItems(settingsListHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    // A swipe is a page-jump, not a single step, so it always leaves the
    // header stop (if it was focused) -- same reasoning as
    // OrganizerScreenActivity's own swipe handling.
    headerFocused = false;
    selectedSettingIndex = selectedSettingIndex == 0 ? 1
                                                     : ButtonNavigator::nextPageIndex(
                                                           selectedSettingIndex, settingsCount + 1, settingsPageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    headerFocused = false;
    selectedSettingIndex =
        ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount + 1, settingsPageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (headerFocused) {
      headerFocused = false;
      selectedSettingIndex = 0;
    } else if (selectedSettingIndex == settingsCount) {
      headerFocused = true;
    } else {
      selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (headerFocused) {
      headerFocused = false;
      selectedSettingIndex = settingsCount;
    } else if (selectedSettingIndex == 0) {
      headerFocused = true;
    } else {
      selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    }
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    applyCategorySelection();
  }
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    // Options come from enumStringValues when the labels are runtime strings
    // (character names, SD font families) and from enumValues otherwise. Both
    // must be consulted: a member-pointer enum backed only by string values
    // would otherwise skip the popup and then take a modulo by zero below.
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    if (totalValues == 0) return;

    if (totalValues > 2) {
      const auto valuePtr = setting.valuePtr;
      // Their structure, with this branch's own side effect kept and
      // applyUiSettingChange dropped - that arrives with an upstream commit this
      // branch does not have.
      auto onSelect = [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        SETTINGS.*valuePtr = idx;
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, currentValue, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()),
                         currentValue, std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % totalValues;
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Todoist:
        startActivityForResult(std::make_unique<TodoistSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::GoogleCalendar:
        startActivityForResult(std::make_unique<GCalSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Ynab:
        startActivityForResult(std::make_unique<YnabSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Habitify:
        startActivityForResult(std::make_unique<HabitifySettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Companion:
        startActivityForResult(std::make_unique<CompanionSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::AppOrder:
        startActivityForResult(std::make_unique<AppOrderActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        // Past this point WiFi has been used, so onExit() owes a teardown.
        // Free NimBLE's ~55KB init-time heap reservation before WiFi/TLS need
        // their own headroom -- no matching resume(): onExit() below now
        // reboots once wifiActivated is set, and BleNotifyRelay::begin()
        // re-advertises fresh on the next boot. This screen previously never
        // rebooted after using WiFi at all (a pre-existing gap independent of
        // BLE -- WiFi/TLS heap fragmentation went unreclaimed here too).
        wifiActivated = true;
        BleNotifyRelay::pause();
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ClockOffset:
        // Saves on exit itself, like it did nested under Customise Status Bar.
        startActivityForResult(std::make_unique<ClockOffsetActivity>(renderer, mappedInput), nullptr);
        break;
      case SettingAction::ClockSync:
        startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), nullptr);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  rebuildSettingsLists();
  selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, true, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  const Rect headerRect{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, headerRect, tr(STR_SETTINGS_TITLE), CROSSPOINT_VERSION);
  // Hovering the header (see headerFocused's own comment): a true pixel
  // invert, the same technique QuickPickActivity's own header focus uses.
  if (headerFocused) {
    renderer.invertRect(headerRect.x, headerRect.y, headerRect.width,
                        std::min(HEADER_FOCUS_HIGHLIGHT_HEIGHT, headerRect.height));
  }

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0 && !headerFocused);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.buttonHintsGap)},
      settingsCount, headerFocused ? -1 : selectedSettingIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
          const uint8_t value = SETTINGS.*(setting.valuePtr);
          // Both label sources, and bounds-checked, exactly as the valueGetter
          // branch below already did. A member-pointer enum whose labels are
          // runtime strings - the companion character picker, whose names come
          // from the generated sprite table - leaves enumValues empty, and this
          // read walked off the end of it into the string table.
          if (!setting.enumStringValues.empty()) {
            if (value < setting.enumStringValues.size()) valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::ENUM && setting.valueGetter) {
          const uint8_t value = setting.valueGetter();
          if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
            valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
            char valueBuffer[32];
            if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
              valueText = tr(STR_SLEEP_NEVER);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                       static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
              valueText = valueBuffer;
            }
          } else {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr));
          }
        }
        return valueText;
      },
      true,
      // An ACTION that does nothing is a footnote by construction - nothing else
      // is built that way - so dimming it needs no separate flag.
      [&settings](int i) {
        return settings[i].type == SettingType::ACTION && settings[i].action == SettingAction::None;
      });

  // Draw help text
  const auto confirmLabel =
      headerFocused ? tr(STR_SYNC_ALL)
      : (selectedSettingIndex == 0)
          ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
          : (selectedSettingIndex > 0 && (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP
                 ? tr(STR_SELECT)
                 : tr(STR_TOGGLE));

  // Back on a setting row (it surfaces to the category tab bar), Home once
  // already there or on the header -- see this file's own Right1 handler.
  const bool rowFocused = !headerFocused && selectedSettingIndex > 0;
  const auto labels =
      mappedInput.mapLabels(rowFocused ? tr(STR_BACK) : tr(STR_HOME), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
