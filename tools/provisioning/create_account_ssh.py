#!/usr/bin/env python3
"""Create a WoW account over SSH using the server-local SOAP endpoint."""

from __future__ import annotations

import argparse
import getpass
import json
import pathlib
import re
import shlex
import subprocess
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_ENV = ROOT / ".env"


USERNAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{2,31}$")

SERVER_PROFILES = {
    "cmangos": {
        "label": "CMaNGOS",
        "env_prefixes": ("MANGOS",),
        "soap_namespace": "urn:MaNGOS",
        "default_expansion": 1,
        "default_soap_url": "http://127.0.0.1:7878/",
    },
    "azerothcore": {
        "label": "AzerothCore",
        "env_prefixes": ("AC", "AZEROTHCORE", "MANGOS"),
        "soap_namespace": "urn:AC",
        "default_expansion": 2,
        "default_soap_url": "http://127.0.0.1:7879/",
    },
    "vmangos": {
        "label": "VMangos",
        "env_prefixes": ("VMANGOS", "MANGOS"),
        "soap_namespace": "urn:MaNGOS",
        "default_expansion": 0,
        "default_soap_url": "http://127.0.0.1:7880/",
        "default_soap_username": "SERVERADMIN",
        "max_soap_password_len": 16,
    },
}


REMOTE_SCRIPT = r"""
import base64
import http.client
import json
import sys
import urllib.parse
import xml.etree.ElementTree as ET

payload = json.loads(sys.stdin.read())
soap_url = payload.get("soap_url") or "http://127.0.0.1:7878/"
soap_user = payload["soap_user"]
soap_password = payload["soap_password"]
soap_namespace = payload.get("soap_namespace") or "urn:MaNGOS"
commands = payload["commands"]
verbose = bool(payload.get("verbose"))

parsed = urllib.parse.urlparse(soap_url)
if parsed.scheme != "http":
    raise SystemExit("Only plain HTTP SOAP endpoints are supported by this helper.")
host = parsed.hostname or "127.0.0.1"
port = parsed.port or 7878
path = parsed.path or "/"


def call_soap(command):
    body = '''<?xml version="1.0" encoding="UTF-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"
                   xmlns:SOAP-ENC="http://schemas.xmlsoap.org/soap/encoding/"
                   xmlns:xsi="http://www.w3.org/1999/XMLSchema-instance"
                   xmlns:xsd="http://www.w3.org/1999/XMLSchema"
                   xmlns:ns1="{soap_namespace}">
  <SOAP-ENV:Body>
    <ns1:executeCommand>
      <command>{command}</command>
    </ns1:executeCommand>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>'''.format(
        soap_namespace=soap_namespace.replace("&", "&amp;").replace('"', "&quot;"),
        command=command.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    )
    auth = base64.b64encode((soap_user + ":" + soap_password).encode("utf-8")).decode("ascii")
    headers = {
        "Authorization": "Basic " + auth,
        "Content-Type": "text/xml; charset=utf-8",
        "SOAPAction": "executeCommand",
    }
    conn = http.client.HTTPConnection(host, port, timeout=10)
    try:
        conn.request("POST", path, body.encode("utf-8"), headers)
        response = conn.getresponse()
        raw = response.read().decode("utf-8", errors="replace")
    finally:
        conn.close()

    if response.status >= 400:
        raise RuntimeError("SOAP HTTP {0}: {1}".format(response.status, raw[:500]))

    try:
        root = ET.fromstring(raw)
    except ET.ParseError:
        raise RuntimeError("SOAP returned non-XML response: " + raw[:500])

    fault = root.find(".//{http://schemas.xmlsoap.org/soap/envelope/}Fault")
    if fault is not None:
        text = "".join(fault.itertext()).strip()
        raise RuntimeError("SOAP fault: " + text)

    result = ""
    for element in root.iter():
        if element.tag.endswith("result") or element.tag.endswith("return"):
            result = element.text or ""
            break
    return result.strip()


for command in commands:
    if verbose:
        shown = command
        if command.lower().startswith("account create "):
            parts = command.split(" ", 3)
            if len(parts) == 4:
                shown = " ".join(parts[:3] + ["********"])
        print("SOAP:", shown)
    try:
        result = call_soap(command)
    except RuntimeError as exc:
        lowered_error = str(exc).lower()
        if command.lower().startswith("account create ") and (
            "already exist" in lowered_error or "already exists" in lowered_error
        ):
            print("Account already exists; continuing.")
            continue
        raise
    lowered = result.lower()
    failed_markers = (" syntax", "error", "not exist", "not found", "incorrect", "already exist", "already exists")
    if any(marker in lowered for marker in failed_markers):
        raise RuntimeError("Command failed: " + result)
    if result:
        print(result)
"""


