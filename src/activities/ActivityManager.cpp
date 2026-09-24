#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/ReadMenuActivity.h"
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "util/ScreenshotUtil.h"
#ifdef ENABLE_BLE_NOTIFY_SPIKE
#include "network/BleNotificationsActivity.h"
#endif
#include "home/QuickPickActivity.h"
#include "organizer/BudgetActivity.h"
#include "organizer/CalendarActivity.h"
#include "organizer/HabitsActivity.h"
#include "organizer/SyncAllActivity.h"
#include "organizer/TasksActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FullScreenMessageActivity.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          8192,               // Stack size
                          this,               // Parameters
                          1,                  // Priority
                          &renderTaskHandle,  // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      currentActivity->render(std::move(lock));
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  if (currentActivity) {
    if (!currentActivity->isQuickPickActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) {
        return;
      }
      goHome();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer(const bool returnToReadMenu) {
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput, returnToReadMenu));
}

void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }

void ActivityManager::goToFileBrowser(std::string path, const bool returnToReadMenu) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path),
                                                        FileBrowserActivity::Mode::Books, returnToReadMenu));
}

void ActivityManager::goToRecentBooks(const bool returnToReadMenu) {
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput, returnToReadMenu));
}

void ActivityManager::goToTasks(const uint8_t initialTab, std::string selectTaskId) {
  replaceActivity(
      std::make_unique<TasksActivity>(renderer, mappedInput, static_cast<int>(initialTab), std::move(selectTaskId)));
}

void ActivityManager::goToCalendar() { replaceActivity(std::make_unique<CalendarActivity>(renderer, mappedInput)); }

void ActivityManager::goToHabits(std::string selectHabitId) {
  replaceActivity(std::make_unique<HabitsActivity>(renderer, mappedInput, std::move(selectHabitId)));
}

void ActivityManager::goToSyncAll(std::function<void()> onReturn) {
  replaceActivity(std::make_unique<SyncAllActivity>(renderer, mappedInput, std::move(onReturn)));
}

#ifdef ENABLE_BLE_NOTIFY_SPIKE
void ActivityManager::goToBleNotifications() {
  replaceActivity(std::make_unique<BleNotificationsActivity>(renderer, mappedInput));
}
#endif

void ActivityManager::goToBudget(const uint8_t initialTab) {
  replaceActivity(std::make_unique<BudgetActivity>(renderer, mappedInput, static_cast<int>(initialTab)));
}

void ActivityManager::goToReadMenu() { replaceActivity(std::make_unique<ReadMenuActivity>(renderer, mappedInput)); }

void ActivityManager::goToBrowser(const bool returnToReadMenu) {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0], returnToReadMenu));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true, returnToReadMenu));
  }
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  // Captured here, before replaceActivity() below swaps in SleepActivity:
  // the framebuffer still holds whatever the outgoing screen last rendered,
  // which is "whatever screen the device is on" -- capturing any later, once
  // SleepActivity itself has painted, would just save a picture of the sleep
  // screen. Same file and format installCustomWallpaper() writes, so
  // SleepActivity's CUSTOM-mode render (which DYNAMIC also uses -- see
  // SleepActivity::renderCustomSleepScreen()) picks it up unchanged.
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::DYNAMIC) {
    const uint8_t* framebuffer = renderer.getFrameBuffer();
    if (framebuffer != nullptr) {
      if (!ScreenshotUtil::saveFramebufferAsBmp("/sleep.bmp", framebuffer, renderer.getDisplayWidth(),
                                                renderer.getDisplayHeight())) {
        LOG_ERR("ACT", "Failed to write dynamic sleep screen");
      }
    } else {
      LOG_ERR("ACT", "Framebuffer unavailable; dynamic sleep screen not updated");
    }
  }
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
  // Home is the companion's own screen now, not the app-tile grid (see
  // QuickPickActivity's own header comment) -- HomeActivity is left fully
  // intact but unreachable, so this is the one place that changed rather
  // than every one of this function's 25+ callers. QuickPickActivity has no
  // grid to preselect a tile on, so initialMenuItem (still passed by several
  // callers, e.g. OrganizerScreenActivity's own homeItem()) is simply
  // ignored now rather than plumbed through -- harmless, not broken.
  (void)initialMenuItem;
  replaceActivity(std::make_unique<QuickPickActivity>(renderer, mappedInput));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::goToCompanion() { replaceActivity(std::make_unique<QuickPickActivity>(renderer, mappedInput)); }

void ActivityManager::goToApp(const homeAppOrder::AppId id) {
  switch (id) {
    case homeAppOrder::AppId::Read:
      goToReadMenu();
      return;
    case homeAppOrder::AppId::Tasks:
      goToTasks();
      return;
    case homeAppOrder::AppId::Calendar:
      goToCalendar();
      return;
    case homeAppOrder::AppId::Budget:
      goToBudget();
      return;
    case homeAppOrder::AppId::Habits:
      goToHabits();
      return;
    case homeAppOrder::AppId::Notifications:
      // adjacentVisibleApp() already skips this app entirely when
      // ENABLE_BLE_NOTIFY_SPIKE isn't compiled in, so this case is only ever
      // reached in a build where goToBleNotifications() actually exists --
      // see its own declaration comment.
#ifdef ENABLE_BLE_NOTIFY_SPIKE
      goToBleNotifications();
#endif
      return;
    case homeAppOrder::AppId::Companion:
      goToCompanion();
      return;
    case homeAppOrder::AppId::Settings:
      goToSettings();
      return;
  }
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::isQuickPickActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isQuickPickActivity(); }) ||
         (currentActivity && currentActivity->isQuickPickActivity());
}

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
