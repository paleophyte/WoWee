# WoWee Bot Status

Compact activity and control panel for a self-playerbot on the patched CMaNGOS TBC server. Keep the game connected while the bot plays.

Activity, History, training and navigation use the same bounded capability
message size as Control. This accepts the server's complete feature list even
with the longest valid request ID and generation; mismatched and oversized
replies are still ignored.

**Quests** or `/botquests` lists accepted quests and live objectives, including
entries under collapsed stock headers. Click a row to inspect it, then **Work on
this quest** to request a plan. On servers advertising `focus_1` (TBC0059), the
selected quest changes only after a matching server reply. **Clear quest
selection** restores ordinary autonomous quest choice. Neither button changes
Manual/Bot control; selecting during Manual prepares the plan for your next
explicit Bot request. A stale plan revision cannot overwrite a newer selection.

**Selected quest** stays separate from the current task and travel/quest
destination. It survives necessary maintenance and control handoffs. During a
handoff or missing reply, the last confirmed quest is labeled historical until
read-only confirmation returns. A request retries at most three times; timeout
or rejection discards the write intent and refreshes status, rather than
replaying it after recovery. Notes explain selection communication errors.

On TBC0060, three eligible empty collection scans accumulate for the same quest
item even when the bot changes locations or does maintenance between checks.
The server pauses that objective's container destinations for two minutes;
an item-count increase clears the pause sooner. Notes report the observed
absence and planned retry without guessing at respawn rates. This objective
pause is separate from rejected-loot recovery and keeps manual control available.

Failed quests remain distinct from ready ones. Server completion with unfinished
reported counts says checking progress. Browsing rows still leaves stock quest
log selection and bot focus untouched. This view requires the native
`WoweeGetAcceptedQuests` API from93638bd1d plus the current addon. Older servers
keep the browser and show selection unavailable; they receive no focus requests.

- **Let me play** pauses your character's AI. Wait for **I am playing (server confirmed)**, then play normally.
- **Bot, you play** resumes it and refreshes its route from your current state. Wait for **Bot is playing (server confirmed)**.
- `/botplay manual` and `/botplay bot` request explicit states. `/botplay` toggles the requested state.
- `/botstatus` or the persistent **Bot activity** button reopens the panel. Closing the panel does not stop the controller or automatic grind switching.
- `/botstatus refresh` or **Refresh** queries current control again without replaying an unconfirmed control/grind request. `/botstatus debug` prints local control/request/generation, capability, snapshot-fragment, and callback diagnostics; it sends no game commands. Use it if updates stop after switching manual/bot control.
- The **WoWee Bot → Switch manual / bot control** keybinding is available for assignment in keybindings.
- `/botnotes` or the persistent **Notes** button opens the inbox. Its badge counts unread entries. Click a note for details; ability batches expand into a spell/rank list. Talents and spellbook links open the existing game windows.

`/botpolicy` opens Hero policies. Servers advertising `requests_1` provide independent **Guild invites**, **Group invites**, and **Trade requests** choices. All three default to **Reject** during bot control. **Allow** disables that particular rejection policy; normal server permissions and bot behavior still apply. Preferences save per character on that server, and each change waits for server confirmation. Manual control always leaves incoming requests for you to handle. The policies do not leave an existing group/guild, cancel an already-open trade just because control changes, or disable outgoing invitations.

On servers advertising `item_use_1` (TBC patch0046), Notes also explain pickups skipped because the character already carries the unique-item limit, including bank storage. For example, an already-owned Water Bucket makes another Water Barrel pickup unnecessary. These are historical observations: other work can continue. A later server observation below the limit updates the note to say the limit cleared. Old servers simply omit this feature; missing or invalid reports do not stop control/status polling.
- **Auto grind** adjusts the non-combat grind strategy for reported incomplete kill/creature-drop objectives. It pauses during manual control, unknown control state, and grouping. It is separate from who controls your character.
- **Policies** or `/botpolicy` opens Social Detours. On servers advertising `social_1`, this preference controls optional NPC visits, conversations, and idle activities. It defaults Off on the patched server; quest and maintenance interactions remain eligible. The display changes to On/Off only after a matching server reply confirms persistence. Policy changes work in manual mode without enabling AI and remain separate from Auto grind. Unsupported servers show an unavailable message.

**Fishing** in Policies is independent of Social Detours and defaults Off on patch0009 for characters without a saved setting. It permits autonomous fishing and fishing-only travel; turning it Off cancels bot fishing while retaining the quest plan. Manual fishing remains available. Wait for server confirmation after changing it. Turning On grants permission but neither switches control nor bypasses the retry pause. Older servers show Fishing as unavailable and receive no fishing-policy requests.

