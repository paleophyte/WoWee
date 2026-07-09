#!/usr/bin/env python3
"""Small HTTP pathfinding service boundary for WoWee fleet automation."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urlparse


Point = dict[str, float]


def point_distance(a: Point, b: Point) -> float:
    dx = b["x"] - a["x"]
    dy = b["y"] - a["y"]
    dz = b["z"] - a["z"]
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def json_response(handler: BaseHTTPRequestHandler, status: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Access-Control-Allow-Origin", "*")
    handler.end_headers()
    handler.wfile.write(body)


def read_json(handler: BaseHTTPRequestHandler) -> dict[str, Any]:
    length = int(handler.headers.get("Content-Length", "0"))
    if length <= 0:
        return {}
    raw = handler.rfile.read(length)
    return json.loads(raw.decode("utf-8"))


def require_number(doc: dict[str, Any], key: str) -> float:
    value = doc.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"missing numeric field: {key}")
    if not math.isfinite(float(value)):
        raise ValueError(f"non-finite numeric field: {key}")
    return float(value)


def parse_point(doc: Any, name: str) -> Point:
    if not isinstance(doc, dict):
        raise ValueError(f"{name} must be an object")
    return {
        "x": require_number(doc, "x"),
        "y": require_number(doc, "y"),
        "z": require_number(doc, "z"),
    }


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def direct_path(start: Point, end: Point, step_yards: float) -> list[Point]:
    dx = end["x"] - start["x"]
    dy = end["y"] - start["y"]
    dz = end["z"] - start["z"]
    distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    if distance <= 0.001:
        return [dict(end)]
    segments = max(1, int(math.ceil(distance / max(1.0, step_yards))))
    return [
        {
            "x": lerp(start["x"], end["x"], i / segments),
            "y": lerp(start["y"], end["y"], i / segments),
            "z": lerp(start["z"], end["z"], i / segments),
        }
        for i in range(1, segments + 1)
    ]


def densify_path(waypoints: list[Point], step_yards: float) -> list[Point]:
    if not waypoints:
        return []
    step_yards = max(1.0, step_yards)
    dense: list[Point] = []
    previous: Point | None = None
    for waypoint in waypoints:
        current = dict(waypoint)
        if previous is None:
            dense.append(current)
            previous = current
            continue
        distance = point_distance(previous, current)
        segments = max(1, int(math.ceil(distance / step_yards)))
        for i in range(1, segments + 1):
            t = i / segments
            dense.append({
                "x": lerp(previous["x"], current["x"], t),
                "y": lerp(previous["y"], current["y"], t),
                "z": lerp(previous["z"], current["z"], t),
            })
        previous = current
    return dense


class PathService:
    def __init__(
        self,
        backend: str,
        helper: str | None,
        helper_timeout: float,
        direct_step_yards: float,
        waypoint_step_yards: float,
        waypoint_arrival_radius: float,
        cmangos_data_dir: str | None,
        tile_mode: str,
        tile_margin: int,
        smooth_z_weight: float = 0.15,
        smooth_z_min_change: float = 0.6,
    ):
        self.backend = backend
        self.helper = helper
        self.helper_timeout = helper_timeout
        self.direct_step_yards = direct_step_yards
        self.waypoint_step_yards = waypoint_step_yards
        self.waypoint_arrival_radius = waypoint_arrival_radius
        self.cmangos_data_dir = cmangos_data_dir
        self.tile_mode = tile_mode
        self.tile_margin = tile_margin
        self.smooth_z_weight = smooth_z_weight
        self.smooth_z_min_change = smooth_z_min_change

    def health(self) -> dict[str, Any]:
        return {
            "ok": True,
            "backend": self.backend,
            "helperConfigured": bool(self.helper),
            "cmangosDataDirConfigured": bool(self.cmangos_data_dir),
            "tileMode": self.tile_mode,
            "tileMargin": self.tile_margin,
            "directStepYards": self.direct_step_yards,
            "waypointStepYards": self.waypoint_step_yards,
            "waypointArrivalRadius": self.waypoint_arrival_radius,
            "smoothZWeight": self.smooth_z_weight,
            "smoothZMinChange": self.smooth_z_min_change,
        }

    def path(self, request: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        try:
            map_id, start, end = self.parse_path_request(request)
        except ValueError as exc:
            return 400, {"ok": False, "error": str(exc)}

        return self.find_path(map_id, start, end)

    def route(self, request: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        try:
            map_id, start, end = self.parse_path_request(request)
        except ValueError as exc:
            return 400, {"ok": False, "error": str(exc)}
        try:
            max_legs = int(request.get("maxLegs", 8))
            arrival_radius = float(request.get("arrivalRadius", 5.0))
            min_progress_yards = float(request.get("minProgressYards", 15.0))
            if max_legs < 1 or max_legs > 64:
                raise ValueError("maxLegs must be between 1 and 64")
            if arrival_radius < 0.1:
                raise ValueError("arrivalRadius must be at least 0.1")
            if min_progress_yards < 0.1:
                raise ValueError("minProgressYards must be at least 0.1")
        except (TypeError, ValueError) as exc:
            return 400, {"ok": False, "error": str(exc)}

        legs: list[dict[str, Any]] = []
        combined: list[Point] = []
        current = dict(start)
        status = "partial"
        warnings: list[str] = []

        for leg_index in range(max_legs):
            before_goal = point_distance(current, end)
            if before_goal <= arrival_radius:
                status = "complete"
                break

            code, result = self.find_path(map_id, current, end)
            if code != 200 or not result.get("ok", False):
                result.setdefault("ok", False)
                result.setdefault("legIndex", leg_index)
                return code, result

            waypoints = result.get("waypoints", [])
            if not isinstance(waypoints, list) or not waypoints:
                warnings.append(f"leg {leg_index + 1}: pathfinding returned no waypoints")
                status = "stalled"
                break

            last = parse_point(waypoints[-1], "last waypoint")
            after_goal = point_distance(last, end)
            progress = before_goal - after_goal
            leg = {
                "index": leg_index + 1,
                "start": current,
                "end": last,
                "requestedEnd": end,
                "pathType": result.get("pathType", "unknown"),
                "backend": result.get("backend", "unknown"),
                "waypointCount": len(waypoints),
                "loadedTiles": result.get("loadedTiles"),
                "polyCount": result.get("polyCount"),
                "distanceToGoalBefore": before_goal,
                "distanceToGoalAfter": after_goal,
                "progressYards": progress,
            }
            legs.append(leg)

            for waypoint in waypoints:
                point = parse_point(waypoint, "waypoint")
                if combined and point_distance(combined[-1], point) < 0.01:
                    continue
                combined.append(point)

            current = last
            if after_goal <= arrival_radius:
                status = "complete"
                break
            if progress < min_progress_yards:
                warnings.append(
                    f"leg {leg_index + 1}: progress {progress:.2f} yards is below minProgressYards"
                )
                status = "stalled"
                break

        if status != "complete" and len(legs) >= max_legs:
            status = "max_legs"

        return 200, {
            "ok": True,
            "backend": self.backend if self.backend != "external" else "cmangos-mmap",
            "routeStatus": status,
            "coordinateSpace": "wowee-canonical",
            "mapId": map_id,
            "start": start,
            "end": end,
            "arrivalRadius": arrival_radius,
            "legs": legs,
            "waypoints": combined,
            "warnings": warnings,
        }

    def parse_path_request(self, request: dict[str, Any]) -> tuple[int, Point, Point]:
        map_id = int(require_number(request, "mapId"))
        start = parse_point(request.get("start"), "start")
        end = parse_point(request.get("end"), "end")
        coordinate_space = str(request.get("coordinateSpace", "wowee-canonical"))
        output_space = str(request.get("outputSpace", coordinate_space))
        if coordinate_space != "wowee-canonical" or output_space != "wowee-canonical":
            raise ValueError("only wowee-canonical coordinates are supported by this service boundary")
        return map_id, start, end

    def find_path(self, map_id: int, start: Point, end: Point) -> tuple[int, dict[str, Any]]:
        if self.backend == "direct":
            waypoints = direct_path(start, end, self.direct_step_yards)
            return 200, {
                "ok": True,
                "backend": "direct",
                "pathType": "direct",
                "coordinateSpace": "wowee-canonical",
                "mapId": map_id,
                "waypoints": waypoints,
                "warnings": ["direct backend does not avoid terrain or collision"],
            }

        if self.backend == "external":
            return self.external_path({
                "mapId": map_id,
                "start": start,
                "end": end,
                "coordinateSpace": "wowee-canonical",
                "outputSpace": "wowee-canonical",
                "dataDir": self.cmangos_data_dir,
                "tileMode": self.tile_mode,
                "tileMargin": self.tile_margin,
                "waypointStepYards": self.waypoint_step_yards,
                "waypointArrivalRadius": self.waypoint_arrival_radius,
                "smoothZWeight": self.smooth_z_weight,
                "smoothZMinChange": self.smooth_z_min_change,
            })

        return 501, {
            "ok": False,
            "error": f"backend is not implemented: {self.backend}",
            "backend": self.backend,
        }

    def external_path(self, request: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        if not self.helper:
            return 500, {"ok": False, "error": "external backend requires --helper"}
        try:
            proc = subprocess.run(
                [self.helper],
                input=json.dumps(request),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=self.helper_timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return 502, {"ok": False, "error": f"path helper failed: {exc}"}
        try:
            payload = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            if proc.returncode != 0:
                return 502, {
                    "ok": False,
                    "error": f"path helper exited {proc.returncode}",
                    "stderr": proc.stderr[-2000:],
                }
            return 502, {"ok": False, "error": f"path helper returned invalid JSON: {exc}"}
        if proc.returncode != 0:
            payload.setdefault("ok", False)
            payload.setdefault("error", f"path helper exited {proc.returncode}")
            payload.setdefault("stderr", proc.stderr[-2000:])
            return 502, payload
        status = 200 if payload.get("ok", False) else 502
        return status, payload


def make_handler(service: PathService) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "WoWeePathService/0.1"

        def log_message(self, fmt: str, *args: Any) -> None:
            print(f"{self.address_string()} - {fmt % args}", file=sys.stderr)

        def do_OPTIONS(self) -> None:
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
            self.end_headers()

        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path == "/health":
                json_response(self, 200, service.health())
                return
            json_response(self, 404, {"ok": False, "error": "not found"})

        def do_POST(self) -> None:
            path = urlparse(self.path).path
            if path not in ("/path", "/route"):
                json_response(self, 404, {"ok": False, "error": "not found"})
                return
            try:
                request = read_json(self)
            except json.JSONDecodeError as exc:
                json_response(self, 400, {"ok": False, "error": f"invalid JSON: {exc}"})
                return
            if path == "/route":
                status, payload = service.route(request)
            else:
                status, payload = service.path(request)
            json_response(self, status, payload)

    return Handler


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--backend", choices=("direct", "external", "cmangos-mmap"), default="direct")
    parser.add_argument("--helper", default="", help="External path helper executable for --backend external")
    parser.add_argument("--helper-timeout", type=float, default=5.0)
    parser.add_argument("--direct-step-yards", type=float, default=35.0)
    parser.add_argument("--waypoint-step-yards", type=float, default=4.0, help="Maximum spacing for mmap helper output waypoints")
    parser.add_argument("--waypoint-arrival-radius", type=float, default=1.5, help="Per-waypoint arrival radius for mmap helper output")
    parser.add_argument("--cmangos-data-dir", default="", help="CMaNGOS DataDir containing mmaps/ for the external mmap helper")
    parser.add_argument("--tile-mode", choices=("all", "bbox"), default="all", help="Tile loading mode passed to the external mmap helper")
    parser.add_argument("--tile-margin", type=int, default=2, help="Extra grid margin when --tile-mode=bbox")
    parser.add_argument("--smooth-z-weight", type=float, default=0.15, help="Z low-pass filter weight (0=no smoothing, 1=no filter)")
    parser.add_argument("--smooth-z-min-change", type=float, default=0.6, help="Minimum Z change per horizontal yard to preserve (stairs/ramps)")
    args = parser.parse_args(argv)

    service = PathService(
        args.backend,
        args.helper,
        args.helper_timeout,
        args.direct_step_yards,
        args.waypoint_step_yards,
        args.waypoint_arrival_radius,
        args.cmangos_data_dir,
        args.tile_mode,
        args.tile_margin,
        smooth_z_weight=args.smooth_z_weight,
        smooth_z_min_change=args.smooth_z_min_change,
    )
    server = ThreadingHTTPServer((args.host, args.port), make_handler(service))
    print(f"Pathfinding service listening on http://{args.host}:{args.port} backend={args.backend}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nPathfinding service stopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
