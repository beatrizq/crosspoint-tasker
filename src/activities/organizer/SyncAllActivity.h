#pragma once
#include <cstdint>
#include <functional>
#include <utility>

#include "activities/Activity.h"
#include "util/OrganizerSync.h"

/**
 * Syncs every configured integration in one go, over one Wi-Fi association.
 *
 * Reached by focusing any screen's own status bar and pressing Select (see
 * ActivityManager::goToSyncAll()). Each organizer screen can already sync
 * itself, but doing all four that way means associating four times and
 * rebooting between each - every one of those screens reboots on exit to
 * reclaim the Wi-Fi/TLS heap. Here the radio comes up once, the four syncs
 * run back to back through organizerSync, and the radio goes down once.
 *
 * Progress is shown per service as it goes, because these are network round
 * trips: a screen that said nothing for forty seconds would look hung. Each row
 * is repainted before its sync starts, and the sync blocks, so the row on screen
 * is always the one being worked on.
 *
 * A service with nothing configured is skipped rather than failed: an
 * integration you have not set up is not a broken one. A service that fails does
 * not stop the others - they are independent accounts, and one expired token
 * should not cost the rest of the sync.
 *
 * Requests cannot be interrupted once started, so Back is ignored until the run
 * finishes. Like the organizer screens, this reboots on exit once the radio has
 * been up.
 */
class SyncAllActivity final : public Activity {
 public:
  // onReturn reopens whatever screen's own status bar sent the user here --
  // a small lambda the caller builds at the moment it calls goToSyncAll(),
  // since the calling Activity itself is destroyed the instant that call
  // replaces it with this one (see ActivityManager::goToSyncAll()'s own
  // comment). Defaults to nullptr, meaning "go Home" -- the original
  // behavior, from when this screen only had the one caller.
  explicit SyncAllActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           std::function<void()> onReturn = nullptr)
      : Activity("SyncAll", renderer, mappedInput), onReturn(std::move(onReturn)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Prevents the idle timer from downclocking the CPU mid-run; runAll() blocks
  // for seconds at a time without otherwise touching lastActivityTime, same as
  // KOReaderSyncActivity and the other long-running sync activities.
  bool preventAutoSleep() override { return !finished; }

 private:
  enum class RowState : uint8_t {
    Skipped,  // Not configured; nothing to ask for
    Waiting,  // Configured, not reached yet
    Syncing,  // In flight
    Done,
    Failed,  // message holds the translated reason
  };

  void runAll();
  // Translated status for the row's right-hand column.
  const char* rowStatus(int index) const;

  RowState states[organizerSync::SERVICE_COUNT] = {};
  const char* messages[organizerSync::SERVICE_COUNT] = {};

  // Set once the whole run is over, which is what re-enables Back.
  bool finished = false;
  // No service was configured, so nothing ran and there is nothing to report.
  bool nothingToDo = false;

  bool wifiActivated = false;
  bool radioTornDown = false;

  std::function<void()> onReturn;
};
