# Bot Fleet Manager Plan

## Goal

Turn `wowee_headless` into a bot automation foundation that can supervise multiple player-character party leaders. Each leader can log in, form a CMaNGOS bot party, receive automation commands, and report world/chat/party state. The first practical scale target is 10 leader sessions, each with up to 4 server-side follower bots.

## Current Strategy

- [x] Use client-side orchestration first; do not require CMaNGOS server changes for v1.
- [x] Treat each logged-in headless character as a leader agent.
- [x] Use CMaNGOS/playerbot follower behavior for party members.
- [x] Keep Python/Lua automation external through HTTP APIs for the first implementation.
- [x] Pull current upstream `master` into the headless branch before continuing.
- [ ] Add WebSocket or Server-Sent Events after the request/response APIs stabilize.
- [ ] Revisit optional CMaNGOS server hooks after the client-side fleet model proves useful.

## Phase 1: Headless Leader Agent

- [x] Log in, select a character, enter world, and expose localhost chat APIs.
- [x] Add startup bot commands through `bots.names` and `automation.onEnterWorldCommands`.
- [x] Preserve local `settings.json` outside git.
- [x] Expose `GET /world/self` with map, position, orientation, movement state, and character identity.
- [x] Expose `GET /party` with parsed party members and leader GUID.
- [x] Expose `POST /commands` for raw queued chat/GM/playerbot commands.
- [x] Expose `POST /movement/goto` for direct leader travel.
- [x] Expose `POST /movement/goto/waypoints` for explicit waypoint-list travel.
- [x] Expose `POST /movement/stop` to cancel leader travel.
- [x] Add movement task status to `GET /status`.
- [x] Add death/combat detection fields to HTTP API (`combat.inCombat`, `health.current/max/isDead/isPlayerDead`).

## Phase 2: Leader Travel MVP

- [x] Implement direct-line steering to a coordinate: `mapId`, `x`, `y`, `z`, optional `arrivalRadius`.
- [x] Face destination, send movement start/stop/facing/heartbeat packets, and update local position based on server run speed.
- [x] Stop when within arrival radius.
- [x] Fail cleanly on map mismatch, not-in-world, or invalid coordinates.
- [x] Detect and report server movement lock as a distinct travel failure.
- [x] Add waypoint-list travel for hand-authored route segments.
- [ ] Validate direct travel against live CMaNGOS after the upstream merge.
- [ ] Add runtime collision checks during movement.
- [x] Decide whether pathfinding should come from CMaNGOS/Detour data, MiniManager-style map assets, or a separate extracted navmesh.
- [x] Add a standalone pathfinding service boundary with `/health` and `/path`.
- [x] Add an MVP direct-line backend for transport and waypoint handoff testing.
- [x] Add fleet `pathfind-goto` command that asks the path service for waypoints and posts them to leaders.
- [x] Add a native `mmap_path_query` helper that loads CMaNGOS mmap/Detour data and emits JSON waypoints.
- [x] Wire the path service `external` backend to pass `DataDir` and preserve helper JSON errors.
- [x] Validate `mmap_path_query` against real extracted `mmaps/` data on the CMaNGOS host.
- [x] Deploy the pathfinding service on the CMaNGOS host and validate live fleet `pathfind-goto` against it.
- [ ] Validate the helper output against CMaNGOS `.mmap path` on the same start/end points.
- [x] Optimize `mmap_path_query` with bbox tile loading for short route queries.
- [x] Add navmesh-projected waypoint densification so stairs, ramps, and bridges are followed more closely by the headless mover.
- [ ] Add route smoothing and height stabilization pass to reduce visual jitter from tiny navmesh Z oscillations while preserving stairs/ramps.
- [ ] Broaden bbox validation across continents, instances, and longer cross-zone routes.
- [x] Add an iterative `/route` planner endpoint that can expose partial/stalled/max-leg progress without moving leaders.
- [x] Add a read-only fleet `plan-route` command for inspecting route legs from a live leader position.
- [x] Add guarded route execution that can move one leg, replan, and stop safely on stalled/partial routes.
- [x] Live-test guarded `route-goto` on a short complete path, then on a known stalled long path.
- [x] Harden the headless HTTP API server against intermittent 10053/10054/reset/400 responses during frequent polling.
- [x] Add named landmarks for common route/demo targets.
- [x] Add a route demo command for repeatable named route tests.
- [x] Add route stop/resume state for guarded route targets.
- [x] Add named travel nodes for cross-map and long-range routes: tram, boats, portals, and flight masters.

