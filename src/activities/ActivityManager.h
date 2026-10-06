#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "activities/network/FileTransferReturn.h"
#include "util/HomeAppOrder.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
class RenderLock;  // forward declaration

// TASKS and CALENDAR are a tile and a screen each.
enum class HomeMenuItem {
  NONE,
  READ_MENU,
  FILE_BROWSER,
  RECENTS,
  TASKS,
  CALENDAR,
  OPDS_BROWSER,
  FILE_TRANSFER,
  SETTINGS_MENU,
  // Named NOTIFICATIONS, not BLE_NOTIFICATIONS: the latter collides with the
  // BLE_NOTIFICATIONS macro (BleNotificationQueue.h's
  // #define BLE_NOTIFICATIONS BleNotificationQueue::getInstance()) -- the
  // preprocessor rewrites it even after "HomeMenuItem::", which does not fail
  // quietly (it errors on the resulting bogus qualified name).
  NOTIFICATIONS,
  // Named COMPANION_SCREEN, not COMPANION: the latter collides with the
  // COMPANION macro (CompanionTracker.h's
  // #define COMPANION CompanionTracker::getInstance()), the same problem
  // NOTIFICATIONS above already has its own comment about. Opens
  // QuickPickActivity, the same as any other home grid tile -- see
  // HomeActivity::activateCompanion().
  COMPANION_SCREEN
};

/**
 * ActivityManager
 *
 * This mirrors the same concept of Activity in Android, where an activity represents a single screen of the UI. The
 * manager is responsible for launching activities, and ensuring that only one activity is active at a time.
 *
 * It also provides a stack mechanism to allow activities to launch sub-activities and get back the results when the
 * sub-activity is done. For example, the WebServer activity can launch a WifiSelect activity to let the user choose a
 * wifi network, and get back the selected network when the user is done.
 *
 * Main differences from Android's ActivityManager:
 * - No onPause/onResume, since we don't have a concept of background activities
 * - onActivityResult is implemented via a callback instead of a separate method, for simplicity
 */
class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;

  void exitActivity(const RenderLock& lock);

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;

  // Mutex to protect rendering operations from race conditions
  // Must only be used via RenderLock
  SemaphoreHandle_t renderingMutex = nullptr;

  // Whether to trigger a render after the current loop()
  // This variable must only be set by the main loop, to avoid race conditions
  std::atomic<bool> requestedUpdate{false};

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
    assert(renderingMutex != nullptr && "Failed to create rendering mutex");
    stackActivities.reserve(10);
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  void begin();
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // goTo... functions are convenient wrapper for replaceActivity()
  // returnToReadMenu routes that screen's own Back button to ReadMenuActivity
  // instead of Home -- set only by ReadMenuActivity itself, since every other
  // caller of these four still expects Back to land on Home as before.
  void goToFileTransfer(FileTransferReturn returnTo = FileTransferReturn::Home);
  void goToSettings();
  void goToFileBrowser(std::string path = {}, bool returnToReadMenu = false);
  void goToRecentBooks(bool returnToReadMenu = false);
  // initialTab is an index into the target screen's tab bar; the header cannot
  // name those types without pulling the activities in. Out-of-range values are
  // clamped to the first tab by PlannerScreenActivity::onEnter().
  // selectTaskId, when non-empty, lands the screen on that specific task's row
  // instead of row 0 -- see TasksActivity's own constructor comment.
  void goToTasks(uint8_t initialTab = 0, std::string selectTaskId = "");  // 0 = first tab
  void goToCalendar();
  // Only reachable when ENABLE_BLE_NOTIFY_SPIKE is defined -- see
  // BleNotifyRelay's own doc comment. HomeActivity never offers this tile
  // otherwise, so no caller outside that build should ever invoke it.
  void goToBleNotifications();
  // Syncs every configured integration over one Wi-Fi association. onReturn
  // reopens whichever screen's status bar this was reached from -- see
  // SyncAllActivity's own comment; defaults to Home for a caller with no
  // particular screen to return to.
  void goToSyncAll(std::function<void()> onReturn = nullptr);
  void goToReadMenu();
  void goToBrowser(bool returnToReadMenu = false);
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  void goToSleep(bool fromTimeout = false, bool forceQuickResume = false);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE);
  // Opens the companion screen (QuickPickActivity, the main screen's "Planner"
  // tile) fresh -- also goToApp()'s AppId::Companion case.
  void goToCompanion();
  // Dispatches to whichever of the goTo* methods above opens `id`'s own
  // screen -- the side Left/Right "previous/next app" shortcut every app
  // screen has (see homeAppOrder::adjacentVisibleApp()) needs one call site
  // that can take any app in the grid's own order, not a fixed pair.
  void goToApp(homeAppOrder::AppId id);

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();

  bool preventAutoSleep() const;
  bool isReaderActivity() const;
  bool isQuickPickActivity() const;
  bool handleForcedRefresh();
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
