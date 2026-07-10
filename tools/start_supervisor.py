#!/usr/bin/env python3
"""Start supervisor in background, detach so the shell tool won't kill children."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
cmd = [
    sys.executable,
    str(ROOT / "tools/bot_fleet_manager/bot_fleet_manager.py"),
    str(ROOT / "tools/bot_fleet_manager/fleet.settings.json"),
    "supervise",
]

log = open(ROOT / "build/supervisor_out.log", "w")
proc = subprocess.Popen(
    cmd,
    cwd=str(ROOT),
    stdout=log,
    stderr=log,
    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS,
)
print(f"Supervisor PID: {proc.pid}")
sys.exit(0)