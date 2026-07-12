#!/usr/bin/env python3
#!/usr/bin/env python3
"""Live integration tests against a local CMaNGOS server and headless client.

Usage:
    python tools/integration_test.py          # provision, test APIs, cleanup
    python tools/integration_test.py --cleanup # remove test account only

Environment:
    Uses tools/.env for SSH/SOAP server credentials.
    Requires a built wowee_headless at build/bin/wowee_headless.exe.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
ENV_PATH = ROOT / "tools" / ".env"
SETTINGS_TEMPLATE = ROOT / "tools" / "headless_client" / "settings.example.json"
HEADLESS_BUILD = ROOT / "build" / "bin" / "wowee_headless.exe"

TEST_ACCOUNT = "WOWEE_TEST_BOT"
TEST_PASSWORD = "TestPass123!"
TEST_CHAR = "Weetest"

SSH_HOST = "10.102.172.4"
SSH_PORT = 22
SSH_USER = "mangos-helper"
SSH_KEY = "C:/Users/admin/.ssh/mangos_codex"
SOAP_USER = "SERVERADMIN"
SOAP_PASS = "server"

if ENV_PATH.exists():
    for line in ENV_PATH.read_text().splitlines():
        if "=" in line and not line.strip().startswith("#"):
            k, v = line.strip().split("=", 1)
            if k == "MANGOS_HOST":
                SSH_HOST = v
            elif k == "MANGOS_PORT":
                SSH_PORT = int(v)
            elif k == "MANGOS_USER":
                SSH_USER = v
            elif k == "MANGOS_SSH_KEY_PATH":
                SSH_KEY = v
            elif k == "MANGOS_SOAP_USERNAME":
                SOAP_USER = v
            elif k == "MANGOS_SOAP_PASSWORD":
                SOAP_PASS = v


def log(msg: str) -> None:
    print(f"  [{test_step}] {msg}", flush=True)


test_step = 0


def step(name: str) -> None:
    global test_step
    test_step += 1
    print(f"\n{'='*60}")
    print(f"Step {test_step}: {name}")
    print(f"{'='*60}")


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def http_get(url: str, timeout: float = 5.0) -> dict:
    if not url.startswith(("http://", "https://")):
        raise ValueError(f"refusing non-http(s) URL: {url}")
    try:
        # url is validated to http(s) above; this calls our own local test targets, not attacker-controlled input.
        with urllib.request.urlopen(url, timeout=timeout) as resp:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
            return json.loads(resp.read().decode())
    except (OSError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        fail(f"GET {url} failed: {exc}")


def http_post(url: str, data: dict, timeout: float = 5.0) -> dict:
    if not url.startswith(("http://", "https://")):
        raise ValueError(f"refusing non-http(s) URL: {url}")
    try:
        body = json.dumps(data).encode()
        req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
        # url is validated to http(s) above; this calls our own local test targets, not attacker-controlled input.
        with urllib.request.urlopen(req, timeout=timeout) as resp:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as exc:
        return json.loads(exc.read().decode()) if exc.code == 400 else {"error": str(exc)}
    except (OSError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        fail(f"POST {url} failed: {exc}")


SSH_SCRIPT = """
import base64, http.client, json, sys, urllib.parse, xml.etree.ElementTree as ET
payload = json.loads(sys.stdin.read())
soap_user, soap_password = payload["soap_user"], payload["soap_password"]
parsed = urllib.parse.urlparse(payload.get("soap_url", payload.get("soapUrl", "http://127.0.0.1:7878/")))
host = parsed.hostname or "127.0.0.1"
port = parsed.port or 7878
path = parsed.path or "/"
def call(cmd):
  body = '''<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="urn:MaNGOS"><SOAP-ENV:Body><ns1:executeCommand><command>{cmd}</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>'''
  auth = base64.b64encode((soap_user + ":" + soap_password).encode()).decode()
  conn = http.client.HTTPConnection(host, port, timeout=10)
  conn.request("POST", path, body.replace("{cmd}", cmd.replace("&","&amp;").replace("<","&lt;")), {"Authorization":"Basic "+auth,"Content-Type":"text/xml","SOAPAction":"executeCommand"})
  return conn.getresponse().read().decode("utf-8", errors="replace")