The addon requires the server's version-1 `.bot idle` / `WIDLE` control protocol. It queries state before changing it, matches acknowledgements to request IDs, and rejects old controller generations. It retries a request at most three times. If control confirmation fails, addon automation pauses and the display shows the last confirmed mode/time separately. The server may still be playing. Read-only status recovery starts after 30 seconds, backing off to 60 then at most 120 seconds between failed retry bursts. **Refresh status** queries immediately. Timed-out control/grind intents are discarded; recovery does not reissue them. A successful query restores observation of the actual server mode; use an explicit control button/command if you still want to switch. No SOAP, SSH, or administrator credentials belong in this addon.

The server pauses the existing AI rather than deleting it. Strategies and quest focus remain available; queued reactions and old routes are cleared. Existing self-bot login configuration still determines what happens at login: manual mode is not yet persistent across relog, and the addon queries the actual state on entry. Companion following and takeover during combat, death, taxis, and vehicles require additional live acceptance testing.

On servers advertising `activity_1`, the panel receives bounded snapshots with an exact quest ID and objective type. It shows the **destination** separately from the **interaction target and selected task**, plus the last successful action. Selected tasks are not necessarily executing; a successful action has no reported execution timestamp. Distances are direct yards, not a route estimate. Missing or incomplete snapshots pause activity-derived grind commands. Servers without this capability retain the previous debug-report adapter. In that adapter, only explicit kill objectives can enable grind: loot display links do not distinguish creatures from containers. Typed creature-loot objectives still enable it; typed object loot does not.

The initial inbox records abilities learned while connected, one changing talent-points reminder, and missing/rejected control, activity, or policy replies. Repeated identical problems do not create more entries. A fresh matching reply resolves a communication blocker; it does not establish that a stalled quest or NPC operation has recovered. The inbox cannot yet report training costs or hold quest reward choices. It never chooses rewards, spends talents, or changes control. Normal Playerbots reward behavior remains in effect until server-enforced choices are implemented. A policy communication failure leaves manual/bot control available; open Policies and use Refresh to query again.

On servers advertising `warning_1`, a correlated **Fishing retry limit reached** note records a server-applied five-minute fishing pause after three failed attempts. It gives the server's observed failure category (cast failed, no channel, no pole found, or equip failed), without guessing a missing reagent. The bot can do other work and you can take manual control. This is historical information: the pause may already have ended. Status replays do not create duplicate notes, including after dismissal, relog or trimming of old entries. Unsupported or stale/unmatched messages are ignored. The addon reports the pause; retry handling runs on the patched server.

On servers advertising `collection_1`, **Could not collect: [quest]** records three rejected nearby container-loot checks and a two-minute skip of the checked containers. The note includes the item count and the server's last observed reason: full bags, a skip-loot setting, a busy/unready container, or an unidentified rejection. The selected quest is retained. Walking, fighting, casting, an open loot transaction, and absent objects do not count as failed collection attempts. This is a historical note, not a declaration that the whole quest is blocked or that respawns are slow. Dismissal and repeated status replies do not restart recovery or duplicate the note. This addon-only update needs no new native rebuild if your client already has the previous Notes/realm support.

Notes persist separately by advertised realm endpoint, realm ID, and character. Changing a realm endpoint starts a separate inbox. The latest native build supplies this identity; older builds show a temporary-notes warning and do not save potentially mixed-realm notes. Dismissing a note or using **Clear notes** does not resolve blockers or discard unresolved choices. Informational history is bounded to 100 entries; unresolved blockers/choices are retained. Cleared talent reminders stay dismissed until the point count changes. Initial spellbook loading does not create learned-ability notes, and learning costs are omitted because they are not supplied by the learned-spell event.

`Control.lua` handles acknowledged control independently of visible frames. `Activity.lua` assembles typed snapshots; `Status.lua` supplies the activity model and legacy adapter. `Grind.lua` supplies legacy model checks. `Inbox.lua` stores notes and `NotesUI.lua` renders them independently of the activity panel. `WoweeBotStatus.lua` connects the controller and activity to game events. A complete quest catalogue, queue, training-result reporting, and reward decisions remain future work.

## Updating the installed addon

After pulling this branch, rebuild WoWee for real realm identity and durable notes. Then refresh the addon code:

```sh
python3 tools/update_bot_addon.py --target build/bin/addons/WoweeBotStatus
```

