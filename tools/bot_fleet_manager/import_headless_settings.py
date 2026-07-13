#!/usr/bin/env python3
"""Create a one-leader fleet config from an existing wowee_headless settings file."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        doc = json.load(handle)
    if not isinstance(doc, dict):
        raise ValueError("settings root must be a JSON object")
    return doc


def write_json(path: Path, doc: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2)
        handle.write("\n")


def resolve_path(raw: str) -> Path:
    path = Path(raw)
    return path if path.is_absolute() else ROOT / path


def bot_name_from_command(command: str) -> str:
    lowered = command.strip().lower()
    if not lowered.startswith(".bot add "):
        return ""
    return command.strip()[len(".bot add "):].strip()


def build_fleet_config(settings: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    auth = settings.get("auth", {})
    client = settings.get("client", {})
    realm = settings.get("realm", {})
    character = settings.get("character", {})
    api = settings.get("api", {})
    bots = settings.get("bots", {})
    automation = settings.get("automation", {})

    party_bots: list[str] = []
    if isinstance(bots, dict) and bots.get("enabled", False):
        for name in bots.get("names", []):
            if isinstance(name, str) and name.strip():
                party_bots.append(name.strip())

    startup_commands: list[str] = []
    for command in automation.get("onEnterWorldCommands", []) if isinstance(automation, dict) else []:
        if not isinstance(command, str) or not command.strip():
            continue
        bot_name = bot_name_from_command(command)
        if bot_name:
            party_bots.append(bot_name)
        else:
            startup_commands.append(command.strip())

    seen: set[str] = set()
    unique_party_bots = []
    for name in party_bots:
        key = name.lower()
        if key in seen:
            continue
        seen.add(key)
        unique_party_bots.append(name)

    return {
        "woweeHeadless": args.wowee_headless,
        "runtimeDir": "tools/bot_fleet_manager/runtime",
        "launchDelaySeconds": 2.0,
        "supervision": {
            "initialRestartBackoffSeconds": 5.0,
            "maxRestartBackoffSeconds": 60.0,
            "maxRestarts": 0,
        },
        "defaults": {
            "auth": {
                "host": auth.get("host", "127.0.0.1"),
                "port": int(auth.get("port", 3724)),
            },
            "client": client,
            "realm": realm,
            "api": {
                "bind": api.get("bind", "127.0.0.1"),
                "basePort": int(api.get("port", 8787)),
                "maxMessages": int(api.get("maxMessages", 500)),
            },
            "automation": {
                "commandDelaySeconds": float(automation.get("commandDelaySeconds", 0.25)) if isinstance(automation, dict) else 0.25,
                "onEnterWorldCommands": [],
            },
        },
        "leaders": [
            {
                "id": args.leader_id,
                "fleet": args.fleet,
                "account": auth.get("account", ""),
                "password": auth.get("password", ""),
                "character": character.get("name", ""),
                "apiPort": int(api.get("port", 8787)),
                "partyBots": unique_party_bots,
                "startupCommands": startup_commands,
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="tools/headless_client/settings.json", help="Existing headless settings JSON")
    parser.add_argument("--output", default="tools/bot_fleet_manager/fleet.settings.json", help="Local fleet config to write")
    parser.add_argument("--wowee-headless", default="build/bin/wowee_headless.exe")
    parser.add_argument("--leader-id", default="demo-leader-1")
    parser.add_argument("--fleet", default="demo")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    fleet = build_fleet_config(load_json(resolve_path(args.input)), args)
    if args.dry_run:
        redacted = json.loads(json.dumps(fleet))
        for leader in redacted.get("leaders", []):
            if "password" in leader:
                leader["password"] = "<redacted>"
            if "account" in leader:
                leader["account"] = "<redacted>"
        print(json.dumps(redacted, indent=2))
        return 0

    output = resolve_path(args.output)
    write_json(output, fleet)
    print(f"Wrote {output}")
    print("This file may contain account credentials and is ignored by git.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
