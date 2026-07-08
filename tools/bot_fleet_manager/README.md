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
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json command ".bot follow"
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json goto 530 -248.0 951.0 84.0
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json stop
```

Target one group or one leader:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json status --fleet alpha
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json command --leader leader-1 ".bot follow"
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json goto --fleet beta 530 -248.0 951.0 84.0
```

Set each leader's `fleet` field in `fleet.settings.json` to create assignment groups.

The manager writes generated per-leader settings under `tools/bot_fleet_manager/runtime/`, which is ignored by git.

Supervisor logs are written under `tools/bot_fleet_manager/runtime/logs/`.
