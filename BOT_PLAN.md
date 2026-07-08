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
- [ ] Decide whether pathfinding should come from CMaNGOS/Detour data, MiniManager-style map assets, or a separate extracted navmesh.

## Phase 3: Bot Fleet Manager

- [x] Add `tools/bot_fleet_manager/`.
- [x] Add `fleet.settings.example.json`.
- [x] Launch multiple `wowee_headless` child processes with distinct settings and API ports.
- [x] Stagger logins/startup commands to avoid server spikes.
- [x] Track leader process state and restart failures with backoff.
- [x] Poll and report leader API status.
- [x] Expose or document aggregate fleet commands: start, stop, status, command, goto.
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

- [ ] Review MiniManager's map/data approach and adapt the useful pieces into Python.
- [x] Build on top of the textual dashboard once leader status data is stable.
- [x] Add an abstract dashboard map that plots online leaders from `/world/self` without requiring map assets.
- [ ] Replace or augment the abstract map with MiniManager-style map art/projection once that project is available.
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

- [ ] Hostile detection and target selection.
- [ ] Combat behavior for party leaders.
- [ ] Loot handling.
- [ ] Patrol routes and behavior trees.
- [ ] Event streaming for lower-latency automation.
- [ ] Optional server-side bot takeover hooks.
- [ ] Lua automation examples.
- [ ] Runtime tests for `/movement/goto` against a live local CMaNGOS server.

## Notes

- First implementation target is CMaNGOS TBC, matching the active test environment.
- Scale should be measured in tiers: 1 leader, 2 leaders, 5 leaders, 10 leaders, then beyond.
- Coordinate conventions from `include/core/coordinates.hpp`:
  - Canonical: `X=north, Y=west, Z=up`.
  - Server/wire: `X=canonical.Y (west), Y=canonical.X (north)`.
  - Engine render: `X=west, Y=north`.
  - Headless `/world/self` API returns canonical `position.x = north`, `position.y = west`.
- First demo path: import `tools/headless_client/settings.json` into a one-leader fleet config, run fleet `supervise --dashboard`, open `http://127.0.0.1:8780`, then use `tools/automation_examples/party_demo.py`.
