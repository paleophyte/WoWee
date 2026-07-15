#!/usr/bin/env python3
"""Print the current Turtle WoW PIN from a local roster totpSecret."""

from __future__ import annotations

import argparse
import base64
import hmac
import hashlib
import json
import struct
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ROSTER = ROOT / "tools" / "provisioning" / "roster.json"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        doc = json.load(handle)
    if not isinstance(doc, dict):
        raise ValueError("roster root must be an object")
    return doc


def normalize_secret(secret: str) -> str:
    return "".join(ch for ch in secret.upper() if not ch.isspace()).rstrip("=")


def decode_base32_secret(secret: str) -> bytes:
    normalized = normalize_secret(secret)
    if not normalized:
        raise ValueError("totpSecret is empty")
    padding = "=" * ((8 - (len(normalized) % 8)) % 8)
    try:
        return base64.b32decode(normalized + padding, casefold=True)
    except Exception as exc:
        raise ValueError("totpSecret is not valid base32") from exc


def totp(secret: str, when: int | None = None) -> int:
    timestamp = int(time.time() if when is None else when)
    counter = timestamp // 30
    key = decode_base32_secret(secret)
    digest = hmac.new(key, struct.pack(">Q", counter), hashlib.sha1).digest()
    offset = digest[-1] & 0x0F
    value = struct.unpack(">I", digest[offset : offset + 4])[0] & 0x7FFFFFFF
    return value % 1_000_000


def find_account(doc: dict[str, Any], account_name: str) -> dict[str, Any]:
    accounts = doc.get("accounts", [])
    if not isinstance(accounts, list):
        raise ValueError("roster accounts must be an array")
    if not accounts:
        raise ValueError("roster has no accounts")

    if account_name:
        for account in accounts:
            if not isinstance(account, dict):
                continue
            if str(account.get("account", "")).lower() == account_name.lower():
                return account
        raise ValueError(f"account not found in roster: {account_name}")

    first = accounts[0]
    if not isinstance(first, dict):
        raise ValueError("first roster account is not an object")
    return first


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--roster", type=Path, default=DEFAULT_ROSTER, help="Local roster JSON with totpSecret")
    parser.add_argument("--account", default="", help="Account name to use when roster has multiple accounts")
    parser.add_argument("--at", type=int, help="Unix timestamp for testing; defaults to now")
    args = parser.parse_args()

    doc = load_json(args.roster)
    defaults = doc.get("defaults", {})
    if not isinstance(defaults, dict):
        defaults = {}
    account = find_account(doc, args.account)
    secret = str(account.get("totpSecret") or defaults.get("totpSecret") or "").strip()
    pin = totp(secret, args.at)
    now = int(time.time() if args.at is None else args.at)
    remaining = 30 - (now % 30)
    account_name = str(account.get("account", "<unknown>"))

    print(f"Account: {account_name}")
    print(f"PIN: {pin:06d}")
    print(f"Valid for: {remaining}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
