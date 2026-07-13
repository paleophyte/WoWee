"""Predict Deeprun Tram car positions from CMaNGOS TBC data.

CMaNGOS models Deeprun cars as GAMEOBJECT_TYPE_TRANSPORT (type 11)
ElevatorTransport objects. Their movement is derived from static DB spawns
plus TransportAnimation.dbc offsets, not the `transports` table used by
boats and zeppelins.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import struct
import time
from pathlib import Path
from typing import Any


DEEPRUN_MAP_ID = 369
DEEPRUN_TRAM_ENTRIES = range(176080, 176087)
STATION_WINDOW_YARDS = 60.0
BOARDING_WINDOW_YARDS = 22.0


@dataclass(frozen=True)
class TramSpawn:
    guid: int
    entry: int
    map_id: int
    x: float
    y: float
    z: float
    orientation: float
    rotation0: float
    rotation1: float
    rotation2: float
    rotation3: float


@dataclass(frozen=True)
class TramFrame:
    record_id: int
    entry: int
    time_ms: int
    x: float
    y: float
    z: float
    sequence_id: int


@dataclass(frozen=True)
class PredictedTram:
    guid: int
    entry: int
    map_id: int
    cycle_ms: int
    time_ms: int
    x: float
    y: float
    z: float
    local_x: float
    local_y: float
    local_z: float
    nearest_station: str
    station_xy_distance: float
    at_station: bool
    boardable_at_station: bool
    xy_distance_to_leader: float | None = None
    distance_to_leader: float | None = None


# Snapshot from tbcmangos.gameobject for entries 176080-176085. These are
# stable client-data spawns for TBC Deeprun; if a server customizes them,
# regenerate this list from the DB.
DEEPRUN_TBC_SPAWNS: tuple[TramSpawn, ...] = (
    TramSpawn(18802, 176080, 369, 4.580639839172363, 28.209699630737305, 7.011069774627686, 1.5707999467849731, 0.0, 0.0, 0.7071070075035095, 0.7071070075035095),
    TramSpawn(18803, 176081, 369, 4.528069972991943, 8.435290336608887, 7.011069774627686, 1.5707999467849731, 0.0, 0.0, 0.7071070075035095, 0.7071070075035095),
    TramSpawn(18804, 176082, 369, -45.4005012512207, 2492.7900390625, 6.98859977722168, 1.5707999467849731, 0.0, 0.0, 0.7071070075035095, 0.7071070075035095),
    TramSpawn(18805, 176083, 369, -45.400699615478516, 2512.14990234375, 6.98859977722168, 1.5707999467849731, 0.0, 0.0, 0.7071070075035095, 0.7071070075035095),
    TramSpawn(18806, 176084, 369, -45.39339828491211, 2472.929931640625, 6.98859977722168, 1.5707999467849731, 0.0, 0.0, 0.7071070075035095, 0.7071070075035095),
    TramSpawn(18807, 176085, 369, 4.49882984161377, -11.34749984741211, 7.011069774627686, -1.5707999467849731, 0.0, 0.0, -0.7071070075035095, 0.7071070075035095),
)


TRACK_STATIONS: tuple[tuple[str, float, float], ...] = (
    ("ironforge", 8.11925983428955, 11.050399780273438),
    ("stormwind", -20.4, 2492.8),
)


def load_transport_animation(path: Path) -> dict[int, list[TramFrame]]:
    with path.open("rb") as handle:
        magic = handle.read(4)
        if magic != b"WDBC":
            raise ValueError(f"{path} is not a WDBC file")
        record_count, field_count, record_size, _string_size = struct.unpack("<4I", handle.read(16))
        if field_count != 7 or record_size != 28:
            raise ValueError(f"unexpected TransportAnimation.dbc layout: fields={field_count} recordSize={record_size}")

        by_entry: dict[int, list[TramFrame]] = {}
        for _ in range(record_count):
            record = handle.read(record_size)
            record_id, entry, time_ms, x, y, z, sequence_id = struct.unpack("<IIIfffI", record)
            if entry in DEEPRUN_TRAM_ENTRIES:
                by_entry.setdefault(entry, []).append(TramFrame(record_id, entry, time_ms, x, y, z, sequence_id))

    for frames in by_entry.values():
        frames.sort(key=lambda frame: frame.time_ms)
    return by_entry


def rotate_by_quaternion(x: float, y: float, z: float, qx: float, qy: float, qz: float, qw: float) -> tuple[float, float, float]:
    # CMaNGOS/G3D uses vector * quaternion in ElevatorTransport. That is the
    # inverse of the more common q * vector convention, so conjugate first.
    qx, qy, qz = -qx, -qy, -qz
    tx = 2.0 * (qy * z - qz * y)
    ty = 2.0 * (qz * x - qx * z)
    tz = 2.0 * (qx * y - qy * x)
    rx = x + qw * tx + (qy * tz - qz * ty)
    ry = y + qw * ty + (qz * tx - qx * tz)
    rz = z + qw * tz + (qx * ty - qy * tx)
    return rx, ry, rz


def interpolate_frame(frames: list[TramFrame], path_time_ms: int) -> tuple[float, float, float]:
    if not frames:
        return (0.0, 0.0, 0.0)
    if path_time_ms <= frames[0].time_ms:
        first = frames[0]
        return (first.x, first.y, first.z)

    prev = frames[0]
    for frame in frames[1:]:
        if frame.time_ms >= path_time_ms:
            span = max(1, frame.time_ms - prev.time_ms)
            t = (path_time_ms - prev.time_ms) / span
            return (
                prev.x + (frame.x - prev.x) * t,
                prev.y + (frame.y - prev.y) * t,
                prev.z + (frame.z - prev.z) * t,
            )
        prev = frame

    last = frames[-1]
    return (last.x, last.y, last.z)


def nearest_station(x: float, y: float) -> tuple[str, float]:
    best_name = "unknown"
    best_dist = float("inf")
    for name, sx, sy in TRACK_STATIONS:
        dist = math.hypot(x - sx, y - sy)
        if dist < best_dist:
            best_name = name
            best_dist = dist
    return best_name, best_dist


def _predict_trams_from_frames(
    frames_by_entry: dict[int, list[TramFrame]],
    now_ms: int | None = None,
    time_offset_ms: int = 0,
    leader: dict[str, Any] | None = None,
) -> list[PredictedTram]:
    clock_ms = int(time.time() * 1000) if now_ms is None else now_ms
    predictions: list[PredictedTram] = []
    for spawn in DEEPRUN_TBC_SPAWNS:
        frames = frames_by_entry.get(spawn.entry, [])
        if not frames:
            continue
        cycle_ms = max(frame.time_ms for frame in frames)
        path_time_ms = (clock_ms + time_offset_ms) % cycle_ms if cycle_ms > 0 else 0
        lx, ly, lz = interpolate_frame(frames, path_time_ms)
        rx, ry, rz = rotate_by_quaternion(
            lx, ly, lz,
            spawn.rotation0, spawn.rotation1, spawn.rotation2, spawn.rotation3,
        )
        # CMaNGOS ElevatorTransport applies this sign flip after local rotation.
        ry = -ry
        x = spawn.x + rx
        y = spawn.y + ry
        z = spawn.z + rz
        station_name, station_xy_distance = nearest_station(x, y)
        xy_distance_to_leader = None
        distance_to_leader = None
        if leader and int(leader.get("mapId", -1)) == spawn.map_id:
            pos = leader.get("position", {})
            dx = x - float(pos.get("x", 0.0))
            dy = y - float(pos.get("y", 0.0))
            dz = z - float(pos.get("z", 0.0))
            xy_distance_to_leader = math.hypot(dx, dy)
            distance_to_leader = math.sqrt(
                dx ** 2
                + dy ** 2
                + dz ** 2
            )
        predictions.append(PredictedTram(
            guid=spawn.guid,
            entry=spawn.entry,
            map_id=spawn.map_id,
            cycle_ms=cycle_ms,
            time_ms=path_time_ms,
            x=x,
            y=y,
            z=z,
            local_x=lx,
            local_y=ly,
            local_z=lz,
            nearest_station=station_name,
            station_xy_distance=station_xy_distance,
            at_station=station_xy_distance <= STATION_WINDOW_YARDS,
            boardable_at_station=station_xy_distance <= BOARDING_WINDOW_YARDS,
            xy_distance_to_leader=xy_distance_to_leader,
            distance_to_leader=distance_to_leader,
        ))

    predictions.sort(key=lambda pred: pred.xy_distance_to_leader if pred.xy_distance_to_leader is not None else 999999.0)
    return predictions


def predict_trams(
    dbc_path: Path,
    now_ms: int | None = None,
    time_offset_ms: int = 0,
    leader: dict[str, Any] | None = None,
) -> list[PredictedTram]:
    return _predict_trams_from_frames(
        load_transport_animation(dbc_path),
        now_ms=now_ms,
        time_offset_ms=time_offset_ms,
        leader=leader,
    )


def find_best_time_offset(
    dbc_path: Path,
    leader: dict[str, Any],
    now_ms: int | None = None,
    step_ms: int = 250,
) -> tuple[int, PredictedTram] | None:
    frames_by_entry = load_transport_animation(dbc_path)
    cycle_ms = max(
        (frame.time_ms for frames in frames_by_entry.values() for frame in frames),
        default=0,
    )
    if cycle_ms <= 0:
        return None
    clock_ms = int(time.time() * 1000) if now_ms is None else now_ms
    best: tuple[int, PredictedTram] | None = None
    best_dist = float("inf")
    step = max(25, step_ms)
    for offset in range(0, cycle_ms, step):
        predictions = _predict_trams_from_frames(
            frames_by_entry,
            now_ms=clock_ms,
            time_offset_ms=offset,
            leader=leader,
        )
        if not predictions or predictions[0].xy_distance_to_leader is None:
            continue
        dist = predictions[0].xy_distance_to_leader
        if dist < best_dist:
            best_dist = dist
            best = (offset, predictions[0])
    return best


def prediction_to_dict(prediction: PredictedTram) -> dict[str, Any]:
    return {
        "guid": prediction.guid,
        "entry": prediction.entry,
        "mapId": prediction.map_id,
        "cycleMs": prediction.cycle_ms,
        "timeMs": prediction.time_ms,
        "position": {"x": prediction.x, "y": prediction.y, "z": prediction.z},
        "localOffset": {"x": prediction.local_x, "y": prediction.local_y, "z": prediction.local_z},
        "nearestStation": prediction.nearest_station,
        "stationXyDistance": prediction.station_xy_distance,
        "atStation": prediction.at_station,
        "boardableAtStation": prediction.boardable_at_station,
        "xyDistanceToLeader": prediction.xy_distance_to_leader,
        "distanceToLeader": prediction.distance_to_leader,
    }
