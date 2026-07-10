# Bot Fleet Manager

`bot_fleet_manager.py` supervises multiple `wowee_headless` leader clients. Each leader logs in as one character, can add CMaNGOS follower bots through startup commands, and exposes its own localhost automation API.

## Run

Copy `fleet.settings.example.json` to a local config and fill in accounts, passwords, characters, and party bot names.

If you already have a working `tools/headless_client/settings.json`, generate a one-leader demo fleet config from it:

```bash
python tools/bot_fleet_manager/import_headless_settings.py
```

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json start
```

`start` launches the leaders and exits. For longer runs, use `supervise` so the manager keeps running, writes per-leader logs, and restarts crashed clients with backoff:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json supervise
```

Start the textual team dashboard while supervising:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json supervise --dashboard
```

Stream each leader's log output to the same console while supervising:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json supervise --dashboard --debug
```

The same flag is also available as `--verbose`, `--v`, or `-v`. Logs are still written under `tools/bot_fleet_manager/runtime/logs/`.
Each leader log line is timestamped by the supervisor as it is written, including plain `wowee_headless` console output that does not include its own timestamp.

Or run only the dashboard against already-running leaders:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json dashboard
```

The dashboard listens on `http://127.0.0.1:8780` by default and shows a leader-position map, leader status, textual position, current activity, party members, and recent chat. Use `--dashboard-port` with `supervise` or `--port` with `dashboard` to change the port.

The map uses MiniManager's continental projection for the high-level overview, and bundled MiniManager zone bounds for zone-level fallback views. It will draw client art when assets are available under `tools/bot_fleet_manager/runtime/map_assets/continent/` and `tools/bot_fleet_manager/runtime/map_assets/zone/`. That runtime directory is ignored by git. Set `WOWEE_MINIMANAGER_ZONE_DIR` to another `img/zone` folder if you want to serve the zone art from a different local MiniManager checkout.

Refresh MiniManager map art from the CMaNGOS host described by the external `wow_server` `.env`:

```bash
python tools/bot_fleet_manager/fetch_minimanager_assets.py --env C:\Users\admin\code\wow_server\.env
```

Common commands:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json status
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json landmarks
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-nodes --routes
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-plan deeprun-prototype
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-plan --static grand-expedition-v0 --max-legs 3
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-survey --leader demo-leader-1 --label stormwind-tram --duration 120
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-capture --leader demo-leader-1 --type tram_entrance --name "Stormwind Deeprun Tram Entrance" --verified stormwind-tram-entrance
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json tram-state --leader demo-leader-1 --calibrate
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json tram-state --leader demo-leader-1 --time-offset-ms 122400 --watch 30
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json board-tram --leader demo-leader-1 --from deeprun-tram-stormwind-platform --to deeprun-tram-ironforge-platform
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json command ".bot follow"
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json goto 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json plan-route 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json route-goto 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json route-demo goldshire
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json route-demo ironforge-probe --max-legs 2
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json resume-route
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json pathfind-goto 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json stop
```

Target one group or one leader:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json status --fleet alpha
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json command --leader leader-1 ".bot follow"
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json goto --fleet beta 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json plan-route --leader leader-1 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json route-goto --leader leader-1 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json pathfind-goto --leader leader-1 530 -248.0 951.0 84.0
```

Set each leader's `fleet` field in `fleet.settings.json` to create assignment groups.

`goto` sends one direct destination to each leader. `landmarks` lists built-in named targets such as `goldshire`, `stormwind`, `ironforge`, `crossroads`, `ratchet`, `exodar`, and `azure-watch`. `plan-route` asks the configured pathfinding service for iterative route legs and prints the result without moving anyone. `route-goto` executes cautiously by moving one planned leg, waiting for arrival, then replanning from the new live position. `route-demo` resolves a named landmark/demo and runs the guarded route flow; add `--plan-only` to inspect without moving. `route-goto` and `route-demo` save each leader's last route target under `tools/bot_fleet_manager/runtime/route_state/`; use `resume-route` to continue toward that saved target after a stop, crash, or interruption. `pathfind-goto` first calls the configured pathfinding service, then posts the returned waypoint list to each leader's `/movement/goto/waypoints` endpoint.

