#!/usr/bin/env python3
"""Create CMaNGOS accounts through the built-in SOAP command endpoint."""

from __future__ import annotations

import argparse
import base64
import json
import sys
import urllib.error
import urllib.request
import defusedxml.ElementTree as ET  # SOAP response is server-controlled, but parse it safely regardless
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


SOAP_NS = "urn:MaNGOS"
SOAP_ENV = "http://schemas.xmlsoap.org/soap/envelope/"


def soap_execute_command(soap_url: str, admin_user: str, admin_pass: str, command: str, timeout: float = 15.0) -> str:
    if not soap_url.startswith(("http://", "https://")):
        raise ValueError(f"refusing non-http(s) URL: {soap_url}")
    envelope = f"""<?xml version="1.0" encoding="UTF-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="{SOAP_ENV}" xmlns:ns1="{SOAP_NS}">
  <SOAP-ENV:Body>
    <ns1:executeCommand>
      <command>{xml_escape(command)}</command>
    </ns1:executeCommand>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>
"""
    auth = base64.b64encode(f"{admin_user}:{admin_pass}".encode("utf-8")).decode("ascii")
    req = urllib.request.Request(
        soap_url,
        data=envelope.encode("utf-8"),
        headers={
            "Authorization": f"Basic {auth}",
            "Content-Type": 'text/xml; charset="utf-8"',
            "SOAPAction": "executeCommand",
        },
        method="POST",
    )

    try:
        # soap_url is validated to http(s) above and comes from an operator-supplied --soap-url flag, not attacker-controlled input.
        with urllib.request.urlopen(req, timeout=timeout) as resp:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
            body = resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"SOAP HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"SOAP request failed: {exc}") from exc

    return parse_soap_result(body)


def xml_escape(value: str) -> str:
    return (
        value.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&apos;")
    )


def parse_soap_result(body: str) -> str:
    try:
        root = ET.fromstring(body)
    except ET.ParseError:
        return body.strip()

    for elem in root.iter():
        if elem.tag.endswith("result"):
            return (elem.text or "").strip()
    return body.strip()


def create_account(args: argparse.Namespace) -> int:
    validate_command_arg(args.account, "account")
    validate_command_arg(args.password, "password")
    expansion = f" {args.expansion}" if args.expansion is not None else ""
    command = f"account create {args.account} {args.password}{expansion}"
    result = soap_execute_command(args.soap_url, args.admin_user, args.admin_pass, command, args.timeout)
    print(result or "Account created.")

    if args.gmlevel > 0:
        gm_cmd = f"account set gmlevel {args.account} {args.gmlevel} {args.realm_id}"
        gm_result = soap_execute_command(args.soap_url, args.admin_user, args.admin_pass, gm_cmd, args.timeout)
        print(gm_result or f"GM level set to {args.gmlevel}.")

    return 0


class AccountService(BaseHTTPRequestHandler):
    server_version = "WoWeeProvisioning/1.0"

    def do_POST(self) -> None:
        if self.path != "/accounts":
            self.send_json(404, {"ok": False, "error": "unknown endpoint"})
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            token = self.server.provision_token  # type: ignore[attr-defined]
            if payload.get("token") != token and self.headers.get("Authorization") != f"Bearer {token}":
                self.send_json(401, {"ok": False, "error": "invalid token"})
                return

            account = require_string(payload, "account")
            password = require_string(payload, "password")
            validate_command_arg(account, "account")
            validate_command_arg(password, "password")
            expansion = payload.get("expansion")
            command = f"account create {account} {password}"
            if expansion is not None:
                command += f" {int(expansion)}"

            result = soap_execute_command(
                self.server.soap_url,  # type: ignore[attr-defined]
                self.server.admin_user,  # type: ignore[attr-defined]
                self.server.admin_pass,  # type: ignore[attr-defined]
                command,
                self.server.soap_timeout,  # type: ignore[attr-defined]
            )
            self.send_json(200, {"ok": True, "result": result})
        except Exception as exc:
            self.send_json(400, {"ok": False, "error": str(exc)})

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"{self.address_string()} - {fmt % args}", file=sys.stderr)

    def send_json(self, status: int, doc: dict[str, Any]) -> None:
        data = json.dumps(doc).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def require_string(doc: dict[str, Any], key: str) -> str:
    value = doc.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{key} is required")
    return value.strip()


def validate_command_arg(value: str, label: str) -> None:
    if any(ch.isspace() or ord(ch) < 32 for ch in value):
        raise ValueError(f"{label} must not contain whitespace or control characters")


def serve(args: argparse.Namespace) -> int:
    if not args.token:
        raise SystemExit("--token is required for service mode")

    httpd = ThreadingHTTPServer((args.bind, args.port), AccountService)
    httpd.soap_url = args.soap_url  # type: ignore[attr-defined]
    httpd.admin_user = args.admin_user  # type: ignore[attr-defined]
    httpd.admin_pass = args.admin_pass  # type: ignore[attr-defined]
    httpd.soap_timeout = args.timeout  # type: ignore[attr-defined]
    httpd.provision_token = args.token  # type: ignore[attr-defined]

    print(f"CMaNGOS account provisioning service listening on http://{args.bind}:{args.port}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping account provisioning service")
    return 0


def add_common_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--soap-url", required=True, help="CMaNGOS SOAP URL, for example http://127.0.0.1:7878/")
    parser.add_argument("--admin-user", required=True, help="GM account with SOAP command access")
    parser.add_argument("--admin-pass", required=True, help="GM account password")
    parser.add_argument("--timeout", type=float, default=15.0, help="SOAP timeout in seconds")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    create = sub.add_parser("create", help="Create one account through CMaNGOS SOAP")
    add_common_args(create)
    create.add_argument("--account", required=True)
    create.add_argument("--password", required=True)
    create.add_argument("--expansion", type=int, help="Optional CMaNGOS expansion value, such as 1 for TBC")
    create.add_argument("--gmlevel", type=int, default=0, choices=(0, 1, 2, 3, 4), help="GM security level (1+ enables .bot add)")
    create.add_argument("--realm-id", type=int, default=-1, help="Realm ID for gmlevel assignment (-1 = all realms)")
    create.set_defaults(func=create_account)

    service = sub.add_parser("serve", help="Expose POST /accounts as a small provisioning service")
    add_common_args(service)
    service.add_argument("--bind", default="127.0.0.1")
    service.add_argument("--port", type=int, default=8790)
    service.add_argument("--token", required=True)
    service.set_defaults(func=serve)

    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