Restart WoWee or reload its interface. The helper copies only Lua/XML/TOC code and preserves character saved variables and other addons. Adjust the target if you installed Bot Status under your game-data `Interface/AddOns` folder: WoWee finds that copy before bundled copies, and its log reports which path was registered. Rebuilding only the `wowee` target does not run the bundled-addon copy target.


## Gathering tools

With TBC server patch0052 (`gather_tools_1`), Notes explain autonomous skinning or
mining skipped for a missing carried Skinning Knife or Mining Pick. Banked tools
cannot satisfy the loot executor. Other eligible work can continue. Acquiring
the tool normally creates a separate availability note; other gathering
requirements still apply. Repeated checks do not duplicate the note, and clearing
notes keeps already-seen events cleared across relog. Gathering destinations also
name their purpose, such as **Skin Stonetusk Boar**. These reports are optional on
servers without the capability and never change character control.

## Travel progress and waiting

On servers advertising nav_1, the existing snapshot query also reports physical travel progress. The panel shows a route rebuild, a timed travel pause, or an observed wait reason. Quest progress and the selected travel plan remain visible. Stale/missing travel replies are labeled unavailable rather than presented as current.

The server independently samples actual map/instance/XYZ. Destination changes and successful action returns do not erase its no-movement budget. It rebuilds once after45 observed eligible seconds without3yd displacement, then pauses travel attempts for two minutes after45 more eligible seconds without progress. It keeps the selected activity; combat, death recovery and Manual control continue. Actual movement releases a pause. Manual takeover clears current tracking while retaining the historical Note.

Combat, casts/channels, flights, transport/vehicle handoffs, party waits, loot, death, teleport delays, immobilization, trade and movement policy have explicit explanations. The safeguard does not interrupt their owning action. A long observed stationary wait can create an informational Note, without guessing a missing item or a bad route. Navigation-pause Notes also describe a past event: the pause may already have ended. Dismissed facts do not reappear on later snapshots or relog, and unrelated later facts remain visible.

This is an addon-only update if your client already has the Notes/realm native support. Pull the branch, run the updater shown above, and restart/reload the interface. No native rebuild is required for these new controls. The original manual/bot controller remains authoritative; an unconfirmed control change never displays automation as stopped. Explicit named go-order preservation across Manual→Bot resume is still a separate known server follow-up.

## Death recovery observations

Servers advertising the separate `WIDLERCAP` recovery capability can supply
self-correlated `WIDLEREC` snapshot fragments. The panel keeps the saved
quest/travel plan visible and adds a recovery line for release, corpse recovery,
reclaim delay/range, spirit-healer attempts and unavailable/other-map corpses.
A resurrection acceptance or reclaim attempt is not presented as success;
the core must report the character alive. Recently observed health/mana appear
for60seconds after that transition.

Notes retain repeated-death counters, a ghost missing its corpse for10 observed
seconds,120 observed seconds without recovery movement, and alive transitions.
Manual takeover preserves historical warnings. A manual handoff alone does not
resolve a gameplay blocker. These displays do not choose resurrection policies,
spend resources, move the character or change quest priorities.

Older servers keep the existing display. Recovery shares the normal snapshot
poll and adds no chat commands. This update needs an addon refresh/reload;
it adds no native client APIs. The TBC server feature must also be installed.

Servers advertising the separate `WIDLESCAP` sickness-wait capability can also
report a current wait after spirit-healer resurrection. The panel shows the
server-confirmed countdown separately from the selected quest. Notes keep one
entry per wait and resolve it after a fresh report that sickness has ended.
Manual/Bot handoffs preserve the selected quest; the addon does not start,
extend or cancel a wait. Missing or stale confirmation hides the current wait.
This compatible addon support is published ahead of the matching TBC server
release. Older servers retain their existing behavior. No native rebuild is
needed for this display and Notes update.


## Selected skinning work

On a TBC server advertising `skinning_hunt_1`, the addon can distinguish a
selected skinning hunt from travel or other gathering. Auto Grind may enable
grinding while that eligible hunt is working, then disable it for travel,
waiting, quest containers and other errands. The server validates the carried
knife, skill/target eligibility and solo self-playerbot context.

This uses a separately negotiated `grind_2` activity snapshot. Older servers
keep their existing snapshot format and behavior. The normal confirmed Bot
control, group and Auto Grind settings still apply. The label describes the
selected plan; a successful kill or skin is not inferred from the label.

The feature requires the matching server update. The client change is addon
only: update the installed addon and reload/restart; no new native API is needed.