## Phase 2b: Grand Expedition Travel Planner

The aspirational route for the next pathfinding phase is a low-level Alliance "Grand Expedition" from Goldshire to Darnassus. It is intentionally bigger than one navmesh query: it forces the automation stack to handle city navigation, transports, continent transitions, and route resumption.

**Definition of done:** create a brand-new Human character, issue one command ("go to Darnassus"), and the automation layer plans and executes the entire route unattended - city navigation, transports, and continent transitions all self-detected and self-resumed, no manual coordinate tuning, no watching-and-reacting to catch a timing window. If a step needs a human to babysit it in real time to work, that step isn't done yet, no matter how many times it's been made to work manually.

Target route:

- [ ] Northshire -> Goldshire -> Stormwind City -> Dwarven District tram entrance.
- [ ] Deeprun Tram: enter tram instance/area, board tram car, wait for transit, exit at Ironforge side.
- [ ] Ironforge -> Dun Morogh -> Loch Modan -> Wetlands -> Menethil Harbor, favoring roads for low-level safety.
- [ ] Menethil Harbor -> Auberdine boat: navigate onto boat, detect departure/transit/arrival, navigate off boat.
- [ ] Auberdine -> Rut'theran Village transport: navigate to the correct dock/boat or validated alternate travel method.
- [ ] Rut'theran Village -> Darnassus: navigate off transport and through the portal.
- [ ] Include at least one griffon/flight-path leg once flight-master gossip and known-route availability can be detected. Candidate: Auberdine/Darkshore <-> Rut'theran Village if available in the active TBC server data.

Implementation roadmap:

- [x] Add a travel node registry with typed nodes: `walk`, `tram_entrance`, `tram_platform`, `boat_dock`, `boat_deck`, `portal`, `flight_master`, `zone_handoff`.
- [x] Store each node in WoWee canonical coordinates with map id, radius, faction/expansion notes, and optional linked destination node.
- [x] Add a route planner mode that chains navmesh legs and travel-node transitions.
- [x] Add planner output that distinguishes `walk`, `board_transport`, `wait_transport`, `disembark`, `use_portal`, and `use_flight` steps.
- [x] Add survey tooling to record live map/position/orientation/movement flags while manually walking transport areas.
- [x] Add a capture command that writes surveyed leader positions back into the travel node registry.
- [ ] Add execution state for travel-node steps so `resume-route` can continue after a crash, disconnect, death, or manual intervention. Resume must re-verify progress against the specific target step, not just any plausible-looking signal that a step completed - live-confirmed this matters: an early tram dwell-detector treated "the car stopped" as arrival without checking *which* platform it stopped at, and falsely reported success ~2400 yards from the actual destination. Every arrival/completion check needs to know what it's actually trying to reach.
- [ ] Prototype with Deeprun Tram first because it directly addresses the current Goldshire/Stormwind -> Ironforge stall.
  - [x] Verify Stormwind tram entrance AreaTrigger `2173` and target landing on map `369`.
  - [x] Add an explicit headless `/area-trigger` API and fleet `area-trigger` command for validated transition prototyping.
  - [x] Identify Deeprun Tram cars as CMaNGOS `GAMEOBJECT_TYPE_TRANSPORT` / ElevatorTransport objects using `gameobject` spawn rows plus `TransportAnimation.dbc`, not boat-style `transports` rows.
  - [x] Add fleet `tram-state` predictor/calibration tooling that estimates Deeprun car positions from DBC animation data and live leader position.
  - [x] Split broad tram station detection from tighter platform boarding candidates after live Ironforge-side testing showed the far tram can still be inside the coarse station window.
  - [ ] Discover/implement actual tram boarding/wait/disembark behavior instead of walking the Deeprun tunnel.
  - [ ] Wire calibrated tram state into `ride_tram` execution: wait for car, board at platform, ride, disembark, then trigger the destination exit.
  - [ ] Move the Deeprun ElevatorTransport math into shared C++ client code so the regular WoWee client and headless automation agree on tram car placement.
