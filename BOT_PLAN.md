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
- [ ] Add execution state for travel-node steps so `resume-route` can continue after a crash, disconnect, death, or manual intervention.
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
- [ ] Add surveyed road/handoff waypoints for Ironforge -> Dun Morogh -> Loch Modan -> Wetlands -> Menethil; the first `grand-expedition-v0` dry run stalls when asking for Ironforge -> Menethil as one giant leg.
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
- [ ] Decide how aggressive low-level road-following should be around Wetlands deaths: pure road route, spirit-resume support, or escort/guard behavior.

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
