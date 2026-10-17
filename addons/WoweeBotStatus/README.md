# WoWee Bot Status

Compact activity and control panel for a self-playerbot on the patched CMaNGOS TBC server. Keep the game connected while the bot plays.

- **Let me play** pauses your character's AI. Wait for **I am playing (server confirmed)**, then play normally.
- **Bot, you play** resumes it and refreshes its route from your current state. Wait for **Bot is playing (server confirmed)**.
- `/botplay manual` and `/botplay bot` request explicit states. `/botplay` toggles the requested state.
- `/botstatus` or the persistent **Bot activity** button reopens the panel. Closing the panel does not stop the controller or automatic grind switching.
- The **WoWee Bot → Switch manual / bot control** keybinding is available for assignment in keybindings.
- **Auto grind** adjusts the non-combat grind strategy for reported incomplete kill/creature-drop objectives. It pauses during manual control, unknown control state, and grouping. It is separate from who controls your character.

The addon requires the server's version-1 `.bot idle` / `WIDLE` control protocol. It queries state before changing it, matches acknowledgements to request IDs, and rejects old controller generations. It retries a request at most three times. If confirmation fails, automatic addon commands pause; use **Refresh** or **Retry control** after checking the connection. No SOAP, SSH, or administrator credentials belong in this addon.

The server pauses the existing AI rather than deleting it. Strategies and quest focus remain available; queued reactions and old routes are cleared. Existing self-bot login configuration still determines what happens at login: manual mode is not yet persistent across relog, and the addon queries the actual state on entry. Companion following and takeover during combat, death, taxis, and vehicles require additional live acceptance testing.

On servers advertising `activity_1`, the panel receives bounded snapshots with an exact quest ID and objective type. It shows the **destination** separately from the **interaction target and selected task**, plus the last successful action. Selected tasks are not necessarily executing; a successful action has no reported execution timestamp. Distances are direct yards, not a route estimate. Missing or incomplete snapshots pause activity-derived grind commands. Servers without this capability retain the previous debug-report adapter.

`Control.lua` handles acknowledged control independently of visible frames. `Activity.lua` assembles typed snapshots; `Status.lua` supplies the activity model and legacy adapter. `Grind.lua` supplies legacy model checks. `WoweeBotStatus.lua` connects them to game events and renders the panel. This does not provide a complete quest catalogue, queue, or reward-decision inbox.

## Updating the installed addon

After pulling this branch, refresh only the addon code (this update does not require a new native binary if the manual/bot-control build is already installed):

```sh
python3 tools/update_bot_addon.py --target build/bin/addons/WoweeBotStatus
```

Restart WoWee or reload its interface. The helper copies only Lua/XML/TOC code and preserves character saved variables and other addons. Adjust the target if you installed Bot Status under your game-data `Interface/AddOns` folder: WoWee finds that copy before bundled copies, and its log reports which path was registered. Rebuilding only the `wowee` target does not run the bundled-addon copy target.