def load_env(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        raise SystemExit(f"Missing env file: {path}")

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def require_env(env: dict[str, str], key: str) -> str:
    value = env.get(key, "").strip()
    if not value:
        raise SystemExit(f"{key} must be set in .env")
    return value


def env_value(env: dict[str, str], prefixes: tuple[str, ...], suffix: str, fallback: str = "") -> str:
    for prefix in prefixes:
        value = env.get(f"{prefix}_{suffix}", "").strip()
        if value:
            return value
    return fallback


def validate_account_username(username: str) -> str:
    username = username.strip()
    if not USERNAME_RE.match(username):
        raise SystemExit(
            "Account username must be 3-32 characters and use only letters, numbers, dot, underscore, or dash."
        )
    return username.upper()


def validate_account_password(password: str) -> str:
    if not password:
        raise SystemExit("Account password cannot be empty.")
    if any(ch.isspace() for ch in password):
        raise SystemExit("Account password cannot contain whitespace because account commands are space-delimited.")
    if len(password) > 64:
        raise SystemExit("Account password is too long for this helper; use 64 characters or fewer.")
    return password


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a WoW account through SSH + server-local SOAP.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("username", help="WoW account username to create")
    parser.add_argument("--password", help="WoW account password. If omitted, prompt securely.")
    parser.add_argument("--server-type", choices=sorted(SERVER_PROFILES), default="cmangos")
    parser.add_argument("--expansion", type=int, choices=(0, 1, 2), help="0=Classic, 1=TBC, 2=WotLK")
    parser.add_argument("--gmlevel", type=int, default=1, choices=range(0, 8), help="GM security level; use 0 for a regular player account")
    parser.add_argument("--realm-id", type=int, default=-1, help="Realm ID for gmlevel assignment (-1 = all realms)")
    parser.add_argument("--env", type=pathlib.Path, default=DEFAULT_ENV, help="Path to .env")
    parser.add_argument("--soap-url", default="", help="Override SOAP URL on the server")
    parser.add_argument("--soap-user", default="", help="SOAP admin username. Defaults to MANGOS_SOAP_USERNAME.")
    parser.add_argument("--soap-password", default="", help="SOAP admin password. Defaults to MANGOS_SOAP_PASSWORD or prompt.")
    parser.add_argument("--skip-create", action="store_true", help="Only run the expansion command for an existing account.")
    parser.add_argument("--dry-run", action="store_true", help="Print the remote commands without connecting.")
    parser.add_argument("--verbose", action="store_true", help="Print SOAP commands before executing them on the server.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    env = load_env(args.env)
    profile = SERVER_PROFILES[args.server_type]
    env_prefixes = profile["env_prefixes"]
    expansion = args.expansion if args.expansion is not None else int(profile["default_expansion"])

    account_name = validate_account_username(args.username)
    account_password = validate_account_password(args.password or getpass.getpass("New WoW account password: "))

    soap_user_prefixes = env_prefixes if args.server_type == "cmangos" else tuple(prefix for prefix in env_prefixes if prefix != "MANGOS")
    soap_user = args.soap_user or env_value(env, soap_user_prefixes, "SOAP_USERNAME") or str(profile.get("default_soap_username", ""))
    if not soap_user:
        soap_user = input("SOAP admin username: ").strip()
    if not soap_user:
        raise SystemExit("SOAP admin username is required.")

    soap_password = args.soap_password or env_value(env, env_prefixes, "SOAP_PASSWORD")
    if not soap_password:
        soap_password = getpass.getpass("SOAP admin password: ")
    if not soap_password:
        raise SystemExit("SOAP admin password is required.")
    max_soap_password_len = int(profile.get("max_soap_password_len", 0) or 0)
    if max_soap_password_len > 0 and not args.soap_password:
        soap_password = soap_password[:max_soap_password_len]

    ssh_host = env_value(env, env_prefixes, "HOST")
    if not ssh_host:
        raise SystemExit(f"{env_prefixes[0]}_HOST must be set in .env")
    ssh_port = env_value(env, env_prefixes, "PORT", "22") or "22"
    ssh_user = env_value(env, env_prefixes, "USER")
    if not ssh_user:
        raise SystemExit(f"{env_prefixes[0]}_USER must be set in .env")
    ssh_key = env_value(env, env_prefixes, "SSH_KEY_PATH")
    if not ssh_key:
        raise SystemExit(f"{env_prefixes[0]}_SSH_KEY_PATH must be set in .env")
    soap_url_prefixes = env_prefixes if args.server_type == "cmangos" else tuple(prefix for prefix in env_prefixes if prefix != "MANGOS")
    soap_url = args.soap_url or env_value(env, soap_url_prefixes, "SOAP_URL", str(profile["default_soap_url"]))

    commands = []
    if not args.skip_create:
        commands.append(f"account create {account_name} {account_password}")
    commands.append(f"account set addon {account_name} {expansion}")
    if args.gmlevel > 0:
        commands.append(f"account set gmlevel {account_name} {args.gmlevel} {args.realm_id}")

    payload: dict[str, Any] = {
        "soap_url": soap_url,
        "soap_user": soap_user,
        "soap_password": soap_password,
        "soap_namespace": profile["soap_namespace"],
        "commands": commands,
        "verbose": args.verbose,
    }

    if args.dry_run:
        print(f"SSH target: {ssh_user}@{ssh_host}:{ssh_port}")
        print(f"Server type: {profile['label']}")
        print(f"SOAP URL on server: {soap_url}")
        print(f"SOAP namespace: {profile['soap_namespace']}")
        for command in commands:
            scrubbed = command.replace(account_password, "********")
            print(f"Would run: {scrubbed}")
        return 0

    ssh_command = [
        "ssh",
        "-i",
        ssh_key,
        "-p",
        ssh_port,
        "-o",
        "StrictHostKeyChecking=accept-new",
        f"{ssh_user}@{ssh_host}",
        "python3 -c " + shlex.quote(REMOTE_SCRIPT),
    ]

    completed = subprocess.run(
        ssh_command,
        input=json.dumps(payload),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    if completed.stdout:
        print(completed.stdout.strip())
    if completed.returncode != 0:
        if completed.stderr:
            print(completed.stderr.strip(), file=sys.stderr)
        return completed.returncode

    parts = [f"Created/updated {profile['label']} account {account_name} with expansion {expansion}"]
    if args.gmlevel > 0:
        parts.append(f"GM level set to {args.gmlevel} (realm {args.realm_id})")
    print(".".join(parts) + ".")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
