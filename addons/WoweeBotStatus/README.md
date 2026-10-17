# WoWee Bot Status

Compact activity and control panel for a self-playerbot on the patched CMaNGOS TBC server. Keep the game connected while the bot plays.

- **Let me play** pauses your character's AI. Wait for **I am playing (server confirmed)**, then play normally.
- **Bot, you play** resumes it and refreshes its route from your current state. Wait for **Bot is playing (server confirmed)**.
- `/botplay manual` and `/botplay bot` request explicit states. `/botplay` toggles the requested state.
- `/botstatus` or the persistent **Bot activity** button reopens the panel. Closing the panel does not stop the controller or automatic grind switching.
- The **WoWee Bot → Switch manual / bot control** keybinding is available for assignment in keybindings.
- `/botnotes` or the persistent **Notes** button opens the inbox. Its badge counts unread entries. Click a note for details; ability batches expand into a spell/rank list. Talents and spellbook links open the existing game windows.
- **Auto grind** adjusts the non-combat grind strategy for reported incomplete kill/creature-drop objectives. It pauses during manual control, unknown control state, and grouping. It is separate from who controls your character.
- **Policies** or `/botpolicy` opens Social Detours. On servers advertising `social_1`, this preference controls optional NPC visits, conversations, and idle activities. It defaults Off on the patched server; quest and maintenance interactions remain eligible. The display changes to On/Off only after a matching server reply confirms persistence. Policy changes work in manual mode without enabling AI and remain separate from Auto grind. Unsupported servers show an unavailable message.

The addon requires the server's version-1 `.bot idle` / `WIDLE` control protocol. It queries state before changing it, matches acknowledgements to request IDs, and rejects old controller generations. It retries a request at most three times. If confirmation fails, automatic addon commands pause; use **Refresh** or **Retry control** after checking the connection. No SOAP, SSH, or administrator credentials belong in this addon.

The server pauses the existing AI rather than deleting it. Strategies and quest focus remain available; queued reactions and old routes are cleared. Existing self-bot login configuration still determines what happens at login: manual mode is not yet persistent across relog, and the addon queries the actual state on entry. Companion following and takeover during combat, death, taxis, and vehicles require additional live acceptance testing.

On servers advertising `activity_1`, the panel receives bounded snapshots with an exact quest ID and objective type. It shows the **destination** separately from the **interaction target and selected task**, plus the last successful action. Selected tasks are not necessarily executing; a successful action has no reported execution timestamp. Distances are direct yards, not a route estimate. Missing or incomplete snapshots pause activity-derived grind commands. Servers without this capability retain the previous debug-report adapter. In that adapter, only explicit kill objectives can enable grind: loot display links do not distinguish creatures from containers. Typed creature-loot objectives still enable it; typed object loot does not.

The initial inbox records abilities learned while connected, one changing talent-points reminder, and missing/rejected control, activity, or policy replies. Repeated identical problems do not create more entries. A fresh matching reply resolves a communication blocker; it does not establish that a stalled quest or NPC operation has recovered. The inbox cannot yet report training costs or hold quest reward choices. It never chooses rewards, spends talents, or changes control. Normal Playerbots reward behavior remains in effect until server-enforced choices are implemented. A policy communication failure leaves manual/bot control available; open Policies and use Refresh to query again.

Notes persist separately by advertised realm endpoint, realm ID, and character. Changing a realm endpoint starts a separate inbox. The latest native build supplies this identity; older builds show a temporary-notes warning and do not save potentially mixed-realm notes. Dismissing a note or using **Clear notes** does not resolve blockers or discard unresolved choices. Informational history is bounded to 100 entries; unresolved blockers/choices are retained. Cleared talent reminders stay dismissed until the point count changes. Initial spellbook loading does not create learned-ability notes, and learning costs are omitted because they are not supplied by the learned-spell event.

`Control.lua` handles acknowledged control independently of visible frames. `Activity.lua` assembles typed snapshots; `Status.lua` supplies the activity model and legacy adapter. `Grind.lua` supplies legacy model checks. `Inbox.lua` stores notes and `NotesUI.lua` renders them independently of the activity panel. `WoweeBotStatus.lua` connects the controller and activity to game events. A complete quest catalogue, queue, training-result reporting, and reward decisions remain future work.

## Updating the installed addon

After pulling this branch, rebuild WoWee for real realm identity and durable notes. Then refresh the addon code:

```sh
python3 tools/update_bot_addon.py --target build/bin/addons/WoweeBotStatus
```

Restart WoWee or reload its interface. The helper copies only Lua/XML/TOC code and preserves character saved variables and other addons. Adjust the target if you installed Bot Status under your game-data `Interface/AddOns` folder: WoWee finds that copy before bundled copies, and its log reports which path was registered. Rebuilding only the `wowee` target does not run the bundled-addon copy target.
