#!/usr/bin/env python3
"""Take a taxi flight and trace position/onFlight over time via the headless HTTP API.

Written to exercise POST /taxi/activate end to end and, in particular, to get
ground-truth position data for a real flight without any of the GUI client's
rendering/animation noise - useful for diagnosing landing-position bugs (see
TODO.md: flight from Menethil to Ironforge landing on the mountain surface
instead of the underground flight point).
"""

from __future__ import annotations

import argparse
import csv
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:8787")
    parser.add_argument("--poll-interval", type=float, default=1.0, help="Seconds between /world/self polls")
    parser.add_argument("--timeout", type=float, default=300.0, help="Max seconds to wait for landing")
    parser.add_argument("--learn-first", action="store_true",
                         help="POST /learn-flight-path before activating (must be near a flight master)")
    parser.add_argument("--search-radius", type=float, default=15.0, help="Flight master search radius for --learn-first")
    parser.add_argument("--csv", help="Optional path to write the full position trace as CSV")

    dest = parser.add_mutually_exclusive_group(required=True)
    dest.add_argument("--dest-name", help="Destination taxi node, matched case-insensitively (e.g. 'Ironforge')")
    dest.add_argument("--dest-node-id", type=int, help="Exact destination taxi node id")

    args = parser.parse_args()
    base = args.base_url.rstrip("/")

    status = request_json("GET", base + "/status")
    print_json("initial status", status)

    if args.learn_first:
        learn = request_json("POST", base + "/learn-flight-path", {"searchRadius": args.search_radius})
        print_json("learn-flight-path result", learn)
        if not learn.get("ok", False):
            print(f"Error: {learn.get('error', 'unknown')}", file=sys.stderr)
            return 1

    payload: dict[str, Any] = {}
    if args.dest_name:
        payload["destName"] = args.dest_name
    else:
        payload["destNodeId"] = args.dest_node_id

    activate = request_json("POST", base + "/taxi/activate", payload)
    print_json("taxi/activate result", activate)
    if not activate.get("ok", False):
        print(f"Error: {activate.get('error', 'unknown')}", file=sys.stderr)
        return 1

    trace: list[dict[str, Any]] = []
    deadline = time.monotonic() + args.timeout
    last_pos: tuple[float, float, float] | None = None
    was_on_flight = False
    start_time = time.monotonic()

    print(f"\nPolling /world/self every {args.poll_interval}s (timeout {args.timeout}s)...")
    try:
        while time.monotonic() < deadline:
            self_info = request_json("GET", base + "/world/self", timeout=args.poll_interval + 1.0)
            pos = self_info.get("position", {})
            x, y, z = pos.get("x", 0.0), pos.get("y", 0.0), pos.get("z", 0.0)
            on_flight = bool(self_info.get("taxi", {}).get("onFlight", False))
            t = time.monotonic() - start_time

            moved = 0.0
            if last_pos is not None:
                moved = ((x - last_pos[0]) ** 2 + (y - last_pos[1]) ** 2 + (z - last_pos[2]) ** 2) ** 0.5
            last_pos = (x, y, z)

            trace.append({"t": round(t, 2), "x": x, "y": y, "z": z, "onFlight": on_flight, "moved": round(moved, 3)})
            print(f"  [{t:6.1f}s] pos=({x:.1f}, {y:.1f}, {z:.1f}) onFlight={on_flight} moved={moved:.2f}")

            if was_on_flight and not on_flight:
                print(f"\nLanded after {t:.1f}s at ({x:.2f}, {y:.2f}, {z:.2f}), mapId={self_info.get('mapId')}")
                break
            was_on_flight = on_flight

            time.sleep(args.poll_interval)
        else:
            print(f"\nTimed out after {args.timeout}s still on flight.", file=sys.stderr)
            if args.csv:
                write_csv(args.csv, trace)
            return 1
    except KeyboardInterrupt:
        print("\nInterrupted; leaving flight in progress (no cancel API for an active taxi flight).")
        if args.csv:
            write_csv(args.csv, trace)
        return 130

    if not any(sample["onFlight"] for sample in trace):
        print("\nWarning: onFlight was never true during polling - activation may not have taken.", file=sys.stderr)

    if args.csv:
        write_csv(args.csv, trace)

    print(f"\n{len(trace)} samples over {trace[-1]['t']:.1f}s. Total straight-line samples logged above; "
          "compare the final landing position against the destination's known flight-point coordinates.")
    return 0


def write_csv(path: str, trace: list[dict[str, Any]]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["t", "x", "y", "z", "onFlight", "moved"])
        writer.writeheader()
        writer.writerows(trace)
    print(f"\nWrote {len(trace)} samples to {path}")


if __name__ == "__main__":
    raise SystemExit(main())
