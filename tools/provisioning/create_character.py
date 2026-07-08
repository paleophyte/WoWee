#!/usr/bin/env python3
"""Create a WoW character by driving wowee_headless in provisioning mode."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def load_settings(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        doc = json.load(f)
    if not isinstance(doc, dict):
        raise ValueError("settings root must be a JSON object")
    return doc


def set_nested(doc: dict[str, Any], section: str, key: str, value: Any) -> None:
    obj = doc.setdefault(section, {})
    if not isinstance(obj, dict):
        raise ValueError(f"{section} must be a JSON object")
    obj[key] = value


def build_settings(args: argparse.Namespace) -> dict[str, Any]:
    doc = load_settings(args.settings)

    if args.account:
        set_nested(doc, "auth", "account", args.account)
    if args.password:
        set_nested(doc, "auth", "password", args.password)
    if args.auth_host:
        set_nested(doc, "auth", "host", args.auth_host)
    if args.auth_port:
        set_nested(doc, "auth", "port", args.auth_port)
    if args.realm:
        set_nested(doc, "realm", "name", args.realm)

    set_nested(doc, "character", "name", args.name)
    doc["provision"] = {
        "createCharacter": {
            "enabled": True,
            "name": args.name,
            "race": args.race,
            "class": args.character_class,
            "gender": args.gender,
            "skin": args.skin,
            "face": args.face,
            "hairStyle": args.hair_style,
            "hairColor": args.hair_color,
            "facialHair": args.facial_hair,
            "exitAfterCreate": True,
        }
    }
    return doc


def build_runtime_env(args: argparse.Namespace) -> dict[str, str]:
    env = os.environ.copy()
    path_parts: list[str] = []

    exe_dir = args.wowee_headless.resolve().parent
    if exe_dir.exists():
        path_parts.append(str(exe_dir))

    if args.msys_ucrt_bin:
        msys_bin = args.msys_ucrt_bin
        if msys_bin.exists():
            path_parts.append(str(msys_bin.resolve()))
        else:
            print(f"Warning: --msys-ucrt-bin does not exist: {msys_bin}", file=sys.stderr)
    elif os.name == "nt":
        default_msys_bin = Path("C:/msys64/ucrt64/bin")
        if default_msys_bin.exists():
            path_parts.append(str(default_msys_bin))

    if path_parts:
        env["PATH"] = os.pathsep.join(path_parts + [env.get("PATH", "")])
        print("Added runtime PATH entries:", os.pathsep.join(path_parts))
    return env


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", required=True, type=Path, help="Base wowee_headless settings JSON")
    parser.add_argument("--wowee-headless", default="build/bin/wowee_headless.exe", type=Path)
    parser.add_argument("--msys-ucrt-bin", type=Path, help="MSYS2 UCRT runtime bin directory; defaults to C:/msys64/ucrt64/bin on Windows if present")
    parser.add_argument("--account", help="Override auth.account")
    parser.add_argument("--password", help="Override auth.password")
    parser.add_argument("--auth-host", help="Override auth.host")
    parser.add_argument("--auth-port", type=int, help="Override auth.port")
    parser.add_argument("--realm", help="Override realm.name")
    parser.add_argument("--name", required=True, help="Character name to create")
    parser.add_argument("--race", required=True, help="Race name or numeric race id")
    parser.add_argument("--class", required=True, dest="character_class", help="Class name or numeric class id")
    parser.add_argument("--gender", default="male", help="male/female or 0/1")
    parser.add_argument("--skin", type=int, default=0)
    parser.add_argument("--face", type=int, default=0)
    parser.add_argument("--hair-style", type=int, default=0)
    parser.add_argument("--hair-color", type=int, default=0)
    parser.add_argument("--facial-hair", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true", help="Print generated settings and do not run the client")
    args = parser.parse_args(argv)

    generated = build_settings(args)
    if args.dry_run:
        print(json.dumps(generated, indent=2))
        return 0

    with tempfile.TemporaryDirectory(prefix="wowee-create-character-") as tmp:
        settings_path = Path(tmp) / "settings.json"
        settings_path.write_text(json.dumps(generated, indent=2), encoding="utf-8")
        cmd = [str(args.wowee_headless), str(settings_path)]
        print("Running:", " ".join(cmd))
        return subprocess.call(cmd, env=build_runtime_env(args))


if __name__ == "__main__":
    raise SystemExit(main())
