# WoWee Headless Client

`wowee_headless` is a minimal terminal/world client for automation. It logs into a WoW-compatible auth server, selects a realm and character, enters the world, then exposes a small localhost HTTP API for chat.

## Settings

Copy `settings.example.json` to `settings.json` and fill in:

- `auth.host`, `auth.port`, `auth.account`, `auth.password`
- `client.*` for the client version/build. WotLK 3.3.5a defaults are already set.
- `client.expansion`: `classic`, `tbc`, `wotlk`, or `turtle`
- `realm.name` or `realm.index`
- `character.name`
- `bots.names` for automatic `.bot add <name>` commands after world entry
- `automation.onEnterWorldCommands` for arbitrary chat/GM commands after world entry
- `api.bind` and `api.port`

Run:

```bash
wowee_headless tools/headless_client/settings.json
```

While running, press `Esc` to request an in-game logout and exit after the server responds. Press `Ctrl-C` to quit the process immediately.

Example bot startup:

```json
{
  "bots": {
    "enabled": true,
    "names": ["Soulweaver", "Leatherfang"]
  },
  "automation": {
    "commandDelaySeconds": 0.25,
    "onEnterWorldCommands": [".bot add Wildbrew"]
  }
}
```

Both sections are optional. `bots.names` expands to `.bot add <name>`, while `automation.onEnterWorldCommands` sends each command exactly as written.

## Emote Text

For friendlier `TEXT_EMOTE` messages, the headless client can load emote strings from extracted client DBC files. Provide these files in either `Data/DBFilesClient/` or `Data/db/`:

- `EmotesText.dbc`
- `EmotesTextData.dbc`
- `Emotes.dbc`

If `Data/manifest.json` exists from `asset_extract`, the normal asset manifest is used. If not, `wowee_headless` falls back to a DBC-only mode and still checks the loose DBC paths above. Without these files, it uses a small built-in fallback table.

## API

Status:

```bash
curl http://127.0.0.1:8787/status
```

World self:

```bash
curl http://127.0.0.1:8787/world/self
```

Party:

```bash
curl http://127.0.0.1:8787/party
```

Queue a raw command:

```bash
curl -X POST http://127.0.0.1:8787/commands \
  -H "Content-Type: application/json" \
  -d "{\"command\":\".bot follow\"}"
```

Move the leader toward coordinates on the current map:

```bash
curl -X POST http://127.0.0.1:8787/movement/goto \
  -H "Content-Type: application/json" \
  -d "{\"mapId\":530,\"x\":-248.0,\"y\":951.0,\"z\":84.0,\"arrivalRadius\":3.0}"
```

Move through a sequence of waypoints:

```bash
curl -X POST http://127.0.0.1:8787/movement/goto/waypoints \
  -H "Content-Type: application/json" \
  -d '{
    "mapId": 0,
    "arrivalRadius": 5.0,
    "waypoints": [
      {"x": -8970, "y": 610, "z": 95},
      {"x": -9050, "y": 520, "z": 92},
      {"x": -9465, "y": 62, "z": 56}
    ]
  }'
```

### Coordinate Convention

The headless client's position and movement API use **game-world coordinates `(game.x, game.y)`**:
- `game.x` = north-south axis (positive = north)
- `game.y` = east-west axis (positive = east)

The `tools.pathfinding.pathfinder` uses **canonical ADT coordinates `(wx, wy)`** where:
- `wx` = east-west (= headless `position.y`)
- `wy` = north-south (= headless `position.x`)

When using the pathfinder output, swap axes before sending to the movement API:
```python
# pathfinder returns (wx, wy)
headless_waypoint = {"x": wy, "y": wx, "z": wz}
```

The `travel_demo.py --pathfind` flag handles this conversion automatically.

Each waypoint can optionally override the arrival radius:

```json
{"x": -8970, "y": 610, "z": 95, "arrivalRadius": 8.0}
```

Stop leader movement:

```bash
curl -X POST http://127.0.0.1:8787/movement/stop \
  -H "Content-Type: application/json" \
  -d "{\"reason\":\"manual stop\"}"
```

The `GET /status` endpoint reports waypoint progress:

```json
"movement": {
  "state": "moving",
  "waypointIndex": 2,
  "waypointCount": 3,
  ...
}
```

Read chat:

```bash
curl "http://127.0.0.1:8787/chat?after=0&limit=50"
```

Send say:

```bash
curl -X POST http://127.0.0.1:8787/chat \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"say\",\"message\":\"hello from automation\"}"
```

Send whisper:

```bash
curl -X POST http://127.0.0.1:8787/chat \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"whisper\",\"target\":\"Playername\",\"message\":\"hello\"}"
```

Send channel:

```bash
curl -X POST http://127.0.0.1:8787/chat \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"channel\",\"target\":\"world\",\"message\":\"hello\"}"
```

Supported send types: `say`, `yell`, `whisper`, `channel`, `party`, `guild`, `raid`, `officer`.