`travel-nodes` lists the long-range route registry in `tools/bot_fleet_manager/travel_nodes.json`. `travel-plan` is read-only: it chains ordinary pathfinding legs with registered travel transitions such as the Deeprun Tram, boats, portals, and future flight paths. It does not move the party. Nodes marked `survey` are placeholders that need in-game coordinates or packet/state detection before execution can be enabled. Use `--static` to plan only between registry nodes without querying a live leader position.

`travel-survey` records append-only JSONL samples under `tools/bot_fleet_manager/runtime/travel_surveys/` by default. It samples `/world/self` and `/status`, including map id, position, orientation, movement flags, movement state, combat, and health. Use `--raw` if you want full API responses in each line. `travel-capture` updates one named node in `travel_nodes.json` from the selected leader's current position. A good tram survey loop is:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-survey --leader demo-leader-1 --label deeprun-tram --interval 0.5 --min-distance 0.5
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-capture --leader demo-leader-1 --type tram_entrance --name "Stormwind Deeprun Tram Entrance" --description "Surveyed Stormwind-side tram entrance" --verified stormwind-tram-entrance
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-capture --leader demo-leader-1 --type tram_platform --name "Deeprun Tram Stormwind Platform" --description "Surveyed Stormwind-side tram platform" --verified deeprun-tram-stormwind-platform
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-capture --leader demo-leader-1 --type tram_platform --name "Deeprun Tram Ironforge Platform" --description "Surveyed Ironforge-side tram platform" --verified deeprun-tram-ironforge-platform
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json travel-capture --leader demo-leader-1 --type tram_entrance --name "Ironforge Deeprun Tram Exit" --description "Surveyed Ironforge-side tram exit" --verified ironforge-tram-exit
```

`tram-state` predicts Deeprun Tram car positions from `Data/dbfilesclient/TransportAnimation.dbc` plus the TBC Deeprun gameobject spawn rows. CMaNGOS models Deeprun cars as `GAMEOBJECT_TYPE_TRANSPORT`/ElevatorTransport objects, not as boat-style rows in the `transports` table. Because the server clock is map-local, use `--calibrate` while the leader is near a known platform to find a useful `--time-offset-ms` for the current server run. The command prints platform distance (`stationXY`), a coarse platform-window flag (`atStation`), a tighter boarding candidate flag (`boardable`/`boardableAtStation`), leader horizontal distance (`xy`), and true 3D leader distance (`dist`) because tram model origins are vertically offset from the platform floor.

`board-tram` actually boards a Deeprun car and rides it, instead of just predicting where one is. Deeprun cars are M2 transports, so `wowee_headless` boards the player client-side: `src/core/application.cpp` auto-attaches the player to any M2 transport within ~12 horizontal / ~15 vertical yards of its live tracked position (see `/world/self`'s `transport.onTransport`/`transport.guid`), the same way a real WoW client boards a moving tram or elevator without a scripted "get on" packet. So `board-tram` doesn't need the offline DBC cycle math at all: it polls `/world/entities?transports=1` for a live Deeprun car (entries 176080-176085), walks the leader onto its reported position every tick with `/movement/goto`, and watches `transport.onTransport` flip true. Once boarded it waits for the leader's (now tram-composed) position to reach the `--to` platform, then walks off toward that node's surveyed `exitSource` coordinates — standing still on a docked tram never disembarks you, since the boarding/disembark check only measures distance from the tram's *current* position, which stays constant relative to a stationary rider. Pass `--from`/`--to` as `tools/bot_fleet_manager/travel_nodes.json` node ids (e.g. `deeprun-tram-stormwind-platform` / `deeprun-tram-ironforge-platform`); omit `--from` if the leader is already standing at the platform, and omit `--to` to just board and stop there. `travel-node-execute` calls the same logic for any `ride_tram` link in a registered route.

Start the MVP pathfinding service in another terminal:

```bash
python tools/pathfinding_service/pathfinding_service.py --host 127.0.0.1 --port 8790
```

Enable it in `fleet.settings.json`:

```json
"pathfinding": {
  "enabled": true,
  "baseUrl": "http://127.0.0.1:8790",
  "coordinateSpace": "wowee-canonical",
  "timeoutSeconds": 45.0
}
```

The service supports a direct-line backend for simple contract testing and an external CMaNGOS mmap helper backend for terrain-aware waypoint generation. See `tools/pathfinding_service/README.md` for helper build and deployment details.

The manager writes generated per-leader settings under `tools/bot_fleet_manager/runtime/`, which is ignored by git.

Supervisor logs are written under `tools/bot_fleet_manager/runtime/logs/`.
