#!/usr/bin/env python3
"""Fetch MiniManager zone map art into the ignored fleet-manager runtime dir."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DEST = ROOT / "tools" / "bot_fleet_manager" / "runtime" / "map_assets" / "zone"
DEFAULT_CONTINENT_DEST = ROOT / "tools" / "bot_fleet_manager" / "runtime" / "map_assets" / "continent"
DEFAULT_REMOTE_DIR = "/var/www/minimanager/img/zone"
DEFAULT_REMOTE_MAP_DIR = "/var/www/minimanager/img/map"
DEFAULT_EXTERNAL_ENV = Path(r"C:\Users\admin\code\wow_server\.env")
CONTINENT_ASSETS = ["azeroth.jpg", "outland.jpg", "northrend.jpg"]


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    with path.open("r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def require(values: dict[str, str], key: str, env_path: Path) -> str:
    value = values.get(key, "").strip()
    if not value:
        raise SystemExit(f"{key} must be set in {env_path}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch MiniManager zone PNGs for the fleet dashboard")
    parser.add_argument("--env", type=Path, default=DEFAULT_EXTERNAL_ENV, help="Path to the wow_server .env file")
    parser.add_argument("--remote-dir", default=DEFAULT_REMOTE_DIR, help="Remote MiniManager img/zone directory")
    parser.add_argument("--remote-map-dir", default=DEFAULT_REMOTE_MAP_DIR, help="Remote MiniManager img/map directory")
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST, help="Local destination directory")
    parser.add_argument("--continent-dest", type=Path, default=DEFAULT_CONTINENT_DEST, help="Local continent map destination directory")
    parser.add_argument("--skip-continents", action="store_true", help="Only fetch zone PNGs")
    args = parser.parse_args()

    env_path = args.env.expanduser()
    if not env_path.exists():
        raise SystemExit(f"Missing env file: {env_path}")

    values = read_env(env_path)
    host = require(values, "MANGOS_HOST", env_path)
    user = require(values, "MANGOS_USER", env_path)
    key_path = require(values, "MANGOS_SSH_KEY_PATH", env_path)
    port = values.get("MANGOS_PORT", "22").strip() or "22"

    dest = args.dest.expanduser()
    dest.mkdir(parents=True, exist_ok=True)
    remote_glob = args.remote_dir.rstrip("/") + "/*.png"
    command = [
        "scp",
        "-i",
        key_path,
        "-P",
        port,
        "-o",
        "StrictHostKeyChecking=accept-new",
        f"{user}@{host}:{remote_glob}",
        str(dest) + os.sep,
    ]

    completed = subprocess.run(command, text=True)
    if completed.returncode != 0:
        return completed.returncode

    files = list(dest.glob("*.png"))
    total_bytes = sum(path.stat().st_size for path in files)
    print(f"Fetched {len(files)} zone map PNGs to {dest} ({total_bytes} bytes)")

    if not args.skip_continents:
        continent_dest = args.continent_dest.expanduser()
        continent_dest.mkdir(parents=True, exist_ok=True)
        for asset in CONTINENT_ASSETS:
            remote_asset = args.remote_map_dir.rstrip("/") + "/" + asset
            command = [
                "scp",
                "-i",
                key_path,
                "-P",
                port,
                "-o",
                "StrictHostKeyChecking=accept-new",
                f"{user}@{host}:{remote_asset}",
                str(continent_dest) + os.sep,
            ]
            completed = subprocess.run(command, text=True)
            if completed.returncode != 0:
                return completed.returncode
        continent_files = list(continent_dest.glob("*.*"))
        continent_bytes = sum(path.stat().st_size for path in continent_files)
        print(f"Fetched {len(continent_files)} continent map assets to {continent_dest} ({continent_bytes} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
