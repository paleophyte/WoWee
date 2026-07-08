#!/usr/bin/env python3
"""Small web dashboard for textual WoWee fleet visibility."""

from __future__ import annotations

import html
import json
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


def _request_json(url: str, timeout: float = 3.0) -> dict[str, Any]:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def _try_request_json(url: str) -> tuple[dict[str, Any] | None, str]:
    try:
        return _request_json(url), ""
    except (OSError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        return None, str(exc)


def _safe_get(doc: dict[str, Any], *path: str, default: Any = "") -> Any:
    value: Any = doc
    for key in path:
        if not isinstance(value, dict):
            return default
        value = value.get(key, default)
    return value


def _summarize_activity(status: dict[str, Any]) -> str:
    movement = status.get("movement", {})
    movement_state = str(movement.get("state") or "idle")
    session_state = str(status.get("status") or "unknown")
    if movement_state == "moving":
        index = movement.get("waypointIndex", 0)
        count = movement.get("waypointCount", 0)
        target = movement.get("target", {})
        return f"moving to {target.get('x', '?')}, {target.get('y', '?')}, {target.get('z', '?')} ({index}/{count})"
    if movement.get("error"):
        return f"{movement_state}: {movement.get('error')}"
    combat = status.get("combat", {})
    if combat.get("inCombat"):
        return "in combat"
    health = status.get("health", {})
    if health.get("isDead") or health.get("isPlayerDead"):
        return "dead"
    return session_state


def _team_state(status: dict[str, Any] | None) -> str:
    if not status:
        return "offline"
    if status.get("inWorld"):
        return "in_world"
    return str(status.get("status") or "connecting")


def collect_team_status(api_bases: list[dict[str, str]]) -> dict[str, Any]:
    teams: list[dict[str, Any]] = []
    collected_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    for item in api_bases:
        leader_id = item["id"]
        base = item["base"].rstrip("/")
        team: dict[str, Any] = {
            "id": leader_id,
            "fleet": item.get("fleet", ""),
            "apiBase": base,
            "apiReachable": False,
            "state": "offline",
            "activity": "unavailable",
            "endpointErrors": {},
        }
        status, status_error = _try_request_json(base + "/status")
        if status is None:
            team["error"] = status_error
            teams.append(team)
            continue

        team.update({
            "apiReachable": True,
            "state": _team_state(status),
            "status": status,
            "activity": _summarize_activity(status),
        })

        endpoint_errors: dict[str, str] = {}
        world, world_error = _try_request_json(base + "/world/self")
        if world is not None:
            team["world"] = world
        elif world_error:
            endpoint_errors["world"] = world_error

        party, party_error = _try_request_json(base + "/party")
        if party is not None:
            team["party"] = party
        elif party_error:
            endpoint_errors["party"] = party_error

        chat, chat_error = _try_request_json(base + "/chat?after=0&limit=8")
        if chat is not None:
            team["recentChat"] = chat.get("messages", [])[-8:]
        elif chat_error:
            endpoint_errors["chat"] = chat_error

        if endpoint_errors:
            team["endpointErrors"] = endpoint_errors
        teams.append(team)

    return {"collectedAt": collected_at, "teams": teams}


def render_dashboard(title: str) -> bytes:
    escaped_title = html.escape(title)
    page = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{escaped_title}</title>
  <style>
    :root {{
      color-scheme: light dark;
      font-family: Inter, Segoe UI, Arial, sans-serif;
      background: #111418;
      color: #edf0f2;
    }}
    body {{ margin: 0; }}
    header {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      padding: 18px 24px;
      border-bottom: 1px solid #30363d;
      background: #171b20;
    }}
    h1 {{ margin: 0; font-size: 20px; font-weight: 650; letter-spacing: 0; }}
    main {{ padding: 18px 24px 28px; }}
    .meta {{ color: #aab2bd; font-size: 13px; }}
    table {{ width: 100%; border-collapse: collapse; table-layout: fixed; }}
    th, td {{ padding: 10px 8px; border-bottom: 1px solid #2a3037; vertical-align: top; text-align: left; }}
    th {{ color: #b8c0cc; font-size: 12px; text-transform: uppercase; letter-spacing: .04em; }}
    td {{ font-size: 14px; }}
    .name {{ font-weight: 650; }}
    .ok {{ color: #85d996; }}
    .warn {{ color: #ffd27a; }}
    .bad {{ color: #ff9a8b; }}
    .muted {{ color: #aab2bd; }}
    .chat {{ display: grid; gap: 3px; max-height: 140px; overflow: auto; }}
    .chat div {{ white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
    code {{ color: #d3e5ff; }}
    @media (max-width: 900px) {{
      table, thead, tbody, tr, th, td {{ display: block; }}
      thead {{ display: none; }}
      tr {{ padding: 10px 0; border-bottom: 1px solid #2a3037; }}
      td {{ border-bottom: 0; padding: 5px 0; }}
      td::before {{ content: attr(data-label); display: block; color: #aab2bd; font-size: 11px; text-transform: uppercase; }}
    }}
  </style>
</head>
<body>
  <header>
    <h1>{escaped_title}</h1>
    <div class="meta" id="updated">loading...</div>
  </header>
  <main>
    <table>
      <thead>
        <tr>
          <th style="width: 16%">Team</th>
          <th style="width: 12%">Status</th>
          <th style="width: 20%">Location</th>
          <th style="width: 18%">Activity</th>
          <th style="width: 16%">Party</th>
          <th style="width: 18%">Recent Chat</th>
        </tr>
      </thead>
      <tbody id="teams"></tbody>
    </table>
  </main>
  <script>
    const esc = (value) => String(value ?? "").replace(/[&<>"']/g, c => ({{"&":"&amp;","<":"&lt;",">":"&gt;","\\"":"&quot;","'":"&#39;"}}[c]));
    function locationText(team) {{
      const world = team.world || {{}};
      const pos = world.position || {{}};
      if (!team.apiReachable) return team.error || "unavailable";
      if (team.endpointErrors && team.endpointErrors.world) return "world endpoint stale";
      if (team.state !== "in_world") return "not in world";
      return `map ${{esc(world.mapId)}} - x=${{Number(pos.x || 0).toFixed(1)}} y=${{Number(pos.y || 0).toFixed(1)}} z=${{Number(pos.z || 0).toFixed(1)}}`;
    }}
    function partyText(team) {{
      if (team.endpointErrors && team.endpointErrors.party) return "<span class='muted'>party endpoint stale</span>";
      const members = ((team.party || {{}}).members || []).map(m => esc(m.name || m.guid || "?"));
      return members.length ? members.join(", ") : "<span class='muted'>none reported</span>";
    }}
    function chatText(team) {{
      if (team.endpointErrors && team.endpointErrors.chat) return "<span class='muted'>chat endpoint stale</span>";
      const messages = team.recentChat || [];
      if (!messages.length) return "<span class='muted'>no recent chat</span>";
      return `<div class="chat">${{messages.map(m => `<div><code>${{esc(m.type)}}</code> ${{esc(m.from || "")}}: ${{esc(m.message || "")}}</div>`).join("")}}</div>`;
    }}
    function statusClass(team) {{
      if (!team.apiReachable || team.state === "offline") return "bad";
      if (team.state === "in_world") return "ok";
      return "warn";
    }}
    async function refresh() {{
      const res = await fetch("/api/teams", {{cache: "no-store"}});
      const data = await res.json();
      document.getElementById("updated").textContent = `updated ${{data.collectedAt}}`;
      document.getElementById("teams").innerHTML = data.teams.map(team => `
        <tr>
          <td data-label="Team"><div class="name">${{esc(team.id)}}</div><div class="muted">${{esc(team.fleet || "default")}}</div><div class="muted">${{esc(team.apiBase)}}</div></td>
          <td data-label="Status" class="${{statusClass(team)}}">${{esc(team.state || "offline")}}</td>
          <td data-label="Location">${{locationText(team)}}</td>
          <td data-label="Activity">${{esc(team.activity)}}</td>
          <td data-label="Party">${{partyText(team)}}</td>
          <td data-label="Recent Chat">${{chatText(team)}}</td>
        </tr>`).join("");
    }}
    refresh();
    setInterval(refresh, 3000);
  </script>
</body>
</html>"""
    return page.encode("utf-8")


def run_team_status_server(api_bases: list[dict[str, str]], host: str = "127.0.0.1", port: int = 8780) -> None:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: Any) -> None:
            return

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if self.path == "/" or self.path.startswith("/?"):
                self._send(200, render_dashboard("WoWee Team Status"), "text/html; charset=utf-8")
                return
            if self.path == "/api/teams":
                body = json.dumps(collect_team_status(api_bases)).encode("utf-8")
                self._send(200, body, "application/json")
                return
            self._send(404, b"not found", "text/plain; charset=utf-8")

    server = ThreadingHTTPServer((host, port), Handler)
    print(f"Team status dashboard listening on http://{host}:{port}")
    server.serve_forever()