- [ ] Add same-continent landmark routing polish after tram prototype: clearer map-mismatch messages, route feasibility labels, and better named demos.
- [x] Make route execution death-aware: check `health.isDead`/`isPlayerDead` (already exposed by `/world/self`) during `route-goto`/`travel-node-execute` and stop cleanly with a clear reported reason instead of continuing to issue movement commands for a dead character until it times out confusingly. Prompted by the low-level Wetlands road crossing on the Grand Expedition route, where mobs stray close enough to the road to be a real risk. Live-validated on an unforced real death during the Ironforge -> Menethil dry run (Dun Morogh/Loch Modan, not even Wetlands yet) - `LeaderDiedError` fired cleanly instead of the route hanging/timing out confusingly.
- [ ] Add death recovery: release spirit, walk back to the corpse as a ghost, reclaim it, and resume the interrupted route. Most of the protocol-level work already exists in the GUI client's release/reclaim flow (`GameHandler::releaseSpirit()`, corpse position tracking, `canReclaimCorpse()` incl. the reclaim-delay timer, the `CMSG_RECLAIM_CORPSE` send) - this is mostly headless API exposure plus Python orchestration, not new protocol work.
  - [ ] Expose corpse/ghost state over the headless API: corpse position + map, `canReclaimCorpse`, remaining reclaim delay, released-spirit flag.
  - [ ] Add `POST /release-spirit` and `POST /reclaim-corpse` endpoints wrapping the existing GameHandler methods.
  - [ ] Add a Python death-recovery routine: detect death -> release spirit -> wait out the reclaim delay -> walk to the corpse via the existing pathfinding-backed route-goto -> reclaim -> confirm alive.
  - [ ] Verify ghost-form movement behaves normally through the existing movement system - untested assumption, not yet confirmed live.
  - [ ] Wire death-recovery into route resume so the interrupted route continues from a sensible point after reclaiming, rather than starting over.
- [ ] Fix headless single-target `/movement/goto`'s Z interpolation: `updateMovementTask()` in `tools/headless_client/main.cpp` (`newZ = move.z + dz * t`) linearly interpolates Z straight toward the *final* target's z over the whole remaining distance, instead of sampling real ground/floor height along the way. Over a long walk this produces a smooth, continuous z ramp with no flat segments or stair step-changes, which doesn't match the real (mostly-flat, stairs-only) floor - live-observed as the leader visibly "jumping into the air and floating back down" when viewed from another client. `setPosition()` in `movement_handler.cpp` is a dumb setter with no ground-height correction, so nothing catches this downstream. `route-goto`/`travel-node-execute` are largely insulated since they walk short (~4y) pathfinding-service-derived hops with real per-point z from the navmesh, so the error self-corrects at every waypoint; the raw single-shot `goto` CLI/API (used for quick ad-hoc walks, e.g. most of this session's live debugging) is what exposes it badly over long distances. Needs either real client-side ground-height sampling or a guard that chunks/refuses long single-target goto calls. Documented, not yet fixed.
- [x] Add hostile-proximity avoidance - ended up building actual rerouting rather than just a pause, since a real trogg camp sat right on the road (pausing at its edge would never clear). `route-goto --avoid-hostiles` scans `/world/entities` for hostile units (real faction-reaction data, `Unit::isHostile()`, now exposed over the API - was already fully computed client-side, just never surfaced), computes a level-gap-scaled danger radius per threat (higher mob level relative to the player pushes it out, matching real aggro-range scaling), and pushes any waypoint inside a threat's radius directly away from it - a repulsion field bending the existing pathfinding-service waypoints around danger without a fresh planning query. Rescans periodically *during* a leg (chunks of ~15 waypoints), not just once at the start - a single leg can span 1000+ yards, and scanning only at the start missed a second, more dangerous mob cluster entirely. Live-validated: successfully detoured around a 12-mob camp and, on retest, 6 more mid-leg reroutes around real level 20-24 threats. Smart routing (this) is done; full combat/engage-and-fight fallback is still Phase 6 territory.
  - Even with working avoidance, the Loch Modan/Wetlands stretch past Thelsamar turned out to be a *sustained* high-level corridor (level 20-24 mobs, 4-14 in nearly every scan), not a single camp - likely just not survivable for a level 6 character no matter how good the detour math is. Character died repeatedly (4+ times) attempting it even with full HP and avoidance active.
