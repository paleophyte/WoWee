# Automation Examples

These scripts exercise the `wowee_headless` HTTP APIs from outside the client.

## First Party Demo

Create a local one-leader fleet config from your already-working headless settings:

```bash
python tools/bot_fleet_manager/import_headless_settings.py
```

Start the supervised leader:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json supervise
```

To start the textual team dashboard at the same time:

```bash
python tools/bot_fleet_manager/bot_fleet_manager.py tools/bot_fleet_manager/fleet.settings.json supervise --dashboard
```

Then open `http://127.0.0.1:8780`.

In another terminal, inspect the leader, party, and chat APIs:

```bash
python tools/automation_examples/party_demo.py
```

Send a party message and then read recent chat after a short wait:

```bash
python tools/automation_examples/party_demo.py --message "hello from the first automation demo"
```

The default API URL is `http://127.0.0.1:8787`. Use `--base-url` if your fleet config uses a different port.

## Leader Travel Demo

Send the leader to a named landmark, exact coordinate, or multi-waypoint route, polling status until arrival:

```bash
# Go to Goldshire (direct line)
python tools/automation_examples/travel_demo.py --landmark goldshire

# Go to an exact coordinate
python tools/automation_examples/travel_demo.py --coord -9465 62 56

# Follow a named waypoint route (Stormwind -> Goldshire, 7 waypoints)
python tools/automation_examples/travel_demo.py --route stormwind_to_goldshire

# Check current position without moving
python tools/automation_examples/travel_demo.py --here

# Stop the current movement task
python tools/automation_examples/travel_demo.py --stop
```

Available landmarks: `goldshire`, `stormwind`, `elwynn_forest`.

Available named routes: `stormwind_to_goldshire` (7 waypoints).

The script polls `GET /status` every second and prints state transitions (`moving` to `arrived`, `failed`, or `movement_locked`) and waypoint progress. Press Ctrl-C to interrupt and stop the leader.
