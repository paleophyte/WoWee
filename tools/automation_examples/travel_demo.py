#!/usr/bin/env python3
"""Demo leader travel: goto a coordinate, follow waypoints, poll status until arrival."""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from typing import Any


def request_json(method: str, url: str, payload: dict[str, Any] | None = None, timeout: float = 5.0) -> dict[str, Any]:
    if not url.startswith(("http://", "https://")):
        raise ValueError(f"refusing non-http(s) URL: {url}")
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    # url is validated to http(s) above; this calls our own local fleet API, not attacker-controlled input.
    with urllib.request.urlopen(req, timeout=timeout) as response:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
        return json.loads(response.read().decode("utf-8"))


def print_json(label: str, doc: dict[str, Any]) -> None:
    print(f"\n== {label} ==")
    print(json.dumps(doc, indent=2, sort_keys=True))


def resolve_destination(name: str) -> dict[str, float]:
    """Return {mapId, x, y, z} for a named landmark on the Eastern Kingdoms map."""
    landmarks: dict[str, dict[str, float]] = {
        "goldshire": {"mapId": 0, "x": -9465.0, "y": 62.0, "z": 56.0},
        "stormwind": {"mapId": 0, "x": -8833.0, "y": 626.0, "z": 94.0},
        "elwynn_forest": {"mapId": 0, "x": -9200.0, "y": -50.0, "z": 73.0},
    }
    key = name.lower().replace(" ", "_")
    if key in landmarks:
        return landmarks[key]
    raise ValueError(f"Unknown landmark: {name}. Known: {', '.join(sorted(landmarks.keys()))}")


def resolve_route(name: str) -> list[dict[str, float]]:
    """Return a list of {x, y, z} waypoints for a named route."""
    routes: dict[str, list[dict[str, float]]] = {
        "stormwind_to_goldshire": [
            {"x": -8833.0, "y": 626.0, "z": 94.0},
            {"x": -8970.0, "y": 610.0, "z": 95.0},
            {"x": -9050.0, "y": 520.0, "z": 92.0},
            {"x": -9150.0, "y": 350.0, "z": 82.0},
            {"x": -9260.0, "y": 220.0, "z": 75.0},
            {"x": -9360.0, "y": 130.0, "z": 65.0},
            {"x": -9465.0, "y": 62.0, "z": 56.0},
        ],
    }
    key = name.lower().replace(" ", "_")
    if key in routes:
        return routes[key]
    raise ValueError(f"Unknown route: {name}. Known: {', '.join(sorted(routes.keys()))}")


STATE_ARRIVED = "arrived"
STATE_FAILED = "failed"
STATE_MOVING = "moving"
STATE_IDLE = "idle"
STATE_LOCKED = "movement_locked"

TERMINAL_STATES = {STATE_ARRIVED, STATE_FAILED, STATE_LOCKED}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:8787")
    parser.add_argument("--poll-interval", type=float, default=1.0, help="Seconds between status polls")
    parser.add_argument("--timeout", type=float, default=120.0, help="Max seconds to wait for arrival")
    parser.add_argument("--arrival-radius", type=float, default=5.0)

    dest = parser.add_mutually_exclusive_group(required=True)
    dest.add_argument("--landmark", help="Named destination (goldshire, stormwind, elwynn_forest)")
    dest.add_argument("--coord", nargs=3, metavar=("X", "Y", "Z"), type=float, help="Exact coordinates")
    dest.add_argument("--route", help="Named multi-waypoint route (stormwind_to_goldshire)")
    dest.add_argument("--here", action="store_true", help="Read current position and use as destination (no-op, just reports)")

    parser.add_argument("--stop", action="store_true", help="Send stop command and exit")
    parser.add_argument("--stop-reason", default="travel demo stop", help="Reason for stop")

    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    if args.stop:
        result = request_json("POST", base + "/movement/stop", {"reason": args.stop_reason})
        print_json("stop result", result)
        return 0

    status = request_json("GET", base + "/status")
    print_json("initial status", status)

    if args.here:
        self_info = request_json("GET", base + "/world/self")
        pos = self_info.get("position", {})
        print(f"\nCurrent position: x={pos.get('x')}, y={pos.get('y')}, z={pos.get('z')}")
        print(f"Map ID: {self_info.get('mapId')}")
        return 0

    if args.route:
        waypoints = resolve_route(args.route)
        print(f"\nSending waypoint route '{args.route}' with {len(waypoints)} waypoints")
        goto = request_json("POST", base + "/movement/goto/waypoints", {
            "mapId": 0,
            "waypoints": waypoints,
            "arrivalRadius": args.arrival_radius,
        })
    elif args.landmark:
        coords = resolve_destination(args.landmark)
        print(f"\nSending goto: mapId={coords['mapId']}, "
              f"x={coords['x']}, y={coords['y']}, z={coords['z']}")
        goto = request_json("POST", base + "/movement/goto", {
            "mapId": coords["mapId"],
            "x": coords["x"],
            "y": coords["y"],
            "z": coords["z"],
            "arrivalRadius": args.arrival_radius,
        })
    else:
        print(f"\nSending goto: x={args.coord[0]}, y={args.coord[1]}, z={args.coord[2]}")
        goto = request_json("POST", base + "/movement/goto", {
            "mapId": 0,
            "x": args.coord[0],
            "y": args.coord[1],
            "z": args.coord[2],
            "arrivalRadius": args.arrival_radius,
        })
    print_json("goto result", goto)

    if not goto.get("ok", False):
        print(f"Error: {goto.get('error', 'unknown')}", file=sys.stderr)
        return 1

    deadline = time.monotonic() + args.timeout
    last_state = ""
    last_waypoint = 0

    print(f"\nPolling status every {args.poll_interval}s (timeout {args.timeout}s)...")
    try:
        while time.monotonic() < deadline:
            status = request_json("GET", base + "/status", timeout=args.poll_interval + 1.0)
            movement = status.get("movement", {})
            state = movement.get("state", STATE_IDLE)
            wp_index = movement.get("waypointIndex", 0)
            wp_count = movement.get("waypointCount", 0)

            if wp_index != last_waypoint:
                print(f"  [{time.strftime('%H:%M:%S')}] waypoint {wp_index}/{wp_count}")
                last_waypoint = wp_index

            if state != last_state:
                print(f"  [{time.strftime('%H:%M:%S')}] movement state: {state}")
                last_state = state

            if state in TERMINAL_STATES:
                print_json("final status", status)
                if state == STATE_ARRIVED:
                    print("\nArrived at destination!")
                    return 0
                else:
                    error = movement.get("error", "unknown")
                    print(f"\nMovement failed: {error}", file=sys.stderr)
                    return 1

            time.sleep(args.poll_interval)
    except KeyboardInterrupt:
        print("\nInterrupted; stopping movement...")
        request_json("POST", base + "/movement/stop", {"reason": "interrupt"})
        print_json("final status", request_json("GET", base + "/status"))
        return 130

    print(f"\nTimed out after {args.timeout}s.", file=sys.stderr)
    print_json("timeout status", request_json("GET", base + "/status"))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
