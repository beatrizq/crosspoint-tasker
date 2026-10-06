# YNAB

The **Budget** tab of the Planner screen shows the categories you picked from your YNAB budget, each with its
available balance on the same line.

Like the other Planner tabs, it renders from a cache on the SD card, so it opens instantly with the radio off.
Syncing is an explicit action — the device only connects to Wi-Fi when you ask it to.

## Setup

1. Get a personal access token from YNAB: **Account Settings → Developer Settings → New Token**
   (<https://app.ynab.com/settings/developer>).
2. Find your budget ID. Open the budget in the web app and copy the UUID out of the URL:
   `https://app.ynab.com/<budget-id>/budget`. The literal `last-used` also works, and picks whichever budget you
   opened most recently.
3. On the device: **Settings → Planner → YNAB**, then fill in **Access Token** and **Budget ID** with the on-screen
   keyboard.
4. Still in that menu, open **Categories** and tick the ones you want on the screen (up to 16). The list is fetched
   live, so this step needs Wi-Fi.
5. Open **Planner → Budget** and press **Sync**.

The token is XOR-obfuscated against the device's MAC address and stored in `/.crosspoint/ynab.json`. It cannot be
decoded on another device or on a PC. If you would rather not type 64 characters on an e-ink keyboard, write
`{"accessToken": "<your token>", "budgetId": "<your budget id>"}` into that file from a computer — it is picked up on
the next boot and the token is re-saved obfuscated. The budget ID is not a secret and stays readable.

## Using the screen

| Button                 | Action                                                        |
| ---------------------- | ------------------------------------------------------------- |
| **Select** (press)     | Switch tab, when the tab bar is highlighted                   |
| **Select** (hold ~1 s) | Sync the tab being shown, from the tab bar                    |
| **Up / Down**          | Move the selection                                            |
| **Back**               | Home                                                          |

Syncing is always the hold, and always from the tab bar - a category row is read-only, so pressing one does nothing.
That is also what makes a tab with nothing on it syncable: an empty list has no rows to highlight, so the tab bar is
the only thing the selection can reach. The same gesture syncs the Tasks and Calendar tabs.

The header shows the month the balances belong to and how many categories are listed. Each row is the category name
with its available balance flush right; a long name is truncated so the number always stays whole.

## Notes and limits

- **Balances are the month's, not today's.** The screen reads YNAB's *current month* view, which is what the
  "Available" column shows in the app. The month in the header comes from the response, so no clock is needed — which
  matters on the X3/X4, neither of which has an RTC.
- **Amounts are formatted by YNAB**, in the budget's own currency format, so no currency setting is needed on the
  device. Budgets on an API version predating formatted amounts fall back to a plain decimal with no symbol.
- **At most 16 categories** can be selected, and the picker lists up to 64. Hidden and deleted categories are left out.
- **Read-only.** Nothing is ever pushed back to YNAB.
- **Syncing reboots on exit.** Like the other Planner tabs, leaving the screen after a sync triggers a silent
  restart to reclaim the heap the Wi-Fi/TLS stack fragmented.
- **YNAB allows 200 requests per hour** per token, which is also why syncing is manual. Each sync and each visit to
  the category picker costs one request; a `429` is reported on screen as "Too many requests, try later".

## API

One request, over TLS 1.3 via wolfSSL:

- `GET https://api.ynab.com/v1/plans/{budget_id}/months/current`, sending `Authorization: Bearer <token>`

`plans` is the current name for what the app still calls budgets; the older `/budgets/{budget_id}` path is an
undocumented alias for the same resource and takes the same ID. The response is streamed straight into a SAX parser
(`lib/Ynab/YnabMonthParser.h`), so the JSON — tens of KB for a real budget — is never buffered whole. Only the
selected categories survive the walk.
