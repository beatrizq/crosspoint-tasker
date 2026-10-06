# Todoist

A **Todoist** entry sits in the main menu, right after Recent Books. It shows the tasks Todoist reports as due today
plus everything overdue, and lets you complete them from the device.

The screen renders from a cache on the SD card, so it opens instantly with the radio off. Syncing is an explicit
action — the device only connects to Wi-Fi when you ask it to.

## Setup

1. Get a personal API token from Todoist: **Settings → Integrations → Developer → API token**
   (<https://app.todoist.com/app/settings/integrations/developer>).
2. On the device: **Settings → Todoist → API Token**, and type the token on the on-screen keyboard.
3. Open **Todoist** from the main menu and press **Select** to sync.

The token is XOR-obfuscated against the device's MAC address and stored in `/.crosspoint/todoist.json`. It cannot be
decoded on another device or on a PC. If you would rather not type 40 characters on an e-ink keyboard, write
`{"token": "<your token>"}` into that file from a computer — it is picked up on the next boot and re-saved obfuscated.

## Using the screen

| Button                 | Action                                                                       |
| ---------------------- | ---------------------------------------------------------------------------- |
| **Select** (press)     | Complete the highlighted task, after a confirmation prompt                    |
| **Select** (hold ~1 s) | On the tab bar: sync — push pending completions, then re-fetch the list       |
| **Up / Down**          | Move the selection                                                           |
| **Back**               | Home                                                                         |

The header shows `Today DD-MM-YYYY` and, on the right, `Overdue: N`. Overdue tasks are sorted to the top of the list.
When completions are waiting to be pushed, the header also shows `N to sync`.

Completing a task removes it from the list immediately and queues the completion locally, so it works with Wi-Fi off.
The queue is pushed at the start of the next sync, before the list is re-fetched. A completion that fails to push
stays queued.

## Sleep screen

Whenever the task list changes — after a successful sync, and after completing a task, which changes the list with
the radio off — the rendered list is saved to `/sleep.bmp` and the sleep screen mode is switched to **Custom**, so the
sleeping device shows your task list. Turn it off with **Settings → Tasker → Todoist → Sleep
Screen**.

`/sleep.bmp` is also where the image viewer's **Set Cover** action writes, so the first task screenshot would replace
a wallpaper you had chosen. It doesn't: the file is copied to `/.crosspoint/sleep_backup.bmp` before the first
replacement, and switching the option off puts it back — along with the sleep screen mode that was in force before
(Cover, Dark, and so on). The copy is dropped once it has been restored, and also when you pick a new cover in the
image viewer, since that is you saying what the wallpaper should be.

If there was no `/sleep.bmp` to begin with, switching the option off restores the mode only; the last task screenshot
stays on the card, unused, until something overwrites it.

## Notes and limits

- **The date comes from the network.** The X3/X4 have no RTC, so each sync pulls the time over NTP and uses the
  device's *Clock UTC Offset* setting (Settings → Customise Status Bar) to derive the local date. The header shows the
  date the list was fetched.
- **At most 60 tasks** are kept, and task titles are truncated to 120 characters.
- **One filter query per sync.** The list comes from `today | overdue` — Todoist evaluates "today" in your account's
  timezone.
- **Syncing reboots on exit.** Like KOReader sync, leaving the screen after a sync triggers a silent restart to
  reclaim the heap the Wi-Fi/TLS stack fragmented.
- Todoist's API is rate-limited; syncing is manual partly for that reason.

## API

Requests go to the Todoist unified API (v1) over TLS 1.3 via wolfSSL:

- `GET https://api.todoist.com/api/v1/tasks/filter?query=today%20%7C%20overdue&limit=200`
- `POST https://api.todoist.com/api/v1/tasks/{id}/close`

Both send `Authorization: Bearer <token>`. The task list response is streamed straight into a SAX parser
(`lib/Todoist/TodoistTasksParser.h`), so the JSON is never buffered whole.
