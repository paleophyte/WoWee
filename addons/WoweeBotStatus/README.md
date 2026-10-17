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

`Control.lua` handles acknowledged control independently of visible frames. `Status.lua` parses activity reports; `Grind.lua` supplies the legacy model checks. `WoweeBotStatus.lua` connects these models to game events and renders the panel. Rich activity currently uses Playerbots debug reports; it does not provide a complete quest catalogue, queue, or reward-decision inbox.
