#!/usr/bin/env python3
"""Supervise multiple wowee_headless leader clients."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
TERMINAL_MOVEMENT_STATES = {"arrived", "failed", "movement_locked"}
TRAVEL_NODES_PATH = ROOT / "tools" / "bot_fleet_manager" / "travel_nodes.json"
LANDMARKS: dict[str, dict[str, Any]] = {
    "goldshire": {
        "mapId": 0,
        "x": 63.52180099487305,
        "y": -9480.08984375,
        "z": 56.17570114135742,
        "description": "Goldshire, Elwynn Forest",
    },
    "stormwind": {
        "mapId": 0,
        "x": 364.0570068359375,
        "y": -9153.76953125,
        "z": 90.48290252685547,
        "description": "Stormwind City",
    },
    "ironforge": {
        "mapId": 0,
        "x": -834.0590209960938,
        "y": -5021.0,
        "z": 495.3190002441406,
        "description": "Ironforge",
    },
    "ironforge-probe": {
        "mapId": 0,
        "x": -834.0590209960938,
        "y": -5021.0,
        "z": 495.3190002441406,
        "description": "Ironforge guarded partial-route probe",
    },
    "kharanos": {
        "mapId": 0,
        "x": -482.12701416015625,
        "y": -5585.9501953125,
        "z": 397.0169982910156,
        "description": "Kharanos, Dun Morogh",
    },
    "crossroads": {
        "mapId": 1,
        "x": -2652.14990234375,
        "y": -455.8999938964844,
        "z": 95.58650207519531,
        "description": "The Crossroads, Barrens",
    },
    "ratchet": {
        "mapId": 1,
        "x": -3680.070068359375,
        "y": -951.364013671875,
        "z": 8.040180206298828,
        "description": "Ratchet, Barrens",
    },
    "exodar": {
        "mapId": 530,
        "x": -11873.599609375,
        "y": -4007.31005859375,
        "z": -0.5502870082855225,
        "description": "The Exodar",
    },
    "azure-watch": {
        "mapId": 530,
        "x": -12501.5,
        "y": -4179.47021484375,
        "z": 50.063499450683594,
        "description": "Azure Watch, Azuremyst Isle",
    },
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")


def utc_timestamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def resolve_path(raw: str) -> Path:
    path = Path(raw)
    return path if path.is_absolute() else ROOT / path


def request_json(method: str, url: str, payload: dict[str, Any] | None = None, timeout: float = 3.0) -> dict[str, Any]:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def point_distance(a: dict[str, Any], b: dict[str, Any]) -> float:
    dx = float(b["x"]) - float(a["x"])
    dy = float(b["y"]) - float(a["y"])
    dz = float(b["z"]) - float(a["z"])
    return (dx * dx + dy * dy + dz * dz) ** 0.5


def landmark_key(name: str) -> str:
    return name.strip().lower().replace(" ", "-").replace("_", "-")


def resolve_landmark(name: str) -> dict[str, Any]:
    key = landmark_key(name)
    if key not in LANDMARKS:
        known = ", ".join(sorted(LANDMARKS))
        raise ValueError(f"unknown landmark '{name}'. Known landmarks: {known}")
    return LANDMARKS[key]


def load_travel_registry(path: Path | None = None) -> dict[str, Any]:
    registry_path = path or TRAVEL_NODES_PATH
    doc = load_json(registry_path)
    nodes = doc.get("nodes", {})
    routes = doc.get("routes", {})
    if not isinstance(nodes, dict) or not nodes:
        raise ValueError(f"travel registry has no nodes: {registry_path}")
    if not isinstance(routes, dict):
        raise ValueError(f"travel registry routes must be an object: {registry_path}")
    return doc


def travel_node_key(name: str) -> str:
    return name.strip().lower().replace(" ", "-").replace("_", "-")


def resolve_travel_node(registry: dict[str, Any], name: str) -> tuple[str, dict[str, Any]]:
    key = travel_node_key(name)
    nodes = registry.get("nodes", {})
    if key not in nodes:
        known = ", ".join(sorted(nodes))
        raise ValueError(f"unknown travel node '{name}'. Known nodes: {known}")
    node = nodes[key]
    if not isinstance(node, dict):
        raise ValueError(f"travel node '{key}' must be an object")
    return key, node


def travel_node_position(node: dict[str, Any]) -> dict[str, Any] | None:
    required = ("mapId", "x", "y", "z")
    if not all(name in node for name in required):
        return None
    return {
        "mapId": int(node["mapId"]),
        "x": float(node["x"]),
        "y": float(node["y"]),
        "z": float(node["z"]),
    }


def travel_node_label(node_id: str, node: dict[str, Any]) -> str:
    name = str(node.get("name", node_id))
    node_type = str(node.get("type", "unknown"))
    return f"{node_id} ({name}, {node_type})"


def find_travel_link(from_node: dict[str, Any], to_node_id: str) -> dict[str, Any] | None:
    links = from_node.get("links", [])
    if not isinstance(links, list):
        return None
    for link in links:
        if isinstance(link, dict) and travel_node_key(str(link.get("to", ""))) == to_node_id:
            return link
    return None


def transition_step_name(mode: str) -> str:
    if mode == "enter_instance":
        return "enter_instance"
    if mode == "exit_instance":
        return "exit_instance"
    if mode == "ride_tram":
        return "board_transport + wait_transport + disembark"
    if mode == "ride_boat":
        return "board_transport + wait_transport + disembark"
    if mode == "use_portal":
        return "use_portal"
    if mode == "use_flight":
        return "use_flight"
    return mode or "transition"


def route_node_ids(registry: dict[str, Any], route_or_nodes: list[str]) -> tuple[str, list[str]]:
    routes = registry.get("routes", {})
    if len(route_or_nodes) == 1:
        route_key = travel_node_key(route_or_nodes[0])
        if route_key in routes:
            route = routes[route_key]
            nodes = route.get("nodes", []) if isinstance(route, dict) else []
            if not isinstance(nodes, list) or not nodes:
                raise ValueError(f"travel route '{route_key}' has no nodes")
            return route_key, [travel_node_key(str(node_id)) for node_id in nodes]
    return "custom", [travel_node_key(node_id) for node_id in route_or_nodes]


class FleetConfig:
    def __init__(self, path: Path):
        self.path = path
        self.doc = load_json(path)
        self.defaults = self.doc.get("defaults", {})
        self.leaders = self.doc.get("leaders", [])
        self.runtime_dir = resolve_path(self.doc.get("runtimeDir", "tools/bot_fleet_manager/runtime"))
        self.headless = resolve_path(self.doc.get("woweeHeadless", "build/bin/wowee_headless.exe"))
        self.launch_delay = float(self.doc.get("launchDelaySeconds", 2.0))
        self.supervision = self.doc.get("supervision", {})
        self.pathfinding = self.doc.get("pathfinding", {})
        self.integrity_dir = self.doc.get("integrityDir", "")
        if not self.leaders:
            raise ValueError("fleet config must contain at least one leader")

    @property
    def log_dir(self) -> Path:
        return self.runtime_dir / "logs"

    @property
    def route_state_dir(self) -> Path:
        return self.runtime_dir / "route_state"

    @property
    def initial_restart_backoff(self) -> float:
        return float(self.supervision.get("initialRestartBackoffSeconds", 5.0))

    @property
    def max_restart_backoff(self) -> float:
        return float(self.supervision.get("maxRestartBackoffSeconds", 60.0))

    @property
    def max_restarts(self) -> int:
        return int(self.supervision.get("maxRestarts", 0))

    @property
    def pathfinding_base_url(self) -> str:
        return str(self.pathfinding.get("baseUrl", "http://127.0.0.1:8790")).rstrip("/")

    @property
    def pathfinding_enabled(self) -> bool:
        return bool(self.pathfinding.get("enabled", False))

    @property
    def pathfinding_timeout(self) -> float:
        return float(self.pathfinding.get("timeoutSeconds", 45.0))

    def api_port_for(self, leader: dict[str, Any], index: int) -> int:
        api_defaults = self.defaults.get("api", {})
        return int(leader.get("apiPort", int(api_defaults.get("basePort", 8787)) + index))

    def api_base_for(self, leader: dict[str, Any], index: int) -> str:
        bind = self.defaults.get("api", {}).get("bind", "127.0.0.1")
        return f"http://{bind}:{self.api_port_for(leader, index)}"

    def leader_settings(self, leader: dict[str, Any], index: int) -> dict[str, Any]:
        api_defaults = self.defaults.get("api", {})
        automation_defaults = self.defaults.get("automation", {})
        startup_commands = list(automation_defaults.get("onEnterWorldCommands", []))
        startup_commands.extend(leader.get("startupCommands", []))

        return {
            "auth": {
                **self.defaults.get("auth", {}),
                "account": leader["account"],
                "password": leader["password"],
            },
            "client": self.defaults.get("client", {}),
            "realm": leader.get("realm", self.defaults.get("realm", {})),
            "character": {"name": leader["character"]},
            "chat": {"autoJoinDefaultChannels": bool(leader.get("autoJoinDefaultChannels", False))},
            "bots": {
                "enabled": bool(leader.get("partyBots")),
                "names": leader.get("partyBots", []),
            },
            "automation": {
                "commandDelaySeconds": float(leader.get(
                    "commandDelaySeconds",
                    automation_defaults.get("commandDelaySeconds", 0.25),
                )),
                "onEnterWorldCommands": startup_commands,
            },
            "api": {
                "enabled": True,
                "bind": api_defaults.get("bind", "127.0.0.1"),
                "port": self.api_port_for(leader, index),
                "maxMessages": int(api_defaults.get("maxMessages", 500)),
            },
        }

    def write_runtime_settings(self) -> list[tuple[dict[str, Any], Path]]:
        written: list[tuple[dict[str, Any], Path]] = []
        for index, leader in enumerate(self.leaders):
            leader_id = leader.get("id", f"leader-{index + 1}")
            path = self.runtime_dir / f"{leader_id}.settings.json"
            write_json(path, self.leader_settings(leader, index))
            written.append((leader, path))
        return written

    def selected_leaders(self, fleet: str = "", leader_ids: list[str] | None = None) -> list[tuple[int, dict[str, Any]]]:
        want_ids = set(leader_ids or [])
        selected: list[tuple[int, dict[str, Any]]] = []
        for index, leader in enumerate(self.leaders):
            leader_id = leader.get("id", f"leader-{index + 1}")
            if fleet and leader.get("fleet", "") != fleet:
                continue
            if want_ids and leader_id not in want_ids:
                continue
            selected.append((index, leader))
        return selected

    def route_state_path(self, leader_id: str) -> Path:
        return self.route_state_dir / f"{leader_id}.json"


def runtime_env(config: FleetConfig | None = None) -> dict[str, str]:
    env = os.environ.copy()
    path_entries: list[str] = []
    msys_ucrt = Path("C:/msys64/ucrt64/bin")
    if sys.platform == "win32" and msys_ucrt.exists():
        path_entries.append(str(msys_ucrt))
    if path_entries:
        env["PATH"] = os.pathsep.join(path_entries + [env.get("PATH", "")])
    if config and config.integrity_dir:
        env["WOWEE_INTEGRITY_DIR"] = str(resolve_path(config.integrity_dir))
    return env


def creation_flags() -> int:
    return subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0


def describe_exit_code(return_code: int) -> str:
    if return_code == 0:
        return "clean exit"
    if return_code == 139:
        return "SIGSEGV caught by crash handler"
    if return_code == 134:
        return "SIGABRT caught by crash handler"
    if sys.platform == "win32" and return_code < 0:
        return f"terminated by signal {-return_code}"
    if sys.platform == "win32":
        unsigned = return_code & 0xFFFFFFFF
        if unsigned == 0xC0000005:
            return "access violation (0xC0000005)"
        if unsigned == 0xC0000409:
            return "stack buffer overrun / fast fail (0xC0000409)"
        if unsigned == 0xC0000374:
            return "heap corruption (0xC0000374)"
        return f"Windows exception 0x{unsigned:08X}"
    if return_code < 0:
        return f"terminated by signal {-return_code}"
    return f"exit code {return_code}"


def log_timestamp() -> str:
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())


class ManagedLeader:
    def __init__(self, config: FleetConfig, leader: dict[str, Any], index: int, settings_path: Path, verbose: bool = False):
        self.config = config
        self.leader = leader
        self.index = index
        self.settings_path = settings_path
        self.leader_id = leader.get("id", f"leader-{index + 1}")
        self.process: subprocess.Popen[Any] | None = None
        self.log_handle: Any = None
        self.stream_thread: threading.Thread | None = None
        self.restart_count = 0
        self.next_start_at = 0.0
        self.backoff = config.initial_restart_backoff
        self.disabled = False
        self.verbose = verbose
        self.log_path = self.config.log_dir / f"{self.leader_id}.log"
        self.crash_log_path = self.config.log_dir / f"{self.leader_id}.crash.log"

    def stream_output(self) -> None:
        if not self.process or not self.process.stdout:
            return
        try:
            for line in self.process.stdout:
                stamped_line = f"[{log_timestamp()}] {line}"
                if self.log_handle:
                    self.log_handle.write(stamped_line)
                    self.log_handle.flush()
                if self.verbose:
                    print(f"[{self.leader_id}] {line}", end="", flush=True)
        except ValueError:
            return

    def start(self) -> None:
        self.config.log_dir.mkdir(parents=True, exist_ok=True)
        if self.log_handle:
            self.log_handle.close()
        self.log_handle = self.log_path.open("a", encoding="utf-8", errors="replace")
        run_stamp = log_timestamp()
        separator = f"\n[{run_stamp}] --- {self.leader_id} run start restart={self.restart_count} ---\n"
        self.log_handle.write(separator)
        self.log_handle.flush()
        child_env = runtime_env(self.config)
        child_env["WOWEE_CRASH_LOG"] = str(self.crash_log_path)
        print(f"Starting {self.leader_id} with {self.settings_path}; log={self.log_path}; crashLog={self.crash_log_path}")
        self.process = subprocess.Popen(
            [str(self.config.headless), str(self.settings_path)],
            cwd=str(ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            creationflags=creation_flags(),
            env=child_env,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.stream_thread = threading.Thread(target=self.stream_output, daemon=True)
        self.stream_thread.start()

    def poll(self) -> None:
        if self.disabled:
            return
        now = time.monotonic()
        if self.process is None:
            if now >= self.next_start_at:
                self.start()
            return

        rc = self.process.poll()
        if rc is None:
            return

        exit_description = describe_exit_code(rc)
        print(f"{self.leader_id}: exited with code {rc} ({exit_description})")
        self.process = None
        if self.stream_thread:
            self.stream_thread.join(timeout=2.0)
            self.stream_thread = None
        if self.log_handle:
            self.log_handle.write(f"[{log_timestamp()}] --- {self.leader_id} exited with code {rc} ({exit_description}) ---\n")
            self.log_handle.flush()
        if self.log_handle:
            self.log_handle.close()
            self.log_handle = None

        # Exit code 0 = clean logout/shutdown; don't restart
        if rc == 0:
            print(f"{self.leader_id}: clean exit; disabling auto-restart")
            self.disabled = True
            return

        self.restart_count += 1
        if self.config.max_restarts > 0 and self.restart_count > self.config.max_restarts:
            print(f"{self.leader_id}: restart limit reached; disabled")
            self.disabled = True
            return

        delay = self.backoff
        self.backoff = min(self.backoff * 2.0, self.config.max_restart_backoff)
        self.next_start_at = now + delay
        print(f"{self.leader_id}: restart {self.restart_count} scheduled in {delay:.1f}s")

    def stop(self) -> None:
        self.disabled = True
        if self.process and self.process.poll() is None:
            print(f"Stopping {self.leader_id}")
            self.process.terminate()
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                print(f"{self.leader_id}: terminate timed out; killing")
                self.process.kill()
        if self.stream_thread:
            self.stream_thread.join(timeout=2.0)
            self.stream_thread = None
        if self.log_handle:
            self.log_handle.close()
            self.log_handle = None


def cmd_start(config: FleetConfig) -> int:
    if not config.headless.exists():
        print(f"Missing wowee_headless executable: {config.headless}", file=sys.stderr)
        return 2

    for index, (leader, settings_path) in enumerate(config.write_runtime_settings()):
        leader_id = leader.get("id", f"leader-{index + 1}")
        print(f"Starting {leader_id} with {settings_path}")
        subprocess.Popen(
            [str(config.headless), str(settings_path)],
            cwd=str(ROOT),
            creationflags=creation_flags(),
            env=runtime_env(config),
        )
        time.sleep(config.launch_delay)
    return 0


def dashboard_targets(config: FleetConfig) -> list[dict[str, str]]:
    return [
        {
            "id": leader.get("id", f"leader-{index + 1}"),
            "fleet": leader.get("fleet", ""),
            "base": config.api_base_for(leader, index),
        }
        for index, leader in enumerate(config.leaders)
    ]


def start_dashboard_thread(config: FleetConfig, host: str, port: int) -> threading.Thread:
    try:
        from team_status_server import run_team_status_server
    except ImportError:
        from bot_fleet_manager.team_status_server import run_team_status_server

    thread = threading.Thread(
        target=run_team_status_server,
        args=(dashboard_targets(config), host, port),
        daemon=True,
    )
    thread.start()
    return thread


def cmd_supervise(
    config: FleetConfig,
    dashboard: bool = False,
    dashboard_host: str = "127.0.0.1",
    dashboard_port: int = 8780,
    verbose: bool = False,
) -> int:
    if not config.headless.exists():
        print(f"Missing wowee_headless executable: {config.headless}", file=sys.stderr)
        return 2

    managed = [
        ManagedLeader(config, leader, index, settings_path, verbose=verbose)
        for index, (leader, settings_path) in enumerate(config.write_runtime_settings())
    ]

    if dashboard:
        start_dashboard_thread(config, dashboard_host, dashboard_port)

    try:
        for leader in managed:
            leader.poll()
            time.sleep(config.launch_delay)

        print("Supervisor running. Press Ctrl-C to stop leader processes.")
        while True:
            for leader in managed:
                leader.poll()
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\nSupervisor stopping")
        for leader in managed:
            leader.stop()
    return 0


def cmd_status(config: FleetConfig, fleet: str = "", leader_ids: list[str] | None = None) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        url = config.api_base_for(leader, index) + "/status"
        try:
            status = request_json("GET", url)
            print(f"{leader_id}: {json.dumps(status, sort_keys=True)}")
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            print(f"{leader_id}: unavailable ({exc})")
    return 0


def post_all(
    config: FleetConfig,
    path: str,
    payload: dict[str, Any],
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    ok = True
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        url = config.api_base_for(leader, index) + path
        try:
            result = request_json("POST", url, payload)
            print(f"{leader_id}: {json.dumps(result, sort_keys=True)}")
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            ok = False
            print(f"{leader_id}: failed ({exc})")
    return 0 if ok else 1


def leader_position(config: FleetConfig, leader: dict[str, Any], index: int) -> dict[str, Any]:
    return request_json("GET", config.api_base_for(leader, index) + "/world/self")


def leader_status(config: FleetConfig, leader: dict[str, Any], index: int) -> dict[str, Any]:
    return request_json("GET", config.api_base_for(leader, index) + "/status")


def current_position_node(config: FleetConfig, leader: dict[str, Any], index: int) -> dict[str, Any]:
    self_info = leader_position(config, leader, index)
    current = self_info.get("position", {})
    return {
        "mapId": int(self_info.get("mapId", current.get("mapId", 0))),
        "x": float(current["x"]),
        "y": float(current["y"]),
        "z": float(current["z"]),
        "orientation": float(self_info.get("orientation", 0.0)),
        "movementFlags": int(self_info.get("movementFlags", 0) or 0),
        "movementFlags2": int(self_info.get("movementFlags2", 0) or 0),
        "status": str(self_info.get("status", "")),
        "character": self_info.get("character", {}),
    }


def default_survey_path(config: FleetConfig, label: str) -> Path:
    safe_label = travel_node_key(label or "survey")
    stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime())
    return config.runtime_dir / "travel_surveys" / f"{stamp}-{safe_label}.jsonl"


def survey_sample_summary(
    config: FleetConfig,
    leader: dict[str, Any],
    index: int,
    label: str,
    include_raw: bool,
) -> dict[str, Any]:
    leader_id = leader.get("id", f"leader-{index + 1}")
    self_info = leader_position(config, leader, index)
    status_info = leader_status(config, leader, index)
    position = self_info.get("position", {})
    sample = {
        "sampledAt": utc_timestamp(),
        "label": label,
        "leaderId": leader_id,
        "character": self_info.get("character", {}),
        "status": self_info.get("status", ""),
        "inWorld": bool(self_info.get("inWorld", False)),
        "mapId": int(self_info.get("mapId", 0) or 0),
        "position": {
            "x": float(position.get("x", 0.0)),
            "y": float(position.get("y", 0.0)),
            "z": float(position.get("z", 0.0)),
        },
        "orientation": float(self_info.get("orientation", 0.0) or 0.0),
        "movementFlags": int(self_info.get("movementFlags", 0) or 0),
        "movementFlags2": int(self_info.get("movementFlags2", 0) or 0),
        "runSpeed": float(self_info.get("runSpeed", 0.0) or 0.0),
        "combat": self_info.get("combat", {}),
        "health": self_info.get("health", {}),
        "movement": status_info.get("movement", self_info.get("movement", {})),
    }
    if include_raw:
        sample["rawWorldSelf"] = self_info
        sample["rawStatus"] = status_info
    return sample


def sample_changed(previous: dict[str, Any] | None, current: dict[str, Any], min_distance: float) -> bool:
    if previous is None:
        return True
    if int(previous.get("mapId", -1)) != int(current.get("mapId", -2)):
        return True
    previous_pos = previous.get("position", {})
    current_pos = current.get("position", {})
    distance = point_distance(previous_pos, current_pos)
    if distance >= min_distance:
        return True
    keys = ("status", "movementFlags", "movementFlags2")
    if any(previous.get(key) != current.get(key) for key in keys):
        return True
    previous_movement = previous.get("movement", {})
    current_movement = current.get("movement", {})
    return previous_movement.get("state") != current_movement.get("state")


def save_route_state(
    config: FleetConfig,
    leader_id: str,
    state: str,
    target: dict[str, Any],
    detail: dict[str, Any] | None = None,
) -> None:
    payload = {
        "leaderId": leader_id,
        "state": state,
        "updatedAt": utc_timestamp(),
        "target": target,
    }
    if detail:
        payload.update(detail)
    write_json(config.route_state_path(leader_id), payload)


def load_route_state(config: FleetConfig, leader_id: str) -> dict[str, Any]:
    path = config.route_state_path(leader_id)
    if not path.exists():
        raise FileNotFoundError(f"no route state for {leader_id}: {path}")
    return load_json(path)


def cmd_pathfind_goto(
    config: FleetConfig,
    map_id: int,
    x: float,
    y: float,
    z: float,
    arrival_radius: float,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1
    if not config.pathfinding_enabled:
        print("Warning: pathfinding.enabled is false; using configured service anyway")

    ok = True
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        try:
            self_info = leader_position(config, leader, index)
            position = self_info.get("position", {})
            current_map = int(self_info.get("mapId", map_id))
            if current_map != map_id:
                raise ValueError(f"leader is on map {current_map}, destination is on map {map_id}")

            path_request = {
                "mapId": map_id,
                "start": {
                    "x": float(position["x"]),
                    "y": float(position["y"]),
                    "z": float(position["z"]),
                },
                "end": {"x": x, "y": y, "z": z},
                "coordinateSpace": "wowee-canonical",
                "outputSpace": "wowee-canonical",
            }
            path_result = request_json(
                "POST",
                config.pathfinding_base_url + "/path",
                path_request,
                timeout=config.pathfinding_timeout,
            )
            if not path_result.get("ok", False):
                raise RuntimeError(path_result.get("error", "pathfinding failed"))
            waypoints = path_result.get("waypoints", [])
            if not isinstance(waypoints, list) or not waypoints:
                raise RuntimeError("pathfinding returned no waypoints")

            movement = request_json("POST", config.api_base_for(leader, index) + "/movement/goto/waypoints", {
                "mapId": map_id,
                "waypoints": waypoints,
                "arrivalRadius": arrival_radius,
            })
            backend = path_result.get("backend", "unknown")
            path_type = path_result.get("pathType", "unknown")
            print(f"{leader_id}: path {backend}/{path_type} waypoints={len(waypoints)} movement={json.dumps(movement, sort_keys=True)}")
            for warning in path_result.get("warnings", []):
                print(f"{leader_id}: path warning: {warning}")
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError, ValueError, RuntimeError) as exc:
            ok = False
            print(f"{leader_id}: pathfind-goto failed ({exc})")
    return 0 if ok else 1


def cmd_plan_route(
    config: FleetConfig,
    map_id: int,
    x: float,
    y: float,
    z: float,
    arrival_radius: float,
    max_legs: int,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1
    if not config.pathfinding_enabled:
        print("Warning: pathfinding.enabled is false; using configured service anyway")

    ok = True
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        try:
            self_info = leader_position(config, leader, index)
            position = self_info.get("position", {})
            current_map = int(self_info.get("mapId", map_id))
            if current_map != map_id:
                raise ValueError(f"leader is on map {current_map}, destination is on map {map_id}")

            route_request = {
                "mapId": map_id,
                "start": {
                    "x": float(position["x"]),
                    "y": float(position["y"]),
                    "z": float(position["z"]),
                },
                "end": {"x": x, "y": y, "z": z},
                "coordinateSpace": "wowee-canonical",
                "outputSpace": "wowee-canonical",
                "arrivalRadius": arrival_radius,
                "maxLegs": max_legs,
            }
            route_result = request_json(
                "POST",
                config.pathfinding_base_url + "/route",
                route_request,
                timeout=config.pathfinding_timeout,
            )
            if not route_result.get("ok", False):
                raise RuntimeError(route_result.get("error", "route planning failed"))

            status = route_result.get("routeStatus", "unknown")
            legs = route_result.get("legs", [])
            waypoints = route_result.get("waypoints", [])
            print(f"{leader_id}: route status={status} legs={len(legs)} waypoints={len(waypoints)}")
            for leg in legs:
                before = float(leg.get("distanceToGoalBefore", 0.0))
                after = float(leg.get("distanceToGoalAfter", 0.0))
                progress = float(leg.get("progressYards", 0.0))
                print(
                    f"  leg {leg.get('index')}: {leg.get('backend')}/{leg.get('pathType')} "
                    f"waypoints={leg.get('waypointCount')} progress={progress:.1f}y "
                    f"remaining {before:.1f}y -> {after:.1f}y"
                )
            for warning in route_result.get("warnings", []):
                print(f"{leader_id}: route warning: {warning}")
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError, ValueError, RuntimeError) as exc:
            ok = False
            print(f"{leader_id}: plan-route failed ({exc})")
    return 0 if ok else 1


def wait_for_movement(
    api_base: str,
    timeout_seconds: float,
    poll_interval: float,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_seconds
    last_state = ""
    last_waypoint = 0
    transient_errors = 0
    while time.monotonic() < deadline:
        try:
            status = request_json("GET", api_base + "/status", timeout=max(2.0, poll_interval + 1.0))
            transient_errors = 0
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            transient_errors += 1
            if transient_errors == 1 or transient_errors % 5 == 0:
                print(f"    transient status poll failure #{transient_errors}: {exc}")
            time.sleep(poll_interval)
            continue
        movement = status.get("movement", {})
        state = str(movement.get("state", "idle"))
        waypoint = int(movement.get("waypointIndex", 0) or 0)
        waypoint_count = int(movement.get("waypointCount", 0) or 0)
        if state != last_state or waypoint != last_waypoint:
            print(f"    movement state={state} waypoint={waypoint}/{waypoint_count}")
            last_state = state
            last_waypoint = waypoint
        if state in TERMINAL_MOVEMENT_STATES or not bool(movement.get("active", False)):
            return status
        time.sleep(poll_interval)
    request_json("POST", api_base + "/movement/stop", {"reason": "route-goto leg timeout"})
    raise TimeoutError(f"movement did not finish within {timeout_seconds:.1f}s")


def post_waypoint_movement(
    api_base: str,
    map_id: int,
    waypoints: list[Any],
    arrival_radius: float,
) -> dict[str, Any]:
    payload = {
        "mapId": map_id,
        "waypoints": waypoints,
        "arrivalRadius": arrival_radius,
    }
    last_error: Exception | None = None
    for attempt in range(1, 4):
        try:
            return request_json("POST", api_base + "/movement/goto/waypoints", payload)
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last_error = exc
            print(f"    movement POST transient failure #{attempt}: {exc}")
            time.sleep(0.5)
            try:
                status = request_json("GET", api_base + "/status", timeout=3.0)
                movement = status.get("movement", {})
                if movement.get("active") and movement.get("state") == "moving":
                    if int(movement.get("waypointCount", 0) or 0) == len(waypoints):
                        print("    movement command appears active after dropped POST response")
                        return {"ok": True, "movement": movement, "recoveredAfterPostError": True}
            except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError):
                pass
    raise RuntimeError(f"movement command failed after retries: {last_error}")


def cmd_route_goto(
    config: FleetConfig,
    map_id: int,
    x: float,
    y: float,
    z: float,
    arrival_radius: float,
    max_legs: int,
    leg_timeout: float,
    poll_interval: float,
    min_progress_yards: float,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1
    if not config.pathfinding_enabled:
        print("Warning: pathfinding.enabled is false; using configured service anyway")

    destination = {"x": x, "y": y, "z": z}
    ok = True
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        api_base = config.api_base_for(leader, index)
        print(f"{leader_id}: guarded route-goto map={map_id} destination={destination}")
        save_route_state(config, leader_id, "running", {
            "mapId": map_id,
            **destination,
            "arrivalRadius": arrival_radius,
        }, {"startedAt": utc_timestamp(), "lastLeg": 0})
        try:
            for leg_number in range(1, max_legs + 1):
                self_info = leader_position(config, leader, index)
                position = self_info.get("position", {})
                current_map = int(self_info.get("mapId", map_id))
                if current_map != map_id:
                    raise ValueError(f"leader is on map {current_map}, destination is on map {map_id}")

                remaining = point_distance(position, destination)
                if remaining <= arrival_radius:
                    print(f"{leader_id}: arrived within {remaining:.1f} yards")
                    break

                route_request = {
                    "mapId": map_id,
                    "start": {
                        "x": float(position["x"]),
                        "y": float(position["y"]),
                        "z": float(position["z"]),
                    },
                    "end": destination,
                    "coordinateSpace": "wowee-canonical",
                    "outputSpace": "wowee-canonical",
                    "arrivalRadius": arrival_radius,
                    "maxLegs": 1,
                    "minProgressYards": min_progress_yards,
                }
                route_result = request_json(
                    "POST",
                    config.pathfinding_base_url + "/route",
                    route_request,
                    timeout=config.pathfinding_timeout,
                )
                if not route_result.get("ok", False):
                    raise RuntimeError(route_result.get("error", "route planning failed"))
                legs = route_result.get("legs", [])
                waypoints = route_result.get("waypoints", [])
                route_status = route_result.get("routeStatus", "unknown")
                if not isinstance(legs, list) or not legs:
                    if route_status == "complete":
                        print(f"{leader_id}: planner reports complete")
                        break
                    raise RuntimeError(f"planner returned no executable legs; status={route_status}")
                if not isinstance(waypoints, list) or not waypoints:
                    raise RuntimeError(f"planner returned no executable waypoints; status={route_status}")

                leg = legs[0]
                progress = float(leg.get("progressYards", 0.0))
                after = float(leg.get("distanceToGoalAfter", remaining))
                print(
                    f"{leader_id}: leg {leg_number}/{max_legs} {leg.get('backend')}/{leg.get('pathType')} "
                    f"waypoints={len(waypoints)} progress={progress:.1f}y remaining={after:.1f}y "
                    f"plannerStatus={route_status}"
                )
                save_route_state(config, leader_id, "running", {
                    "mapId": map_id,
                    **destination,
                    "arrivalRadius": arrival_radius,
                }, {
                    "lastLeg": leg_number,
                    "lastPlannerStatus": route_status,
                    "remainingYards": after,
                    "progressYards": progress,
                    "waypointCount": len(waypoints),
                })
                if progress < min_progress_yards and after > arrival_radius:
                    raise RuntimeError(
                        f"planner stalled: progress {progress:.1f}y below {min_progress_yards:.1f}y"
                    )

                movement = post_waypoint_movement(api_base, map_id, waypoints, arrival_radius)
                if not movement.get("ok", False):
                    raise RuntimeError(movement.get("error", "movement command failed"))

                final_status = wait_for_movement(api_base, leg_timeout, poll_interval)
                movement_status = final_status.get("movement", {})
                state = movement_status.get("state", "unknown")
                if state != "arrived":
                    raise RuntimeError(f"movement ended as {state}: {movement_status.get('error', '')}")
            else:
                raise RuntimeError(f"route-goto used all {max_legs} legs without arriving")
            save_route_state(config, leader_id, "complete", {
                "mapId": map_id,
                **destination,
                "arrivalRadius": arrival_radius,
            }, {"completedAt": utc_timestamp()})
        except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError, KeyError, ValueError, RuntimeError) as exc:
            ok = False
            print(f"{leader_id}: route-goto failed ({exc})")
            save_route_state(config, leader_id, "failed", {
                "mapId": map_id,
                **destination,
                "arrivalRadius": arrival_radius,
            }, {"failedAt": utc_timestamp(), "error": str(exc)})
            try:
                request_json("POST", api_base + "/movement/stop", {"reason": "route-goto failed"})
            except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError):
                pass
    return 0 if ok else 1


def cmd_landmarks() -> int:
    for name in sorted(LANDMARKS):
        target = LANDMARKS[name]
        print(
            f"{name}: map={target['mapId']} "
            f"x={target['x']} y={target['y']} z={target['z']} - {target['description']}"
        )
    return 0


def cmd_travel_nodes(registry_path: Path | None, show_routes: bool) -> int:
    registry = load_travel_registry(registry_path)
    nodes = registry.get("nodes", {})
    for node_id in sorted(nodes):
        node = nodes[node_id]
        if not isinstance(node, dict):
            continue
        pos = travel_node_position(node)
        flags: list[str] = []
        if node.get("verified"):
            flags.append("verified")
        if node.get("requiresSurvey"):
            flags.append("survey")
        suffix = f" [{' '.join(flags)}]" if flags else ""
        if pos:
            print(
                f"{node_id}: type={node.get('type', 'unknown')} map={pos['mapId']} "
                f"x={pos['x']} y={pos['y']} z={pos['z']} - {node.get('description', node.get('name', ''))}{suffix}"
            )
        else:
            print(
                f"{node_id}: type={node.get('type', 'unknown')} "
                f"- {node.get('description', node.get('name', ''))}{suffix}"
            )

    if show_routes:
        routes = registry.get("routes", {})
        print("\nRoutes:")
        for route_id in sorted(routes):
            route = routes[route_id]
            if not isinstance(route, dict):
                continue
            node_count = len(route.get("nodes", [])) if isinstance(route.get("nodes", []), list) else 0
            print(f"{route_id}: nodes={node_count} - {route.get('description', route.get('name', ''))}")
    return 0


def print_walk_plan(
    config: FleetConfig,
    leader_id: str,
    step_index: int,
    from_label: str,
    from_pos: dict[str, Any],
    to_label: str,
    to_pos: dict[str, Any],
    arrival_radius: float,
    max_legs: int,
) -> bool:
    if int(from_pos["mapId"]) != int(to_pos["mapId"]):
        print(
            f"  {step_index}. map_transition_needed {from_label} -> {to_label}: "
            f"map {from_pos['mapId']} -> {to_pos['mapId']} has no registered transport link"
        )
        return True

    route_request = {
        "mapId": int(to_pos["mapId"]),
        "start": {"x": from_pos["x"], "y": from_pos["y"], "z": from_pos["z"]},
        "end": {"x": to_pos["x"], "y": to_pos["y"], "z": to_pos["z"]},
        "coordinateSpace": "wowee-canonical",
        "outputSpace": "wowee-canonical",
        "arrivalRadius": arrival_radius,
        "maxLegs": max_legs,
    }
    try:
        route_result = request_json(
            "POST",
            config.pathfinding_base_url + "/route",
            route_request,
            timeout=config.pathfinding_timeout,
        )
        if not route_result.get("ok", False):
            raise RuntimeError(route_result.get("error", "route planning failed"))
    except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError, RuntimeError) as exc:
        print(f"  {step_index}. walk {from_label} -> {to_label}: path query failed for {leader_id}: {exc}")
        return False

    status = route_result.get("routeStatus", "unknown")
    legs = route_result.get("legs", [])
    waypoints = route_result.get("waypoints", [])
    print(
        f"  {step_index}. walk {from_label} -> {to_label}: "
        f"status={status} legs={len(legs)} waypoints={len(waypoints)}"
    )
    for leg in legs:
        before = float(leg.get("distanceToGoalBefore", 0.0))
        after = float(leg.get("distanceToGoalAfter", 0.0))
        progress = float(leg.get("progressYards", 0.0))
        print(
            f"     leg {leg.get('index')}: {leg.get('backend')}/{leg.get('pathType')} "
            f"waypoints={leg.get('waypointCount')} progress={progress:.1f}y "
            f"remaining {before:.1f}y -> {after:.1f}y"
        )
    for warning in route_result.get("warnings", []):
        print(f"     warning: {warning}")
    return True


def cmd_travel_plan(
    config: FleetConfig,
    registry_path: Path | None,
    route_or_nodes: list[str],
    static: bool,
    arrival_radius: float,
    max_legs: int,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    registry = load_travel_registry(registry_path)
    plan_name, node_ids = route_node_ids(registry, route_or_nodes)
    nodes_by_id: list[tuple[str, dict[str, Any]]] = [
        resolve_travel_node(registry, node_id) for node_id in node_ids
    ]
    routes = registry.get("routes", {})
    route = routes.get(plan_name, {}) if isinstance(routes, dict) else {}
    if isinstance(route, dict) and plan_name != "custom":
        print(f"travel-plan {plan_name}: {route.get('description', route.get('name', ''))}")
    else:
        print(f"travel-plan custom: {' -> '.join(node_id for node_id, _node in nodes_by_id)}")

    if static:
        selected: list[tuple[int, dict[str, Any] | None]] = [(-1, None)]
    else:
        selected = [(index, leader) for index, leader in config.selected_leaders(fleet, leader_ids)]
        if not selected:
            print("No matching leaders")
            return 1
        if not config.pathfinding_enabled:
            print("Warning: pathfinding.enabled is false; using configured service anyway")

    ok = True
    for index, leader in selected:
        if leader is None:
            leader_id = "static"
            pairs: list[tuple[str, dict[str, Any], str, dict[str, Any]]] = []
        else:
            leader_id = leader.get("id", f"leader-{index + 1}")
            try:
                self_info = leader_position(config, leader, index)
                current = self_info.get("position", {})
                current_node = {
                    "type": "current_position",
                    "mapId": int(self_info.get("mapId", current.get("mapId", 0))),
                    "x": float(current["x"]),
                    "y": float(current["y"]),
                    "z": float(current["z"]),
                    "name": "Current leader position",
                }
            except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError, KeyError, ValueError) as exc:
                ok = False
                print(f"{leader_id}: travel-plan failed to read leader position ({exc})")
                continue
            pairs = [("__current__", current_node, nodes_by_id[0][0], nodes_by_id[0][1])]

        pairs.extend(
            (from_id, from_node, to_id, to_node)
            for (from_id, from_node), (to_id, to_node) in zip(nodes_by_id, nodes_by_id[1:])
        )

        print(f"{leader_id}: {len(pairs)} step(s)")
        for step_index, (from_id, from_node, to_id, to_node) in enumerate(pairs, start=1):
            from_label = travel_node_label(from_id, from_node)
            to_label = travel_node_label(to_id, to_node)
            link = None if from_id == "__current__" else find_travel_link(from_node, to_id)
            if link:
                mode = str(link.get("mode", "transition"))
                transport = f" transport={link['transportMap']}" if "transportMap" in link else ""
                print(
                    f"  {step_index}. {transition_step_name(mode)} {from_label} -> {to_label}: "
                    f"{link.get('description', '')}{transport}"
                )
                if to_node.get("requiresSurvey") or from_node.get("requiresSurvey"):
                    print("     survey required before this transition can be executed")
                continue

            from_pos = travel_node_position(from_node)
            to_pos = travel_node_position(to_node)
            if not from_pos or not to_pos:
                missing: list[str] = []
                if not from_pos:
                    missing.append(from_id)
                if not to_pos:
                    missing.append(to_id)
                print(
                    f"  {step_index}. survey_needed {from_label} -> {to_label}: "
                    f"missing coordinates for {', '.join(missing)}"
                )
                continue

            if not print_walk_plan(
                config,
                leader_id,
                step_index,
                from_label,
                from_pos,
                to_label,
                to_pos,
                arrival_radius,
                max_legs,
            ):
                ok = False
    return 0 if ok else 1


def cmd_travel_survey(
    config: FleetConfig,
    label: str,
    duration: float,
    interval: float,
    min_distance: float,
    output: Path | None,
    include_raw: bool,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1

    output_path = resolve_path(str(output)) if output else default_survey_path(config, label)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + duration if duration > 0 else None
    last_samples: dict[str, dict[str, Any]] = {}
    written = 0
    print(f"Writing travel survey samples to {output_path}")
    print("Press Ctrl-C to stop." if deadline is None else f"Sampling for {duration:.1f}s.")
    try:
        with output_path.open("a", encoding="utf-8") as handle:
            while deadline is None or time.monotonic() < deadline:
                for index, leader in selected:
                    leader_id = leader.get("id", f"leader-{index + 1}")
                    try:
                        sample = survey_sample_summary(config, leader, index, label, include_raw)
                    except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError, KeyError, ValueError) as exc:
                        print(f"{leader_id}: survey sample failed ({exc})")
                        continue
                    if not sample_changed(last_samples.get(leader_id), sample, min_distance):
                        continue
                    last_samples[leader_id] = sample
                    handle.write(json.dumps(sample, sort_keys=True) + "\n")
                    handle.flush()
                    written += 1
                    pos = sample["position"]
                    movement = sample.get("movement", {})
                    print(
                        f"{leader_id}: map={sample['mapId']} "
                        f"x={pos['x']:.3f} y={pos['y']:.3f} z={pos['z']:.3f} "
                        f"o={sample['orientation']:.3f} flags=0x{sample['movementFlags']:x} "
                        f"move={movement.get('state', '')}"
                    )
                time.sleep(max(0.1, interval))
    except KeyboardInterrupt:
        print("Survey stopped.")
    print(f"Wrote {written} sample(s)")
    return 0


def cmd_travel_capture(
    config: FleetConfig,
    registry_path: Path | None,
    node_id: str,
    node_type: str,
    name: str,
    description: str,
    radius: float,
    verified: bool,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1
    if len(selected) != 1:
        print("travel-capture requires exactly one selected leader")
        return 2

    registry_path_resolved = resolve_path(str(registry_path)) if registry_path else TRAVEL_NODES_PATH
    registry = load_travel_registry(registry_path_resolved)
    nodes = registry.setdefault("nodes", {})
    if not isinstance(nodes, dict):
        print("travel registry nodes must be an object")
        return 2

    index, leader = selected[0]
    leader_id = leader.get("id", f"leader-{index + 1}")
    key = travel_node_key(node_id)
    try:
        current = current_position_node(config, leader, index)
    except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError, KeyError, ValueError) as exc:
        print(f"{leader_id}: travel-capture failed ({exc})")
        return 1

    existing = nodes.get(key, {})
    if not isinstance(existing, dict):
        existing = {}
    character = current.get("character", {})
    updated = {
        **existing,
        "type": node_type,
        "mapId": current["mapId"],
        "x": current["x"],
        "y": current["y"],
        "z": current["z"],
        "radius": radius,
        "name": name or existing.get("name", key),
        "description": description or existing.get("description", f"Captured from {leader_id}"),
        "verified": verified,
        "requiresSurvey": False,
        "capturedAt": utc_timestamp(),
        "source": f"travel-capture:{leader_id}",
        "capture": {
            "orientation": current["orientation"],
            "movementFlags": current["movementFlags"],
            "movementFlags2": current["movementFlags2"],
            "character": character,
        },
    }
    if not verified:
        updated["requiresSurvey"] = True
    nodes[key] = updated
    write_json(registry_path_resolved, registry)
    print(
        f"Captured {key}: map={updated['mapId']} "
        f"x={updated['x']} y={updated['y']} z={updated['z']} "
        f"verified={updated['verified']} registry={registry_path_resolved}"
    )
    return 0


def cmd_route_demo(
    config: FleetConfig,
    name: str,
    plan_only: bool,
    arrival_radius: float,
    max_legs: int,
    leg_timeout: float,
    poll_interval: float,
    min_progress_yards: float,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    target = resolve_landmark(name)
    map_id = int(target["mapId"])
    x = float(target["x"])
    y = float(target["y"])
    z = float(target["z"])
    print(f"route-demo {landmark_key(name)}: {target['description']} map={map_id} x={x} y={y} z={z}")
    if plan_only:
        return cmd_plan_route(
            config,
            map_id,
            x,
            y,
            z,
            arrival_radius,
            max_legs,
            fleet,
            leader_ids,
        )
    return cmd_route_goto(
        config,
        map_id,
        x,
        y,
        z,
        arrival_radius,
        max_legs,
        leg_timeout,
        poll_interval,
        min_progress_yards,
        fleet,
        leader_ids,
    )


def _transition_step_label(mode: str, to_label: str) -> str:
    return f"{transition_step_name(mode)} -> {to_label}"


def cmd_travel_node_execute(
    config: FleetConfig,
    registry_path: Path | None,
    route_or_nodes: list[str],
    arrival_radius: float,
    max_legs: int,
    leg_timeout: float,
    poll_interval: float,
    min_progress_yards: float,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    registry = load_travel_registry(registry_path)
    plan_name, node_ids = route_node_ids(registry, route_or_nodes)
    nodes_by_id: list[tuple[str, dict[str, Any]]] = [
        resolve_travel_node(registry, node_id) for node_id in node_ids
    ]

    selected = [(index, leader) for index, leader in config.selected_leaders(fleet, leader_ids)]
    if not selected:
        print("No matching leaders")
        return 1

    ok = True
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        api_base = config.api_base_for(leader, index)

        try:
            self_info = leader_position(config, leader, index)
        except Exception as exc:
            print(f"{leader_id}: travel-node-execute failed to read leader position ({exc})")
            ok = False
            continue

        current = self_info.get("position", {})
        pairs: list[tuple[str, dict[str, Any], str, dict[str, Any]]] = [
            ("__current__", {
                "type": "current_position",
                "mapId": int(self_info.get("mapId", current.get("mapId", 0))),
                "x": float(current["x"]),
                "y": float(current["y"]),
                "z": float(current["z"]),
                "name": "Current leader position",
            }, node_ids[0], nodes_by_id[0][1])
        ]
        pairs.extend(
            (from_id, from_node, to_id, to_node)
            for (from_id, from_node), (to_id, to_node) in zip(nodes_by_id, nodes_by_id[1:])
        )

        plan_state = {
            "planName": plan_name,
            "nodeIds": node_ids,
            "stepCount": len(pairs),
        }

        complete = True
        for step_index, (from_id, from_node, to_id, to_node) in enumerate(pairs, start=1):
            to_label = travel_node_label(to_id, to_node)
            link = None if from_id == "__current__" else find_travel_link(from_node, to_id)

            detail = {
                "planName": plan_name,
                "nodeIds": node_ids,
                "stepIndex": step_index,
                "stepCount": len(pairs),
                "fromNodeId": from_id,
                "toNodeId": to_id,
            }

            if link:
                mode = str(link.get("mode", "transition"))
                step_name = _transition_step_label(mode, to_label)
                print(f"  {step_index}. {step_name}")

                if mode == "enter_instance" or mode == "exit_instance" or mode == "use_portal":
                    area_trigger_id = link.get("areaTriggerId") or (
                        from_node.get("areaTrigger", {}).get("id") if mode == "enter_instance"
                        else to_node.get("areaTrigger", {}).get("exitId") if mode == "exit_instance"
                        else None
                    )
                    if not area_trigger_id:
                        area_trigger_id = (from_node.get("areaTrigger", {}) if mode == "enter_instance" else to_node.get("areaTrigger", {})).get("id")

                    if not area_trigger_id:
                        # fallback: use from_node areaTrigger.id or to_node areaTrigger entryId/exitId
                        at = from_node.get("areaTrigger", {}) or to_node.get("areaTrigger", {})
                        area_trigger_id = at.get("id") or at.get("entryId") or at.get("exitId")

                    if not area_trigger_id:
                        print(f"    no area trigger id for {step_name}; skipping")
                        continue

                    print(f"    sending area trigger {area_trigger_id}")
                    try:
                        request_json("POST", api_base + "/area-trigger", {"id": int(area_trigger_id)})
                    except Exception as exc:
                        print(f"    area trigger failed: {exc}")
                        ok = False
                        complete = False
                        save_route_state(config, leader_id, "failed", {}, {
                            **detail,
                            "failedAt": utc_timestamp(),
                            "error": f"area-trigger {area_trigger_id} failed: {exc}",
                        })
                        break

                    target_pos = travel_node_position(to_node)
                    if target_pos and int(target_pos["mapId"]) == int(from_node.get("mapId", 0)):
                        print(f"    waiting for position arrival at {to_label}")
                        try:
                            _wait_node_arrival(config, leader, index, target_pos, arrival_radius, leg_timeout, poll_interval)
                        except TimeoutError as exc:
                            print(f"    {exc}")
                    else:
                        print(f"    waiting for map change to {target_pos.get('mapId', '?')} (up to {leg_timeout}s)")
                        try:
                            _wait_map_change(config, leader, index, target_pos.get("mapId"), leg_timeout, poll_interval)
                        except TimeoutError as exc:
                            print(f"    {exc}")

                    save_route_state(config, leader_id, "running", {}, {
                        **detail,
                        "completedNodeId": to_id,
                        "completedAt": utc_timestamp(),
                    })
                    continue

                if mode == "ride_tram" or mode == "ride_boat":
                    print(f"    waiting for transit (map change to target map, timeout {leg_timeout}s)")
                    target_pos = travel_node_position(to_node)
                    target_map = int(target_pos.get("mapId", 369)) if target_pos else 369
                    try:
                        request_json("POST", api_base + "/area-trigger", {"id": int(to_node.get("areaTrigger", {}).get("entryId", 0))})
                    except Exception:
                        pass
                    try:
                        _wait_map_change(config, leader, index, target_map, leg_timeout, poll_interval)
                    except TimeoutError as exc:
                        print(f"    transit timeout: {exc}")
                        ok = False
                        complete = False
                        save_route_state(config, leader_id, "failed", {}, {
                            **detail,
                            "failedAt": utc_timestamp(),
                            "error": str(exc),
                        })
                        break

                    save_route_state(config, leader_id, "running", {}, {
                        **detail,
                        "completedAt": utc_timestamp(),
                    })
                    continue

                print(f"    unknown link mode {mode}; skipping")
                continue

            from_pos = travel_node_position(from_node)
            to_pos = travel_node_position(to_node)
            if not from_pos or not to_pos:
                print(f"  {step_index}. survey_needed -> {to_label}: missing coordinates")
                continue

            print(f"  {step_index}. walk -> {to_label}")
            step_result = cmd_route_goto(
                config,
                int(to_pos["mapId"]),
                float(to_pos["x"]),
                float(to_pos["y"]),
                float(to_pos["z"]),
                arrival_radius,
                max_legs,
                leg_timeout,
                poll_interval,
                min_progress_yards,
                leader_ids=[leader_id],
            )
            if step_result != 0:
                ok = False
                complete = False
                save_route_state(config, leader_id, "failed", {}, {
                    **detail,
                    "failedAt": utc_timestamp(),
                    "error": "walk leg failed",
                })
                break

            save_route_state(config, leader_id, "running", {}, {
                **detail,
                "completedAt": utc_timestamp(),
            })

        if complete:
            save_route_state(config, leader_id, "complete", {}, {
                "planName": plan_name,
                "completedAt": utc_timestamp(),
            })
            print(f"{leader_id}: travel plan {plan_name} complete")

    return 0 if ok else 1


def _wait_node_arrival(
    config: FleetConfig,
    leader: dict[str, Any],
    index: int,
    target_pos: dict[str, Any],
    arrival_radius: float,
    timeout_seconds: float,
    poll_interval: float,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            self_info = leader_position(config, leader, index)
        except Exception:
            time.sleep(poll_interval)
            continue
        position = self_info.get("position", {})
        remaining = point_distance(position, target_pos)
        print(f"    distance to {target_pos.get('name', 'target')}: {remaining:.1f}y")
        if remaining <= arrival_radius:
            return
        time.sleep(poll_interval)
    raise TimeoutError(f"did not arrive at target within {timeout_seconds:.1f}s")


def _wait_map_change(
    config: FleetConfig,
    leader: dict[str, Any],
    index: int,
    expected_map_id: str | int | None,
    timeout_seconds: float,
    poll_interval: float,
) -> int:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            self_info = leader_position(config, leader, index)
        except Exception:
            time.sleep(poll_interval)
            continue
        current_map = int(self_info.get("mapId", 0))
        print(f"    current map: {current_map}")
        if expected_map_id is not None and current_map == int(expected_map_id):
            return current_map
        time.sleep(poll_interval)
    raise TimeoutError(f"map did not change to {expected_map_id} within {timeout_seconds:.1f}s")


def cmd_resume_travel_plan(
    config: FleetConfig,
    registry_path: Path | None,
    arrival_radius: float,
    max_legs: int,
    leg_timeout: float,
    poll_interval: float,
    min_progress_yards: float,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1

    ok = True
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        try:
            state = load_route_state(config, leader_id)
        except FileNotFoundError:
            print(f"{leader_id}: no saved travel plan state")
            ok = False
            continue

        detail = {}
        for k in ("planName", "nodeIds", "stepIndex", "stepCount", "fromNodeId", "toNodeId"):
            v = state.get(k)
            if v is not None:
                detail[k] = v

        plan_name = detail.get("planName") or (state.get("detail") or {}).get("planName")
        node_ids = detail.get("nodeIds") or (state.get("detail") or {}).get("nodeIds")
        step_index = detail.get("stepIndex") or (state.get("detail") or {}).get("stepIndex")

        if not plan_name or not node_ids:
            print(f"{leader_id}: saved state does not contain a travel plan; use resume-route instead")
            ok = False
            continue

        if step_index is not None and isinstance(step_index, int):
            remaining = node_ids[step_index:]
        else:
            remaining = node_ids

        print(
            f"{leader_id}: resuming travel plan {plan_name} "
            f"step {step_index}/{len(node_ids) if node_ids else '?'} "
            f"remaining nodes: {' -> '.join(remaining)}"
        )

        result = cmd_travel_node_execute(
            config,
            registry_path or TRAVEL_NODES_PATH,
            remaining,
            arrival_radius,
            max_legs,
            leg_timeout,
            poll_interval,
            min_progress_yards,
            leader_ids=[leader_id],
        )
        ok = ok and result == 0
    return 0 if ok else 1


def cmd_resume_route(
    config: FleetConfig,
    arrival_radius: float | None,
    max_legs: int,
    leg_timeout: float,
    poll_interval: float,
    min_progress_yards: float,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    selected = config.selected_leaders(fleet, leader_ids)
    if not selected:
        print("No matching leaders")
        return 1

    ok = True
    for index, leader in selected:
        leader_id = leader.get("id", f"leader-{index + 1}")
        try:
            state = load_route_state(config, leader_id)
            target = state.get("target", {})
            map_id = int(target["mapId"])
            radius = float(arrival_radius if arrival_radius is not None else target.get("arrivalRadius", 5.0))
            print(
                f"{leader_id}: resuming route state={state.get('state')} "
                f"target map={map_id} x={target['x']} y={target['y']} z={target['z']}"
            )
            result = cmd_route_goto(
                config,
                map_id,
                float(target["x"]),
                float(target["y"]),
                float(target["z"]),
                radius,
                max_legs,
                leg_timeout,
                poll_interval,
                min_progress_yards,
                leader_ids=[leader_id],
            )
            ok = ok and result == 0
        except (FileNotFoundError, KeyError, TypeError, ValueError, RuntimeError) as exc:
            ok = False
            print(f"{leader_id}: resume-route failed ({exc})")
    return 0 if ok else 1


def cmd_dashboard(config: FleetConfig, host: str, port: int) -> int:
    try:
        from team_status_server import run_team_status_server
    except ImportError:
        from bot_fleet_manager.team_status_server import run_team_status_server

    run_team_status_server(dashboard_targets(config), host=host, port=port)
    return 0


def cmd_tram_state(
    config: FleetConfig,
    time_offset_ms: int,
    calibrate: bool,
    calibrate_step_ms: int,
    watch_seconds: float,
    interval: float,
    json_output: bool,
    fleet: str = "",
    leader_ids: list[str] | None = None,
) -> int:
    try:
        from deeprun_tram import find_best_time_offset, predict_trams, prediction_to_dict
    except ImportError:
        from bot_fleet_manager.deeprun_tram import find_best_time_offset, predict_trams, prediction_to_dict

    dbc_path = ROOT / "Data" / "dbfilesclient" / "transportanimation.dbc"
    if not dbc_path.exists():
        print(f"Missing TransportAnimation.dbc: {dbc_path}", file=sys.stderr)
        return 2

    selected = config.selected_leaders(fleet, leader_ids)
    leader_info: dict[str, Any] | None = None
    leader_label = ""
    if selected:
        index, leader = selected[0]
        leader_label = leader.get("id", f"leader-{index + 1}")
        try:
            leader_info = leader_position(config, leader, index)
        except Exception as exc:
            print(f"{leader_label}: unable to read leader position ({exc})", file=sys.stderr)

    if calibrate:
        if not leader_info:
            print("--calibrate requires a reachable leader position", file=sys.stderr)
            return 1
        best = find_best_time_offset(dbc_path, leader_info, step_ms=calibrate_step_ms)
        if not best:
            print("unable to calibrate Deeprun tram offset", file=sys.stderr)
            return 1
        time_offset_ms, prediction = best
        print(
            f"Best Deeprun tram offset: {time_offset_ms}ms "
            f"(entry={prediction.entry} guid={prediction.guid} "
            f"xyDistance={prediction.xy_distance_to_leader:.1f}y "
            f"distance={prediction.distance_to_leader:.1f}y "
            f"pos=({prediction.x:.1f}, {prediction.y:.1f}, {prediction.z:.1f}))"
        )

    deadline = time.monotonic() + watch_seconds if watch_seconds > 0 else None
    while True:
        if selected:
            index, leader = selected[0]
            try:
                leader_info = leader_position(config, leader, index)
            except Exception:
                pass

        predictions = predict_trams(dbc_path, time_offset_ms=time_offset_ms, leader=leader_info)
        if json_output:
            print(json.dumps({
                "collectedAt": utc_timestamp(),
                "timeOffsetMs": time_offset_ms,
                "leader": leader_info,
                "trams": [prediction_to_dict(prediction) for prediction in predictions],
            }, indent=2))
        else:
            header = f"Deeprun tram prediction offset={time_offset_ms}ms"
            if leader_label and leader_info:
                pos = leader_info.get("position", {})
                header += (
                    f" leader={leader_label} map={leader_info.get('mapId')} "
                    f"pos=({float(pos.get('x', 0.0)):.1f}, {float(pos.get('y', 0.0)):.1f}, {float(pos.get('z', 0.0)):.1f})"
                )
            print(header)
            for prediction in predictions:
                dist = (
                    f" xy={prediction.xy_distance_to_leader:.1f}y dist={prediction.distance_to_leader:.1f}y"
                    if prediction.xy_distance_to_leader is not None and prediction.distance_to_leader is not None
                    else ""
                )
                print(
                    f"  entry={prediction.entry} guid={prediction.guid} "
                    f"t={prediction.time_ms}/{prediction.cycle_ms}ms "
                    f"station={prediction.nearest_station} stationXY={prediction.station_xy_distance:.1f}y "
                    f"atStation={str(prediction.at_station).lower()} "
                    f"boardable={str(prediction.boardable_at_station).lower()} "
                    f"pos=({prediction.x:.1f}, {prediction.y:.1f}, {prediction.z:.1f})"
                    f"{dist}"
                )

        if deadline is None or time.monotonic() >= deadline:
            break
        time.sleep(max(0.1, interval))

    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("start")
    sub.add_parser("landmarks", help="List built-in named route landmarks")
    travel_nodes_parser = sub.add_parser("travel-nodes", help="List long-range travel nodes and routes")
    travel_nodes_parser.add_argument("--registry", type=Path, default=None, help="Travel node registry path")
    travel_nodes_parser.add_argument("--routes", action="store_true", help="Also list named travel routes")
    supervise_parser = sub.add_parser("supervise")
    supervise_parser.add_argument("--dashboard", action="store_true", help="Also start the team status dashboard")
    supervise_parser.add_argument("--dashboard-port", type=int, default=8780, help="Dashboard port")
    supervise_parser.add_argument("--dashboard-host", default="127.0.0.1", help="Dashboard bind address")
    supervise_parser.add_argument(
        "-v",
        "--verbose",
        "--debug",
        "--v",
        action="store_true",
        help="Stream each leader's log output to the supervisor console",
    )
    status_parser = sub.add_parser("status")
    status_parser.add_argument("--fleet", default="")
    status_parser.add_argument("--leader", action="append", default=[])
    stop_parser = sub.add_parser("stop")
    stop_parser.add_argument("--fleet", default="")
    stop_parser.add_argument("--leader", action="append", default=[])
    command_parser = sub.add_parser("command")
    command_parser.add_argument("--fleet", default="")
    command_parser.add_argument("--leader", action="append", default=[])
    command_parser.add_argument("text")
    area_trigger_parser = sub.add_parser("area-trigger", help="Send CMSG_AREATRIGGER through selected leader APIs")
    area_trigger_parser.add_argument("--fleet", default="")
    area_trigger_parser.add_argument("--leader", action="append", default=[])
    area_trigger_parser.add_argument("trigger_id", type=int)
    goto_parser = sub.add_parser("goto")
    goto_parser.add_argument("--fleet", default="")
    goto_parser.add_argument("--leader", action="append", default=[])
    goto_parser.add_argument("mapId", type=int)
    goto_parser.add_argument("x", type=float)
    goto_parser.add_argument("y", type=float)
    goto_parser.add_argument("z", type=float)
    goto_parser.add_argument("--arrival-radius", type=float, default=3.0)
    pathfind_goto_parser = sub.add_parser("pathfind-goto", help="Pathfind through the configured path service, then send waypoints")
    pathfind_goto_parser.add_argument("--fleet", default="")
    pathfind_goto_parser.add_argument("--leader", action="append", default=[])
    pathfind_goto_parser.add_argument("mapId", type=int)
    pathfind_goto_parser.add_argument("x", type=float)
    pathfind_goto_parser.add_argument("y", type=float)
    pathfind_goto_parser.add_argument("z", type=float)
    pathfind_goto_parser.add_argument("--arrival-radius", type=float, default=3.0)
    plan_route_parser = sub.add_parser("plan-route", help="Ask the path service for an iterative route without moving leaders")
    plan_route_parser.add_argument("--fleet", default="")
    plan_route_parser.add_argument("--leader", action="append", default=[])
    plan_route_parser.add_argument("mapId", type=int)
    plan_route_parser.add_argument("x", type=float)
    plan_route_parser.add_argument("y", type=float)
    plan_route_parser.add_argument("z", type=float)
    plan_route_parser.add_argument("--arrival-radius", type=float, default=5.0)
    plan_route_parser.add_argument("--max-legs", type=int, default=8)
    route_goto_parser = sub.add_parser("route-goto", help="Move leaders one planned route leg at a time, replanning between legs")
    route_goto_parser.add_argument("--fleet", default="")
    route_goto_parser.add_argument("--leader", action="append", default=[])
    route_goto_parser.add_argument("mapId", type=int)
    route_goto_parser.add_argument("x", type=float)
    route_goto_parser.add_argument("y", type=float)
    route_goto_parser.add_argument("z", type=float)
    route_goto_parser.add_argument("--arrival-radius", type=float, default=5.0)
    route_goto_parser.add_argument("--max-legs", type=int, default=8)
    route_goto_parser.add_argument("--leg-timeout", type=float, default=180.0)
    route_goto_parser.add_argument("--poll-interval", type=float, default=1.0)
    route_goto_parser.add_argument("--min-progress-yards", type=float, default=15.0)
    route_demo_parser = sub.add_parser("route-demo", help="Run a named route demo using built-in landmarks")
    route_demo_parser.add_argument("--fleet", default="")
    route_demo_parser.add_argument("--leader", action="append", default=[])
    route_demo_parser.add_argument("--plan-only", action="store_true", help="Only print the route plan; do not move")
    route_demo_parser.add_argument("--arrival-radius", type=float, default=5.0)
    route_demo_parser.add_argument("--max-legs", type=int, default=8)
    route_demo_parser.add_argument("--leg-timeout", type=float, default=180.0)
    route_demo_parser.add_argument("--poll-interval", type=float, default=1.0)
    route_demo_parser.add_argument("--min-progress-yards", type=float, default=15.0)
    route_demo_parser.add_argument("name", help="Landmark/demo name, e.g. goldshire or ironforge-probe")
    travel_plan_parser = sub.add_parser("travel-plan", help="Plan a read-only chained route through travel nodes")
    travel_plan_parser.add_argument("--fleet", default="")
    travel_plan_parser.add_argument("--leader", action="append", default=[])
    travel_plan_parser.add_argument("--registry", type=Path, default=None, help="Travel node registry path")
    travel_plan_parser.add_argument("--static", action="store_true", help="Plan only between listed nodes; do not query live leaders")
    travel_plan_parser.add_argument("--arrival-radius", type=float, default=8.0)
    travel_plan_parser.add_argument("--max-legs", type=int, default=8)
    travel_plan_parser.add_argument(
        "route_or_nodes",
        nargs="+",
        help="Named route such as deeprun-prototype, or an explicit sequence of travel node ids",
    )
    travel_survey_parser = sub.add_parser("travel-survey", help="Record live leader position/state samples for route-node surveying")
    travel_survey_parser.add_argument("--fleet", default="")
    travel_survey_parser.add_argument("--leader", action="append", default=[])
    travel_survey_parser.add_argument("--label", default="survey", help="Survey label stored in each sample and default output filename")
    travel_survey_parser.add_argument("--duration", type=float, default=0.0, help="Seconds to sample; 0 means until Ctrl-C")
    travel_survey_parser.add_argument("--interval", type=float, default=1.0, help="Seconds between polls")
    travel_survey_parser.add_argument("--min-distance", type=float, default=1.0, help="Only write samples after this many yards of movement")
    travel_survey_parser.add_argument("--output", type=Path, default=None, help="JSONL output path")
    travel_survey_parser.add_argument("--raw", action="store_true", help="Include full /world/self and /status responses in each sample")
    travel_capture_parser = sub.add_parser("travel-capture", help="Capture the selected leader's current position into the travel registry")
    travel_capture_parser.add_argument("--fleet", default="")
    travel_capture_parser.add_argument("--leader", action="append", default=[])
    travel_capture_parser.add_argument("--registry", type=Path, default=None, help="Travel node registry path")
    travel_capture_parser.add_argument("--type", default="walk", help="Travel node type, e.g. walk, tram_entrance, tram_platform, boat_dock")
    travel_capture_parser.add_argument("--name", default="", help="Human-readable node name")
    travel_capture_parser.add_argument("--description", default="", help="Human-readable node description")
    travel_capture_parser.add_argument("--radius", type=float, default=8.0)
    travel_capture_parser.add_argument("--verified", action="store_true", help="Mark captured node as verified")
    travel_capture_parser.add_argument("node_id")
    travel_node_execute_parser = sub.add_parser(
        "travel-node-execute",
        help="Execute a travel-node plan: walk legs and trigger transitions (tram/boat/portal) step by step",
    )
    travel_node_execute_parser.add_argument("--fleet", default="")
    travel_node_execute_parser.add_argument("--leader", action="append", default=[])
    travel_node_execute_parser.add_argument("--registry", type=Path, default=None, help="Travel node registry path")
    travel_node_execute_parser.add_argument("--arrival-radius", type=float, default=8.0)
    travel_node_execute_parser.add_argument("--max-legs", type=int, default=8)
    travel_node_execute_parser.add_argument("--leg-timeout", type=float, default=180.0)
    travel_node_execute_parser.add_argument("--poll-interval", type=float, default=1.0)
    travel_node_execute_parser.add_argument("--min-progress-yards", type=float, default=15.0)
    travel_node_execute_parser.add_argument(
        "route_or_nodes",
        nargs="+",
        help="Named route such as deeprun-prototype, or an explicit sequence of travel node ids",
    )
    resume_travel_parser = sub.add_parser(
        "resume-travel-plan", help="Resume a travel-node-execute plan from its last saved step"
    )
    resume_travel_parser.add_argument("--fleet", default="")
    resume_travel_parser.add_argument("--leader", action="append", default=[])
    resume_travel_parser.add_argument("--registry", type=Path, default=None, help="Travel node registry path")
    resume_travel_parser.add_argument("--arrival-radius", type=float, default=8.0)
    resume_travel_parser.add_argument("--max-legs", type=int, default=8)
    resume_travel_parser.add_argument("--leg-timeout", type=float, default=180.0)
    resume_travel_parser.add_argument("--poll-interval", type=float, default=1.0)
    resume_travel_parser.add_argument("--min-progress-yards", type=float, default=15.0)
    resume_route_parser = sub.add_parser("resume-route", help="Resume each leader's last saved guarded route target")
    resume_route_parser.add_argument("--fleet", default="")
    resume_route_parser.add_argument("--leader", action="append", default=[])
    resume_route_parser.add_argument("--arrival-radius", type=float, default=None)
    resume_route_parser.add_argument("--max-legs", type=int, default=8)
    resume_route_parser.add_argument("--leg-timeout", type=float, default=180.0)
    resume_route_parser.add_argument("--poll-interval", type=float, default=1.0)
    resume_route_parser.add_argument("--min-progress-yards", type=float, default=15.0)
    dashboard_parser = sub.add_parser("dashboard", help="Start textual team status dashboard")
    dashboard_parser.add_argument("--port", type=int, default=8780, help="Dashboard port")
    dashboard_parser.add_argument("--host", default="127.0.0.1", help="Dashboard bind address")
    tram_state_parser = sub.add_parser("tram-state", help="Predict Deeprun Tram car positions from DBC + spawn data")
    tram_state_parser.add_argument("--fleet", default="")
    tram_state_parser.add_argument("--leader", action="append", default=[])
    tram_state_parser.add_argument("--time-offset-ms", type=int, default=0, help="Calibration offset added to local wall clock")
    tram_state_parser.add_argument("--calibrate", action="store_true", help="Find the offset that puts a predicted tram nearest the selected leader")
    tram_state_parser.add_argument("--calibrate-step-ms", type=int, default=250, help="Calibration search step size")
    tram_state_parser.add_argument("--watch", type=float, default=0.0, help="Seconds to continuously print predictions")
    tram_state_parser.add_argument("--interval", type=float, default=1.0, help="Seconds between watch samples")
    tram_state_parser.add_argument("--json", action="store_true", help="Print JSON instead of text")

    args = parser.parse_args(argv)
    config = FleetConfig(resolve_path(str(args.config)))

    if args.command == "start":
        return cmd_start(config)
    if args.command == "landmarks":
        return cmd_landmarks()
    if args.command == "travel-nodes":
        return cmd_travel_nodes(args.registry, args.routes)
    if args.command == "supervise":
        return cmd_supervise(
            config,
            dashboard=args.dashboard,
            dashboard_host=args.dashboard_host,
            dashboard_port=args.dashboard_port,
            verbose=args.verbose,
        )
    if args.command == "status":
        return cmd_status(config, args.fleet, args.leader)
    if args.command == "stop":
        return post_all(config, "/movement/stop", {"reason": "fleet stop"}, args.fleet, args.leader)
    if args.command == "command":
        return post_all(config, "/commands", {"command": args.text}, args.fleet, args.leader)
    if args.command == "area-trigger":
        return post_all(config, "/area-trigger", {"id": args.trigger_id}, args.fleet, args.leader)
    if args.command == "goto":
        return post_all(config, "/movement/goto", {
            "mapId": args.mapId,
            "x": args.x,
            "y": args.y,
            "z": args.z,
            "arrivalRadius": args.arrival_radius,
        }, args.fleet, args.leader)
    if args.command == "pathfind-goto":
        return cmd_pathfind_goto(
            config,
            args.mapId,
            args.x,
            args.y,
            args.z,
            args.arrival_radius,
            args.fleet,
            args.leader,
        )
    if args.command == "plan-route":
        return cmd_plan_route(
            config,
            args.mapId,
            args.x,
            args.y,
            args.z,
            args.arrival_radius,
            args.max_legs,
            args.fleet,
            args.leader,
        )
    if args.command == "route-goto":
        return cmd_route_goto(
            config,
            args.mapId,
            args.x,
            args.y,
            args.z,
            args.arrival_radius,
            args.max_legs,
            args.leg_timeout,
            args.poll_interval,
            args.min_progress_yards,
            args.fleet,
            args.leader,
        )
    if args.command == "route-demo":
        return cmd_route_demo(
            config,
            args.name,
            args.plan_only,
            args.arrival_radius,
            args.max_legs,
            args.leg_timeout,
            args.poll_interval,
            args.min_progress_yards,
            args.fleet,
            args.leader,
        )
    if args.command == "travel-plan":
        return cmd_travel_plan(
            config,
            args.registry,
            args.route_or_nodes,
            args.static,
            args.arrival_radius,
            args.max_legs,
            args.fleet,
            args.leader,
        )
    if args.command == "travel-survey":
        return cmd_travel_survey(
            config,
            args.label,
            args.duration,
            args.interval,
            args.min_distance,
            args.output,
            args.raw,
            args.fleet,
            args.leader,
        )
    if args.command == "travel-capture":
        return cmd_travel_capture(
            config,
            args.registry,
            args.node_id,
            args.type,
            args.name,
            args.description,
            args.radius,
            args.verified,
            args.fleet,
            args.leader,
)
    if args.command == "travel-node-execute":
        return cmd_travel_node_execute(
            config,
            args.registry,
            args.route_or_nodes,
            args.arrival_radius,
            args.max_legs,
            args.leg_timeout,
            args.poll_interval,
            args.min_progress_yards,
            args.fleet,
            args.leader,
        )
    if args.command == "resume-travel-plan":
        return cmd_resume_travel_plan(
            config,
            args.registry,
            args.arrival_radius,
            args.max_legs,
            args.leg_timeout,
            args.poll_interval,
            args.min_progress_yards,
            args.fleet,
            args.leader,
        )
    if args.command == "resume-route":
        return cmd_resume_route(
            config,
            args.arrival_radius,
            args.max_legs,
            args.leg_timeout,
            args.poll_interval,
            args.min_progress_yards,
            args.fleet,
            args.leader,
        )
    if args.command == "dashboard":
        return cmd_dashboard(config, args.host, args.port)
    if args.command == "tram-state":
        return cmd_tram_state(
            config,
            args.time_offset_ms,
            args.calibrate,
            args.calibrate_step_ms,
            args.watch,
            args.interval,
            args.json,
            args.fleet,
            args.leader,
        )
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
