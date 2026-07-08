#!/usr/bin/env python3
"""Small demo client for a running wowee_headless leader API."""

from __future__ import annotations

import argparse
import json
import time
import urllib.request
from typing import Any


def request_json(method: str, url: str, payload: dict[str, Any] | None = None, timeout: float = 5.0) -> dict[str, Any]:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def print_json(label: str, doc: dict[str, Any]) -> None:
    print(f"\n== {label} ==")
    print(json.dumps(doc, indent=2, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:8787")
    parser.add_argument("--message", default="")
    parser.add_argument("--after", type=int, default=0)
    parser.add_argument("--wait-seconds", type=float, default=4.0)
    args = parser.parse_args()

    base = args.base_url.rstrip("/")
    print_json("status", request_json("GET", base + "/status"))
    print_json("self", request_json("GET", base + "/world/self"))
    print_json("party", request_json("GET", base + "/party"))

    if args.message:
        print_json("send party chat", request_json("POST", base + "/chat", {
            "type": "party",
            "message": args.message,
        }))
        time.sleep(max(0.0, args.wait_seconds))

    print_json("chat", request_json("GET", f"{base}/chat?after={args.after}&limit=50"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