- [ ] Add surveyed road/handoff waypoints for Ironforge -> Dun Morogh -> Loch Modan -> Wetlands -> Menethil; the first `grand-expedition-v0` dry run stalls when asking for Ironforge -> Menethil as one giant leg.
  - [x] Split the leg at `thelsamar` (DBC-verified coordinate), then found a second stall right at the Ironforge gate itself - not a missing-tile issue (all 513 mmap tiles for map 0 checked out contiguous once a filename-parsing mistake on our end was corrected: the `.mmtile` filename encodes `mapId+Y+X`, not `mapId+X+Y`), but a narrow navmesh connectivity gap at the indoor-WMO-to-outdoor-terrain seam (`polyCount` collapsed to 5 right at the gate, jumped to 124+ about 300 yards out). Added `ironforge-gate-south`, a live-surveyed waypoint just past the gap, as a workaround rather than chasing a navmesh-generation fix.
  - [ ] Continue the survey past Thelsamar through Wetlands to Menethil Harbor. First attempt (2026-07-11) got roughly 1140y past Thelsamar via `route-goto` before stalling again around canonical `(-1240.7, -4332.1)` in Dun Morogh/Loch Modan (`polyCount` collapsed to 5, same signature as the Ironforge gate). Unlike the gate, this one is not a simple point fix: nudging forward found a spot ~24y away where `polyCount` ticked up to only 9 (vs. 124+ at the gate), and a multi-leg `/route` query from there made 23.9y of real progress before stalling again at `polyCount=1`. A compass-direction probe around that point found the connected navmesh pocket is small in most directions, with several queries even 502'ing (path-helper failure, not just a low-poly response) - inconclusive on which way the real road actually goes. All 36 tiles covering the area are loaded per the service's own accounting, so this looks like either a genuine navmesh generation gap over complex terrain (not a missing-file issue) or the straight-line probing direction simply isn't the real road's path.
    - Turned out the automated straight-line probing was leading up into unreachable mountain terrain the whole time - the user manually walked the real road (through an actual gate structure) and repositioned the leader there; from that spot the pathfinder found a real, well-connected road (`polyCount` 185/183/93) for ~1140y before it turned out to just loop back into Ironforge's own navmesh rather than continue toward Wetlands. **Separately, the user found a real hazard while manually exploring past Thelsamar**: a tunnel mouth around canonical `(-2483.9, -6039.2)` where walking forward sends the character up into the mountain and eventually falling through world geometry into the void - confirmed server-side (not just a client render glitch) since relogging afterward reset the character to the Ironforge bind point. Do not route bots through that tunnel; treat it as a known map-data hazard, not a navmesh gap to fix.
    - Lesson for next attempt: don't trust automated point-to-point pathfinding to find the real road in this stretch - it happily routes back through well-connected areas (like a city interior) that are geometrically "on the way" but wrong. Better to have a human walk the real route once (or drive the walk via the new `follow_player.py` trailing script) and capture waypoints from that, rather than blind coordinate probing.
  - [x] Have a human walk Ironforge -> Menethil live while `follow_player.py` trails and catalogs it. Executed 2026-07-11 evening: leader followed the user on foot from the Dun Algaz tunnel entrance through Wetlands to Menethil Harbor, producing a 119-waypoint human-verified catalog at `tools/bot_fleet_manager/road_surveys/wetlands/ironforge-to-menethil.json`. Survived two real leader deaths mid-session (recovered via `recover-death`/manual revive + restarting `follow_player.py`, which now resumes from the existing catalog instead of overwriting it - see below). Replayable via `bot_fleet_manager.py replay-survey tools/bot_fleet_manager/road_surveys/wetlands/ironforge-to-menethil.json`.
  - [x] Build a reusable survey-catalog system: `follow_player.py <api> <player> <map-name> <route-name>` saves the walked path to `tools/bot_fleet_manager/road_surveys/<map-name>/<route-name>.json`; `bot_fleet_manager.py replay-survey <path>` walks a leader through a saved catalog directly (optionally with `--avoid-hostiles`), skipping the pathfinding service entirely. Designed to extend to other maps/routes, not just this one leg - live-exercised end-to-end on the Wetlands survey above.
    - Fixed a data-loss bug found during that live run: `follow_player.py` always started from an empty waypoint list, so restarting after a leader death silently overwrote everything captured so far (lost 71 waypoints once before the fix). Now loads the existing catalog file on startup and resumes appending to it.
  - [x] Exercise the catalog both directions: `replay-survey --reverse` walks tail-to-head. Live-validated 2026-07-11 by replaying the Wetlands survey backwards (Menethil -> Ironforge) with `--avoid-hostiles --resume-through-death`, surviving 5 real deaths with zero manual intervention and reaching the Dun Algaz tunnel entrance. `--resume-through-death` recovers via the same ghost-walk/reclaim primitive `recover-death` uses, then resumes from whichever waypoint is nearest the post-recovery position. Found and fixed one bug live: `wait_for_movement` raises as soon as it sees death, before movement reaches a terminal state, which bypassed the death-recovery loop entirely until caught explicitly.
  - [x] Add an efficiency calculation for death recovery: `_recover_from_death` now compares ghost-walk-to-corpse-and-reclaim (free, but gated by a real server-side reclaim delay - observed 72-100s per death, scaling with repeated deaths) against spirit-healer resurrection (near-instant, but costs 25% durability + resurrection sickness, and still requires walking alive from the graveyard to the nearest resume target). Only switches to the spirit healer when it's 30%+ faster, not on a marginal edge. Wired into both `recover-death` (route-goto, via the saved route state's target) and `replay-survey --resume-through-death` (via the remaining catalog waypoints) through the same shared primitive. Live-validated both branches: chose the spirit healer correctly when it was clearly faster, and chose the corpse walk correctly when it wasn't - but the first live spirit-healer attempt exposed a real bug (see below).
  - [x] Extended `--avoid-hostiles`/`--resume-through-death` to `route-goto` and `travel-node-execute` (previously only `replay-survey`), plus a new `--auto-learn-flight-paths` flag on all three. Found and fixed two interaction-range bugs live: both `_recover_from_death`'s spirit-healer branch and the new flight-path auto-learn fired their NPC interaction from wherever the search radius (30-60y) happened to find the NPC, not the real ~3-5y CMaNGOS requires to actually accept it - silently doing nothing from just outside that range even though the NPC shows up in the entity scan. Both now walk in close first.
  - [x] Made `/follow` actually walk toward the target instead of just tracking the camera - it already existed (`FollowCommand`/`GameHandler::followTarget()`) but only moved `followRenderPos_` for the camera controller; the character never moved, which is exactly the gap `follow_player.py` was built to paper over externally (and lagged behind the target's real position doing so). Added `MovementHandler::updateFollowMovement()`, called from the same per-frame block that already refreshes `followRenderPos_`, so it works in both clients with no extra wiring. Exposed to headless as `POST /follow {name}` / `POST /follow/stop`. Live-validated: followed a real Ironforge NPC, confirmed genuine per-frame position updates (not just a camera pointer), re-chased when the NPC moved, stopped cleanly at a 4-yard threshold.
  - [x] Reversed the remaining Ironforge <-> Menethil route (Wetlands tunnel entrance -> Thelsamar -> Ironforge gate -> Ironforge City) via `travel-node-execute` with avoidance/death-resume/auto-learn-flight-paths all enabled. Survived a Rockjaw Trogg gauntlet and several more deaths with automatic recovery. Hit two more local navmesh dead-ends where the planner reported near-zero remaining distance without the character actually being close (one right at Thelsamar, one approaching the Ironforge gate from the south this time) - same failure signature as the original `ironforge-gate-south` stall, worked around the same way (a short direct `/movement/goto` past the stuck point rather than chasing the pathfinder). Learned the Ironforge flight path on arrival.
- [ ] Survey and capture `stormwind-dwarven-district`, `stormwind-tram-entrance`, `deeprun-tram-stormwind-platform`, `deeprun-tram-ironforge-platform`, and `ironforge-tram-exit`.
  - [x] Seed Stormwind entrance and Deeprun/Ironforge platform nodes from `AreaTrigger.dbc` plus live CMaNGOS `areatrigger_teleport`.
  - [x] Adjust Stormwind tram approach nodes to the floor-level navmesh endpoint reached by the headless client.

Known unknowns:

- [ ] Confirm exact TBC coordinates and map/area transitions for both Deeprun Tram entrances, platforms, and exit points.
  - [x] Confirm Stormwind entrance `2173` -> Deeprun Stormwind platform on map `369`.
  - [ ] Confirm Ironforge exit `2166` from the Deeprun Ironforge platform once the tram-side leg is reachable.
- [ ] Determine whether the headless client can detect being on a tram/boat through movement flags, transport GUID, map id changes, position deltas, or server packets.
  - [x] Confirm the headless entity stream does not reliably expose Deeprun type-11 tram gameobjects while stationary on the platform; use DBC/DB prediction as the current Deeprun prototype.
  - [x] Confirm simply moving onto the floor-projected predicted tram position does not attach the headless client to a Deeprun transport; `onTransport` remains false and the character stays at the static floor position.
- [ ] Determine how to interact with portals and flight masters through available packets/Gossip APIs.
- [ ] Confirm whether Darkshore <-> Rut'theran flight is available to the test characters or if boat/portal flow should be the canonical Darnassus route.
- [ ] Decide how aggressive low-level road-following should be around Wetlands deaths: pure road route, spirit-resume support, or escort/guard behavior. Near-term plan is death-awareness + hostile-proximity pause (see implementation roadmap above) to fail loud and clean rather than avoid death entirely - actual death recovery (spirit walk back, corpse run, or just re-resurrect and resume-route) is a distinct, harder problem to solve later, not blocking the travel planner work now.

## Phase 3: Bot Fleet Manager

- [x] Add `tools/bot_fleet_manager/`.
- [x] Add `fleet.settings.example.json`.
- [x] Launch multiple `wowee_headless` child processes with distinct settings and API ports.
- [x] Stagger logins/startup commands to avoid server spikes.
- [x] Track leader process state and restart failures with backoff.
- [x] Poll and report leader API status.
- [x] Expose or document aggregate fleet commands: start, stop, status, command, goto.
- [x] Expose or document aggregate pathfinding command: pathfind-goto.
- [x] Add explicit fleet assignment semantics.
- [x] Support multiple fleet managers by making ports, settings paths, and process labels explicit.

## Phase 3b: Textual Fleet Visibility

- [x] Add a web dashboard that lists configured teams without a map dependency.
- [x] Show leader online/offline state, API base, textual map/position, activity, party members, and recent chat.
- [x] Add `dashboard` subcommand for already-running leaders.
- [x] Add `supervise --dashboard` for running the supervisor and dashboard together.
- [ ] Add richer activity inference from recent chat, movement transitions, combat, death, and command queue state.
- [ ] Add team filtering and a compact JSON endpoint suitable for external automation.
- [ ] Add stale-data timestamps per endpoint so partial API failures are obvious.

## Phase 3c: Map Viewer (Later)

- [x] Review MiniManager's map/data approach and adapt the useful pieces into Python.
- [x] Build on top of the textual dashboard once leader status data is stable.
- [x] Add an abstract dashboard map that plots online leaders from `/world/self` without requiring map assets.
- [x] Add a high-level MiniManager/POMM-style continental overview map.
- [x] Augment the abstract map with MiniManager-style zone bounds and optional zone art.
- [x] Add a scripted asset refresh command for pulling MiniManager zone PNGs into the ignored runtime asset directory.
- [x] Avoid committing the experimental static WHM/Leaflet map server until its coordinate and asset assumptions are validated.

## Phase 4: External Automation

- [x] Add Python examples that call the manager/headless HTTP APIs.
- [ ] Add Lua examples that call the HTTP APIs externally.
- [x] Provide examples for fleet startup, party formation, and party chat.
- [x] Provide examples for direct leader travel and explicit waypoint travel.
- [ ] Keep durable state in the manager, not in scripts.

## Phase 5: Provisioning

- [x] Add a direct SOAP account creation script.
- [x] Add an SSH-based account creation script that reads CMaNGOS host credentials from `tools/.env` and runs SOAP locally on the server host.
- [x] Add `wowee_headless` provisioning mode for creating a character from settings JSON.
- [x] Add a character creation wrapper script that generates temporary headless settings and exits after creation.
- [x] Document the account and character provisioning flow.
- [x] Add a batch/fleet provisioning script that creates many accounts and characters from one roster file.
- [ ] Add live integration tests against a local CMaNGOS instance.

## Asset Extraction Follow-Up

- [x] Identify the restored asset extraction fixes in `tools/asset_extract/open_format_emitter.cpp`.
- [x] Validate that the changes are isolated from bot automation work.
- [x] Build `asset_extract` after the upstream merge.
- [ ] Split the asset extraction fix into a standalone branch/PR.
- [ ] Validate on real extracted ADT inputs before treating WOC/WHM output as pathfinding-grade data.

## Future Work

- [ ] Event streaming for lower-latency automation.
- [ ] Optional server-side bot takeover hooks.
- [ ] Lua automation examples.
- [ ] Runtime tests for `/movement/goto` against a live local CMaNGOS server.

## Phase 6: Combat, Loot, and Progression Automation

The prerequisite for most of the fun stuff: leader characters can navigate but can't fight, loot, or run quests yet. Follower bots (CMaNGOS playerbots) already have their own combat AI, so the real gap is getting the *leader* characters (the actual headless client) to target, attack, loot, and handle quests.

- [ ] Add hostile target selection for leader characters (nearest hostile in range, or explicit target-by-guid via the API).
- [ ] Detect when a follower bot enters combat (party/bot status) and have the leader assist instead of continuing to navigate away and leaving the bot to fight alone.
- [ ] Add auto-attack / basic ability usage triggering for leader characters.
- [ ] Add loot handling: auto-loot on kill, or a queued loot command.
- [ ] Add quest automation: detect quest giver, accept, track objectives, turn in.
- [ ] Add a leveling-assembly-line mode: chain quest-accept -> navigate -> kill/loot -> turn-in cycles to level a character (target 1->60 or 1->70).
- [ ] Add a repeatable gold-farming circuit mode: fixed route, kill + loot + vendor + repeat, report gold/hour.
- [ ] Add gathering-profession automation: detect nearby herb/mining/skinning nodes and run a farming route.
- [ ] Add crafting-profession automation: buy mats from the AH, craft for skill-ups.

## Phase 7: Fleet-Scale Group Content Demo

A flagship stress test once Phase 6 lands: exercises fleet scale, travel automation, quest automation, and coordinated combat together in one scenario. 8 leaders x 5 characters (1 leader + 4 playerbot followers each) = 40 total, which fits inside the existing "10 leaders x 4 followers" scale target.

- [ ] Provision 8 accounts x 5 Human characters using the existing batch provisioning tooling.
- [ ] Configure the fleet manager to operate all 8 party leaders simultaneously.
- [ ] Navigate all parties to Westbrook Garrison.
- [ ] Automate accepting "Wanted: Hogger" for each party.
- [ ] Navigate to Hogger and coordinate group engagement (depends on Phase 6 combat automation).
- [ ] Return to Westbrook Garrison and turn in the quest.

## Phase 8: Social and Observability Automation

Doesn't depend on Phase 6 - these only need chat, `/who`, and existing slash commands, so they can ship independently and probably sooner.

- [ ] Server census: log in on each faction/realm, poll `/who` periodically, and build a population timeline (peak hours, faction balance).
- [ ] Guild management bot: automate invites, MOTD updates, event postings, and guild bank deposit/withdrawal tracking.
- [ ] Achievement/level-up celebrations: parse chat for level-up/achievement events, then trigger a synchronized fleet-wide emote chain, mount parade, or fireworks display.

## Notes

- First implementation target is CMaNGOS TBC, matching the active test environment.
- Scale should be measured in tiers: 1 leader, 2 leaders, 5 leaders, 10 leaders, then beyond.
- Coordinate conventions from `include/core/coordinates.hpp`:
  - Canonical: `X=north, Y=west, Z=up`.
  - Server/wire: `X=canonical.Y (west), Y=canonical.X (north)`.
  - Engine render: `X=west, Y=north`.
  - Headless `/world/self` API returns canonical `position.x = north`, `position.y = west`.
- First demo path: import `tools/headless_client/settings.json` into a one-leader fleet config, run fleet `supervise --dashboard`, open `http://127.0.0.1:8780`, then use `tools/automation_examples/party_demo.py`.

## Upstream Sync (2026-07-12)

Fast-forwarded `master` (local + `origin`) onto `upstream/master` (28 commits, clean - no local-only commits on master, `feat/headless-bot-fleet` dry-run-merges against the new master with zero conflicts across all 10 overlapping files). Peruse of the 28 commits for anything relevant to our own TBC/CMaNGOS bug-hunting, since several were framed as "restore WotLK X" fixes touching shared packet-parsing code:

- [x] **Auction list enchant-slot count was wrong for us too**: `AuctionListResultParser` read `numEnchantSlots=3` for TBC/WotLK; real value is 6 (`MAX_INSPECTED_ENCHANTMENT_SLOT`), a 36-byte-per-entry desync that corrupted every subsequent item ID/price. Comment explicitly says "WotLK/TBC send 6" - not emulator-specific, so this affected (or would have affected, if the fleet ever used the Auction House) our CMaNGOS bots too. Already fixed via the sync, no separate action needed.
- [x] **`SMSG_SPELLLOGEXECUTE` effectType was read as `uint8` instead of `uint32`**: "All expansions send effectType as uint32" per the fix commit - a universal bug, not WotLK-specific. Corrupted CREATE_ITEM effect item IDs (e.g. crafted item shows as item #3808428032). Already fixed via the sync.
- [x] **Deeprun Tram CMaNGOS-workaround gating** (`entity_controller.cpp`'s DESTROY_OBJECT transport-preservation, `transport_manager.cpp`'s position-echo handling) is now behind `isPreWotlk()` so it doesn't misfire on AzerothCore/WotLK. Verified this is a no-op for us: `isPreWotlk()` is true for our CMaNGOS/TBC setup, so both gated blocks behave exactly as before - no conflict with, or changes needed to, our own tram-boarding work.
- [ ] **Possible latent desync in `parseMonsterMoveSplineBody`/`parseMonsterMoveSplineBodyVanilla`** (`src/game/spline_packet.cpp:34,112`, used by `SMSG_MONSTER_MOVE` - reachable on TBC): both have `if (pointCount == 0) return true;`, structurally the same shape as the WotLK bug just fixed upstream (`c83db01b`, `parseWotlkMoveUpdateSpline`), where AzerothCore was found to still write trailer bytes (splineMode + endPoint) on a zero-point spline that the old code assumed had none, desyncing the next read. Not confirmed broken - `SMSG_MONSTER_MOVE` is a different wire format from the UPDATE_OBJECT-embedded spline block the WotLK bug was in (destination is read as part of the point block here, so a genuinely-empty case may be correct to skip entirely) - but worth a live CMaNGOS packet capture to rule out an equivalent trailer-byte assumption, especially given we already found two other packed-vs-raw-field desync bugs this session (`MSG_MOVE_TELEPORT_ACK`, other-player movement relay) in the same movement-parsing neighborhood.
- Methodological note, not a bug: `1b1831a2` (Wrath quest-POI axis-order fix) is another instance of the same "packet-embedded coordinate field uses the opposite axis order from the canonical world convention" class of bug we hit repeatedly tonight (WorldMapArea.dbc, MSG_MOVE_TELEPORT_ACK). Doesn't affect us directly (quest POI parsing isn't part of the automation flow), but worth checking axis order explicitly, not assuming, whenever a new packet-derived coordinate field gets consumed by the bot fleet.
