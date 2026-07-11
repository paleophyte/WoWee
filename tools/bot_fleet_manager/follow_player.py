"""Have a headless leader trail a live player around, logging waypoints as it goes.

Polls the leader's own /world/entities for a PLAYER by name, and re-issues
/movement/goto toward that player's live position whenever they've moved far
enough away. Each commanded position is appended to a JSON waypoint log so a
survey run can be turned into travel_nodes.json entries afterward.

Usage:
    python follow_player.py <api_base> <player_name> [--follow-distance 12]
                             [--repath-threshold 15] [--poll-interval 2.0]
                             [--log <path>]
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def http_json(url: str, method: str = "GET", payload: dict | None = None, timeout: float = 10.0):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method,
                                  headers={"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read())


def find_player(api_base: str, name: str, radius: float = 150.0):
    result = http_json(f"{api_base}/world/entities?radius={radius}")
    for entity in result.get("entities", []):
        if entity.get("type") == "PLAYER" and entity.get("name", "").lower() == name.lower():
            return entity
    return None


def point_distance(a: dict, b: dict) -> float:
    return ((a["x"] - b["x"]) ** 2 + (a["y"] - b["y"]) ** 2 + (a["z"] - b["z"]) ** 2) ** 0.5


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("api_base")
    parser.add_argument("player_name")
    parser.add_argument("--follow-distance", type=float, default=12.0,
                         help="arrivalRadius for each goto - how close the leader stops behind the target")
    parser.add_argument("--repath-threshold", type=float, default=15.0,
                         help="minimum player movement (yards) before issuing a new goto")
    parser.add_argument("--poll-interval", type=float, default=2.0)
    parser.add_argument("--log", default="tools/bot_fleet_manager/runtime/follow_waypoints.json")
    parser.add_argument("--search-radius", type=float, default=150.0)
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    waypoints: list[dict] = []

    print(f"waiting to spot {args.player_name} within {args.search_radius}y...")
    last_target: dict | None = None
    last_map_id = None

    while True:
        try:
            self_info = http_json(f"{args.api_base}/world/self")
        except Exception as exc:
            print(f"leader status check failed: {exc}")
            time.sleep(args.poll_interval)
            continue

        if not self_info.get("inWorld"):
            print("leader not in world, waiting...")
            time.sleep(args.poll_interval)
            continue

        health = self_info.get("health", {})
        if health.get("isPlayerDead") or health.get("isDead"):
            print("leader is dead, stopping follow")
            break

        try:
            target = find_player(args.api_base, args.player_name, args.search_radius)
        except Exception as exc:
            print(f"entity scan failed: {exc}")
            time.sleep(args.poll_interval)
            continue

        if target is None:
            time.sleep(args.poll_interval)
            continue

        map_id = self_info.get("mapId")
        pos = target["position"]
        if last_target is None:
            print(f"spotted {args.player_name} at ({pos['x']:.1f},{pos['y']:.1f},{pos['z']:.1f}) "
                  f"distance={target.get('distance', 0):.1f}y, following")

        moved_enough = last_target is None or point_distance(pos, last_target) >= args.repath_threshold
        map_changed = map_id != last_map_id

        if moved_enough or map_changed:
            try:
                http_json(f"{args.api_base}/movement/goto", "POST", {
                    "mapId": map_id, "x": pos["x"], "y": pos["y"], "z": pos["z"],
                    "arrivalRadius": args.follow_distance,
                })
            except Exception as exc:
                print(f"goto failed: {exc}")
                time.sleep(args.poll_interval)
                continue
            entry = {
                "t": time.time(), "mapId": map_id,
                "x": pos["x"], "y": pos["y"], "z": pos["z"],
            }
            waypoints.append(entry)
            log_path.write_text(json.dumps(waypoints, indent=2))
            print(f"-> following to ({pos['x']:.1f},{pos['y']:.1f},{pos['z']:.1f}) mapId={map_id} "
                  f"[{len(waypoints)} waypoints logged]")
            last_target = pos
            last_map_id = map_id

        time.sleep(args.poll_interval)

    return 0


if __name__ == "__main__":
    sys.exit(main())
