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


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")


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
        self.integrity_dir = self.doc.get("integrityDir", "")
        if not self.leaders:
            raise ValueError("fleet config must contain at least one leader")

    @property
    def log_dir(self) -> Path:
        return self.runtime_dir / "logs"

    @property
    def initial_restart_backoff(self) -> float:
        return float(self.supervision.get("initialRestartBackoffSeconds", 5.0))

    @property
    def max_restart_backoff(self) -> float:
        return float(self.supervision.get("maxRestartBackoffSeconds", 60.0))

    @property
    def max_restarts(self) -> int:
        return int(self.supervision.get("maxRestarts", 0))

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

    def stream_output(self) -> None:
        if not self.process or not self.process.stdout:
            return
        try:
            for line in self.process.stdout:
                if self.log_handle:
                    self.log_handle.write(line)
                    self.log_handle.flush()
                print(f"[{self.leader_id}] {line}", end="", flush=True)
        except ValueError:
            return

    def start(self) -> None:
        self.config.log_dir.mkdir(parents=True, exist_ok=True)
        if self.log_handle:
            self.log_handle.close()
        log_path = self.config.log_dir / f"{self.leader_id}.log"
        log_mode = "a" if self.verbose else "ab"
        log_kwargs = {"encoding": "utf-8", "errors": "replace"} if self.verbose else {}
        self.log_handle = log_path.open(log_mode, **log_kwargs)
        print(f"Starting {self.leader_id} with {self.settings_path}; log={log_path}")
        if self.verbose:
            self.process = subprocess.Popen(
                [str(self.config.headless), str(self.settings_path)],
                cwd=str(ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                creationflags=creation_flags(),
                env=runtime_env(self.config),
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
            self.stream_thread = threading.Thread(target=self.stream_output, daemon=True)
            self.stream_thread.start()
        else:
            self.process = subprocess.Popen(
                [str(self.config.headless), str(self.settings_path)],
                cwd=str(ROOT),
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
                creationflags=creation_flags(),
                env=runtime_env(self.config),
            )

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

        print(f"{self.leader_id}: exited with code {rc}")
        self.process = None
        if self.stream_thread:
            self.stream_thread.join(timeout=2.0)
            self.stream_thread = None
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


def cmd_dashboard(config: FleetConfig, host: str, port: int) -> int:
    try:
        from team_status_server import run_team_status_server
    except ImportError:
        from bot_fleet_manager.team_status_server import run_team_status_server

    run_team_status_server(dashboard_targets(config), host=host, port=port)
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("start")
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
    goto_parser = sub.add_parser("goto")
    goto_parser.add_argument("--fleet", default="")
    goto_parser.add_argument("--leader", action="append", default=[])
    goto_parser.add_argument("mapId", type=int)
    goto_parser.add_argument("x", type=float)
    goto_parser.add_argument("y", type=float)
    goto_parser.add_argument("z", type=float)
    goto_parser.add_argument("--arrival-radius", type=float, default=3.0)
    dashboard_parser = sub.add_parser("dashboard", help="Start textual team status dashboard")
    dashboard_parser.add_argument("--port", type=int, default=8780, help="Dashboard port")
    dashboard_parser.add_argument("--host", default="127.0.0.1", help="Dashboard bind address")

    args = parser.parse_args(argv)
    config = FleetConfig(resolve_path(str(args.config)))

    if args.command == "start":
        return cmd_start(config)
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
    if args.command == "goto":
        return post_all(config, "/movement/goto", {
            "mapId": args.mapId,
            "x": args.x,
            "y": args.y,
            "z": args.z,
            "arrivalRadius": args.arrival_radius,
        }, args.fleet, args.leader)
    if args.command == "dashboard":
        return cmd_dashboard(config, args.host, args.port)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