out = [call(cmd) for cmd in payload["commands"]]
print(json.dumps(out))
"""


def ssh_soap(commands: list[str]) -> list[str]:
    key_path = SSH_KEY.replace("\\", "/")
    payload = json.dumps({"soapUser": SOAP_USER, "soapPassword": SOAP_PASS, "commands": commands})
    ssh_cmd = [
        "ssh", "-i", key_path,
        "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=no",
        "-o", "ConnectTimeout=8",
        "-p", str(SSH_PORT),
        f"{SSH_USER}@{SSH_HOST}",
        "--", "python3", "-c", SSH_SCRIPT,
    ]
    try:
        proc = subprocess.run(
            ssh_cmd,
            input=payload,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=20,
        )
        if proc.returncode != 0:
            raise RuntimeError(f"SSH exit {proc.returncode}: {proc.stderr.strip()}")
        results = json.loads(proc.stdout)
        return [r.strip() for r in results]
    except subprocess.TimeoutExpired:
        return ["SSH timed out"]
    except (OSError, json.JSONDecodeError, RuntimeError) as exc:
        return [f"SSH error: {exc}"]


def soap_cmd(command: str) -> str:
    results = ssh_soap([command])
    return results[0] if results else "no output"


# ---------------------------------------------------------------------------
# Step 1: Cleanup previous test artifacts
# ---------------------------------------------------------------------------
step("Clean up any prior test account and runtime files")

soap_cmd(f"account delete {TEST_ACCOUNT}")
runtime_dir = ROOT / "tools" / "bot_fleet_manager" / "runtime"
if runtime_dir.exists():
    import stat as stat_mod
    def safe_unlink(p: Path) -> None:
        try:
            p.chmod(stat_mod.S_IWRITE)
            p.unlink()
        except Exception:
            pass
    for f in runtime_dir.glob("*test*"):
        if f.is_dir():
            safe_unlink(f)
        else:
            safe_unlink(f)
    route_state_dir = runtime_dir / "route_state"
    if route_state_dir.exists():
        for f in route_state_dir.glob("*test*"):
            safe_unlink(f)
print("  Cleanup done")


# ---------------------------------------------------------------------------
# Step 2: Create test account and character
# ---------------------------------------------------------------------------
step("Create test account via SOAP")

result = soap_cmd(f"account create {TEST_ACCOUNT} {TEST_PASSWORD}")
print(f"  Account create: {result}")
result = soap_cmd(f"account set addon {TEST_ACCOUNT} 1")
print(f"  Set addon: {result}")

step("Create test character via headless provisioning")

tmp_settings = ROOT / "build" / "test_char_settings.json"
if not SETTINGS_TEMPLATE.exists():
    fail(f"Settings template not found: {SETTINGS_TEMPLATE}")

settings = json.loads(SETTINGS_TEMPLATE.read_text())
settings["auth"]["account"] = TEST_ACCOUNT
settings["auth"]["password"] = TEST_PASSWORD
settings["character"]["name"] = TEST_CHAR
settings["provision"] = {
    "createCharacter": {
        "enabled": True,
        "name": TEST_CHAR,
        "race": "human",
        "class": "warrior",
        "gender": 0,
        "exitAfterCreate": True,
    }
}
tmp_settings.write_text(json.dumps(settings, indent=2))

if not HEADLESS_BUILD.exists():
    fail(f"Headless build not found: {HEADLESS_BUILD}")

env = os.environ.copy()
env["WOWEE_HEADLESS"] = "1"
proc = subprocess.Popen(
    [str(HEADLESS_BUILD), str(tmp_settings)],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    env=env,
)
try:
    stdout, _ = proc.communicate(timeout=60)
    print(f"  Provisioning exit code: {proc.returncode}")
    for line in stdout.splitlines()[-5:]:
        print(f"    {line}")
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
    fail("Provisioning timed out (60s)")

# Clean up tmp settings
tmp_settings.unlink(missing_ok=True)

# ---------------------------------------------------------------------------
# Step 3: Launch headless client in normal mode
# ---------------------------------------------------------------------------
step("Launch headless client with test character")

settings.pop("provision", None)
settings["api"]["port"] = 8789
settings["bots"]["enabled"] = True  # test that bot fields parse but don't require bots
settings["bots"]["names"] = ["TestBotOne"]

normal_settings = ROOT / "build" / "test-normal-settings.json"
settings.pop("provision", None)
normal_settings.write_text(json.dumps(settings, indent=2))

proc = subprocess.Popen(
    [str(HEADLESS_BUILD), str(normal_settings)],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    env=env,
)
API_BASE = "http://127.0.0.1:8787"

# Wait for in-world (up to 30s)
deadline = time.monotonic() + 30
entered = False
while time.monotonic() < deadline:
    try:
        status = http_get(f"{API_BASE}/status", timeout=3)
        if status.get("inWorld") and status.get("status") == "in_world":
            print(f"  Entered world after {30 - (deadline - time.monotonic()):.0f}s")
            entered = True
            break
    except SystemExit:
        raise
    except Exception:
        time.sleep(0.5)
if not entered:
    proc.kill()
    proc.wait()
    fail("Headless client did not enter world within 30s")


# ---------------------------------------------------------------------------
# Step 4: Test /status endpoint
# ---------------------------------------------------------------------------
step("Test GET /status")

status = http_get(f"{API_BASE}/status")
required_status_fields = ["inWorld", "status", "account", "character"]
for field in required_status_fields:
    assert field in status, f"/status missing field {field}"
assert status["inWorld"] is True, f"Expected inWorld=true, got {status['inWorld']}"
assert status["status"] == "in_world", f"Expected status=in_world, got {status['status']}"
assert status["character"]["name"] == TEST_CHAR, f"Expected character name {TEST_CHAR}"
movement = status.get("movement", {})
assert "active" in movement, "/status missing movement.active"
combat = status.get("combat", {})
assert "inCombat" in combat, "/status missing combat.inCombat"
health = status.get("health", {})
assert "isDead" in health, "/status missing health.isDead"
print("  OK")


# ---------------------------------------------------------------------------
# Step 5: Test /world/self endpoint
# ---------------------------------------------------------------------------
step("Test /world/self")

world = http_get(f"{API_BASE}/world/self")
for field in ("position", "mapId", "orientation", "movementFlags", "movementFlags2", "character"):
    assert field in world, f"/world/self missing field {field}"
pos = world["position"]
for axis in ("x", "y", "z"):
    assert axis in pos, f"/world/self.position missing {axis}"
assert isinstance(world["mapId"], int) or isinstance(world["mapId"], float), "mapId must be numeric"
assert isinstance(world["orientation"], (int, float)), "orientation must be numeric"
print(f"  OK: map={world['mapId']} pos=({pos['x']:.1f}, {pos['y']:.1f}, {pos['z']:.1f})")


# ---------------------------------------------------------------------------
# Step 6: Test /party endpoint
# ---------------------------------------------------------------------------
step("Test /party")

party = http_get(f"{API_BASE}/party")
assert "leader" in party, "/party missing leader GUID"
assert "members" in party, "/party missing members list"
assert isinstance(party["members"], list), "/party.members must be a list"
print(f"  OK: {len(party['members'])} party member(s)")


# ---------------------------------------------------------------------------
# Step 7: Test /chat endpoint
# ---------------------------------------------------------------------------
step("Test /chat")

chat = http_get(f"{API_BASE}/chat?after=0&limit=5")
assert "messages" in chat, "/chat missing messages"
assert isinstance(chat["messages"], list), "/chat.messages must be a list"
print(f"  OK: {len(chat['messages'])} message(s)")


# ---------------------------------------------------------------------------
if len(sys.argv) > 1 and sys.argv[1] == "--cleanup":
    step("Cleanup test account")
    print(f"  {soap_cmd(f'account delete {TEST_ACCOUNT}')}")
    sys.exit(0)

# Step 8: Test /commands endpoint
# ---------------------------------------------------------------------------
step("Test /commands")

cmd_result = http_post(f"{API_BASE}/commands", {"command": "/say Integration test!"})
assert isinstance(cmd_result, dict), "POST /commands must return a dict"
print(f"  OK: {cmd_result}")


# ---------------------------------------------------------------------------
# Step 9: Test /movement/goto and /movement/stop
# ---------------------------------------------------------------------------
step("Test /movement/goto and /movement/stop")

goto_result = http_post(f"{API_BASE}/movement/goto", {
    "mapId": 0, "x": 100.0, "y": -9500.0, "z": 50.0,
    "arrivalRadius": 5.0,
})
assert goto_result.get("ok"), f"/movement/goto failed: {goto_result}"
print("  Move command accepted")

# Let it run a moment, then stop
time.sleep(1)
stop_result = http_post(f"{API_BASE}/movement/stop", {"reason": "integration test"})
assert stop_result.get("ok") or stop_result.get("movement", {}).get("state") in ("idle", "stopped"), \
    f"/movement/stop failed: {stop_result}"
print("  Stop command accepted")


# ---------------------------------------------------------------------------
# Step 10: Verify movement state in /status after stop
# ---------------------------------------------------------------------------
step("Verify movement state after stop")

status2 = http_get(f"{API_BASE}/status")
mv = status2.get("movement", {})
assert not mv.get("active", True), "Movement should be inactive after stop"
assert mv.get("state") in ("idle", "stopped", "arrived", "failed"), \
    f"Unexpected movement state: {mv.get('state')}"
print(f"  OK: movement state={mv.get('state')}")


# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
print(f"\n{'='*60}")
print(f"All {test_step} steps passed!")

# Graceful shutdown
print("\nShutting down headless client...")
proc.terminate()
try:
    proc.wait(timeout=8)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

normal_settings.unlink(missing_ok=True)
print("Cleanup done.")