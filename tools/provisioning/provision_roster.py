#!/usr/bin/env python3
"""Provision many CMaNGOS accounts and characters from one roster JSON file."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
PROVISIONING = TOOLS / "provisioning"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        doc = json.load(f)
    if not isinstance(doc, dict):
        raise ValueError("roster root must be a JSON object")
    return doc


def bool_value(value: Any, fallback: bool) -> bool:
    if value is None:
        return fallback
    return bool(value)


def string_value(value: Any, fallback: str = "") -> str:
    if value is None:
        return fallback
    if not isinstance(value, str):
        raise ValueError(f"expected string, got {type(value).__name__}")
    return value


def path_from(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def scrub_command(cmd: list[str]) -> str:
    scrubbed: list[str] = []
    hide_next = False
    for item in cmd:
        if hide_next:
            scrubbed.append("********")
            hide_next = False
            continue
        scrubbed.append(item)
        if item in {"--password", "--soap-password", "--admin-pass"}:
            hide_next = True
    return " ".join(scrubbed)


def run_step(cmd: list[str], dry_run: bool) -> int:
    print(("Would run: " if dry_run else "Running: ") + scrub_command(cmd))
    if dry_run:
        return 0
    return subprocess.call(cmd, cwd=ROOT)


def account_command(args: argparse.Namespace, account: str, password: str, expansion: int, gmlevel: int = 0, realm_id: int = -1) -> list[str]:
    if args.account_mode == "ssh":
        cmd = [
            sys.executable,
            str(args.account_ssh_script),
            account,
            "--password",
            password,
            "--expansion",
            str(expansion),
        ]
        if gmlevel > 0:
            cmd.extend(["--gmlevel", str(gmlevel), "--realm-id", str(realm_id)])
        if args.env:
            cmd.extend(["--env", str(args.env)])
        return cmd

    cmd = [
        sys.executable,
        str(args.account_direct_script),
        "create",
        "--soap-url",
        args.soap_url,
        "--admin-user",
        args.admin_user,
        "--admin-pass",
        args.admin_pass,
        "--account",
        account,
        "--password",
        password,
        "--expansion",
        str(expansion),
    ]
    if gmlevel > 0:
        cmd.extend(["--gmlevel", str(gmlevel), "--realm-id", str(realm_id)])
    return cmd


def character_command(
    args: argparse.Namespace,
    defaults: dict[str, Any],
    account_doc: dict[str, Any],
    character_doc: dict[str, Any],
    account: str,
    password: str,
) -> list[str]:
    settings = string_value(character_doc.get("settings"), string_value(account_doc.get("settings"), string_value(defaults.get("settings"))))
    wowee = string_value(
        character_doc.get("woweeHeadless"),
        string_value(account_doc.get("woweeHeadless"), string_value(defaults.get("woweeHeadless"), "build/bin/wowee_headless.exe")),
    )
    name = string_value(character_doc.get("name"))
    race = string_value(character_doc.get("race"), string_value(account_doc.get("race"), string_value(defaults.get("race"), "human")))
    cls = string_value(character_doc.get("class"), string_value(account_doc.get("class"), string_value(defaults.get("class"), "warrior")))
    gender = string_value(character_doc.get("gender"), string_value(account_doc.get("gender"), string_value(defaults.get("gender"), "male")))

    if not settings:
        raise ValueError(f"{account}/{name}: settings is required")
    if not name:
        raise ValueError(f"{account}: character name is required")

    cmd = [
        sys.executable,
        str(args.character_script),
        "--settings",
        str(path_from(settings)),
        "--wowee-headless",
        str(path_from(wowee)),
        "--account",
        account,
        "--password",
        password,
        "--name",
        name,
        "--race",
        race,
        "--class",
        cls,
        "--gender",
        gender,
    ]

    for option_name, cli_name in (
        ("skin", "--skin"),
        ("face", "--face"),
        ("hairStyle", "--hair-style"),
        ("hairColor", "--hair-color"),
        ("facialHair", "--facial-hair"),
    ):
        value = character_doc.get(option_name, account_doc.get(option_name, defaults.get(option_name)))
        if value is not None:
            cmd.extend([cli_name, str(int(value))])

    return cmd


def iter_characters(account_doc: dict[str, Any]) -> list[dict[str, Any]]:
    if "characters" in account_doc:
        characters = account_doc["characters"]
        if not isinstance(characters, list):
            raise ValueError("account.characters must be an array")
        return [ch for ch in characters if isinstance(ch, dict)]
    if "character" in account_doc:
        character = account_doc["character"]
        if isinstance(character, str):
            return [{"name": character}]
        if isinstance(character, dict):
            return [character]
    return []


def provision(args: argparse.Namespace) -> int:
    roster = load_json(args.roster)
    defaults = roster.get("defaults", {})
    if not isinstance(defaults, dict):
        raise ValueError("defaults must be an object")
    accounts = roster.get("accounts", [])
    if not isinstance(accounts, list):
        raise ValueError("accounts must be an array")

    failures = 0
    for raw_account in accounts:
        if not isinstance(raw_account, dict):
            continue
        account = string_value(raw_account.get("account")).strip()
        password = string_value(raw_account.get("password"), string_value(defaults.get("accountPassword"))).strip()
        expansion = int(raw_account.get("expansion", defaults.get("expansion", 1)))
        gmlevel = int(raw_account.get("gmlevel", defaults.get("gmlevel", 0)))
        realm_id = int(raw_account.get("realmId", defaults.get("realmId", -1)))
        if not account:
            raise ValueError("account is required")
        if not password:
            raise ValueError(f"{account}: password is required")

        create_account = bool_value(raw_account.get("createAccount"), bool_value(defaults.get("createAccount"), True))
        create_character = bool_value(raw_account.get("createCharacter"), bool_value(defaults.get("createCharacter"), True))

        if create_account and not args.skip_accounts:
            rc = run_step(account_command(args, account, password, expansion, gmlevel, realm_id), args.dry_run)
            if rc != 0:
                failures += 1
                print(f"Account step failed for {account} with exit code {rc}", file=sys.stderr)
                if not args.continue_on_error:
                    return rc

        if create_character and not args.skip_characters:
            for character in iter_characters(raw_account):
                try:
                    cmd = character_command(args, defaults, raw_account, character, account, password)
                except Exception as exc:
                    failures += 1
                    print(f"Character step setup failed for {account}: {exc}", file=sys.stderr)
                    if not args.continue_on_error:
                        return 2
                    continue
                rc = run_step(cmd, args.dry_run)
                if rc != 0:
                    failures += 1
                    print(f"Character step failed for {account} with exit code {rc}", file=sys.stderr)
                    if not args.continue_on_error:
                        return rc

    if failures:
        print(f"Provisioning finished with {failures} failure(s).")
        return 1
    print("Provisioning finished successfully.")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("roster", type=Path, help="Roster JSON file")
    parser.add_argument("--account-mode", choices=("ssh", "direct-soap"), default="ssh")
    parser.add_argument("--env", type=Path, default=TOOLS / ".env", help="Env file for SSH account mode")
    parser.add_argument("--account-ssh-script", type=Path, default=PROVISIONING / "create_account_ssh.py")
    parser.add_argument("--account-direct-script", type=Path, default=PROVISIONING / "create_account_direct_soap.py")
    parser.add_argument("--character-script", type=Path, default=PROVISIONING / "create_character.py")
    parser.add_argument("--soap-url", default="", help="Direct SOAP mode URL")
    parser.add_argument("--admin-user", default="", help="Direct SOAP mode admin user")
    parser.add_argument("--admin-pass", default="", help="Direct SOAP mode admin password")
    parser.add_argument("--skip-accounts", action="store_true")
    parser.add_argument("--skip-characters", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    if args.account_mode == "direct-soap" and not args.skip_accounts:
        missing = [name for name in ("soap_url", "admin_user", "admin_pass") if not getattr(args, name)]
        if missing:
            parser.error("--account-mode direct-soap requires --soap-url, --admin-user, and --admin-pass")
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        return provision(parse_args(argv))
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
