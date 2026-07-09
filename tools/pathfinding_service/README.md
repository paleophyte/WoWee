# WoWee Pathfinding Service

This is the first pathfinding service boundary for fleet automation.

The MVP service provides the same HTTP contract that the fleet manager will use once a real CMaNGOS mmap/Detour backend exists. For now, the built-in `direct` backend returns a subdivided straight-line route. It is useful for testing transport, waypoint handoff, dashboard status, and automation scripts, but it does not avoid terrain, buildings, water, cliffs, or zone boundaries.

## Run

```bash
python tools/pathfinding_service/pathfinding_service.py --host 127.0.0.1 --port 8790
```

Check health:

```bash
curl http://127.0.0.1:8790/health
```

Ask for a path in WoWee canonical coordinates:

```bash
curl -X POST http://127.0.0.1:8790/path ^
  -H "Content-Type: application/json" ^
  -d "{\"mapId\":0,\"start\":{\"x\":-8833,\"y\":626,\"z\":94},\"end\":{\"x\":-9465,\"y\":62,\"z\":56}}"
```

The response includes `waypoints` that can be posted directly to a headless client's `/movement/goto/waypoints` endpoint.

Ask for an iterative route plan without moving anything:

```bash
curl -X POST http://127.0.0.1:8790/route ^
  -H "Content-Type: application/json" ^
  -d "{\"mapId\":0,\"start\":{\"x\":620.3,\"y\":-8885.53,\"z\":95.49},\"end\":{\"x\":-834.06,\"y\":-5021.0,\"z\":495.32},\"maxLegs\":8,\"arrivalRadius\":5}"
```

`/route` repeatedly calls the configured path backend from the last reachable waypoint toward the destination. It returns `routeStatus`:

- `complete`: the final waypoint is within `arrivalRadius`.
- `partial`: more work is needed but the planner has not declared a failure.
- `stalled`: a leg did not make enough progress.
- `max_legs`: the planner used all allowed legs before arriving.

This is intentionally a planner contract, not a transport system. Cross-map travel, boats, zeppelins, trams, portals, and flight paths still need named travel nodes above this layer.

## Fleet Manager Use

Add a pathfinding block to `tools/bot_fleet_manager/fleet.settings.json`:

```json
{
  "pathfinding": {
    "enabled": true,
    "baseUrl": "http://127.0.0.1:8790",
    "coordinateSpace": "wowee-canonical"
  }
}
```

Then run:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json pathfind-goto 0 -9465 62 56
```

To inspect the same route planner without moving the leader:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json plan-route 0 -9465 62 56
```

## Real CMaNGOS Backend Plan

CMaNGOS already pathfinds through Recast/Detour mmaps. The durable backend should reuse that rather than inventing new map math in Python.

Build the native helper from WoWee. By default CMake looks for CMaNGOS at `C:/Users/admin/code/mangos-tbc`; override that path with `-DWOWEE_MANGOS_TBC_DIR=...` if needed.

```bash
cmake -S . -B build -G Ninja -DWOWEE_MANGOS_TBC_DIR=C:/Users/admin/code/mangos-tbc
cmake --build build --target mmap_path_query
```

The helper reads one JSON request on stdin and writes one JSON response on stdout. It needs a CMaNGOS-style `DataDir` containing `mmaps/`.

```bash
echo "{\"mapId\":0,\"dataDir\":\"/opt/cmangos/data\",\"start\":{\"x\":-8833,\"y\":626,\"z\":94},\"end\":{\"x\":-9465,\"y\":62,\"z\":56}}" \
  | build/bin/mmap_path_query
```

The service can wrap that helper through the `external` backend:

```bash
python tools/pathfinding_service/pathfinding_service.py \
  --backend external \
  --helper /opt/wowee/bin/mmap_path_query \
  --helper-timeout 30 \
  --cmangos-data-dir /opt/cmangos/data \
  --tile-mode bbox \
  --tile-margin 2 \
  --waypoint-step-yards 4 \
  --waypoint-arrival-radius 1.5
```

Use `--tile-mode bbox --tile-margin 2` for normal service use. It loads only the tiles around the requested route and is much faster than `all`. Keep `--tile-mode all` as a diagnostic fallback if a route crosses a long distance or returns a suspicious partial/no-path result.

Use `--waypoint-step-yards` to control how densely the mmap helper samples Detour's straight path. Smaller values produce more waypoints and better stair/bridge surface following, at the cost of more movement updates. The helper projects intermediate samples back onto the navmesh height where possible. `--waypoint-arrival-radius` is written into each returned waypoint so the headless mover does not skip dense samples when a fleet command uses a broader route arrival radius.

This improves visual movement, but it is still only as good as the CMaNGOS mmap data. If the mmap considers a tree, building corner, or decorative bridge edge walkable, the service cannot infer a better path from navmesh data alone.

When running the service on the CMaNGOS host, point the fleet manager at it:

```json
{
  "pathfinding": {
    "enabled": true,
    "baseUrl": "http://CMANGOS_HOST:8790",
    "coordinateSpace": "wowee-canonical",
    "timeoutSeconds": 45.0
  }
}
```

The bbox helper backend is fast for nearby and short routes. Keep the service `--helper-timeout` and fleet `timeoutSeconds` comfortably above the observed query time so longer diagnostic `all` calls can still complete.

The helper contract is JSON on stdin/stdout:

Input:

```json
{
  "mapId": 0,
  "start": {"x": -8833.0, "y": 626.0, "z": 94.0},
  "end": {"x": -9465.0, "y": 62.0, "z": 56.0},
  "coordinateSpace": "wowee-canonical",
  "outputSpace": "wowee-canonical",
  "waypointStepYards": 4,
  "waypointArrivalRadius": 1.5
}
```

Output:

```json
{
  "ok": true,
  "backend": "cmangos-mmap",
  "pathType": "navmesh",
  "coordinateSpace": "wowee-canonical",
  "mapId": 0,
  "waypoints": [
    {"x": -9000.0, "y": 500.0, "z": 90.0},
    {"x": -9465.0, "y": 62.0, "z": 56.0}
  ]
}
```

Coordinate reminder: WoWee APIs expose canonical coordinates where `x = north`, `y = west`, `z = up`. CMaNGOS server/wire coordinates use `x = west`, `y = north`, `z = up`, so the native helper should swap X/Y at the CMaNGOS boundary and return canonical waypoints to this service.
