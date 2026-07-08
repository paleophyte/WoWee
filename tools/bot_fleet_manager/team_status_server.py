#!/usr/bin/env python3
"""Small web dashboard for textual WoWee fleet visibility."""

from __future__ import annotations

import html
import json
import os
import posixpath
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
MAP_DATA_PATH = ROOT / "map_data" / "zone_map_bounds.json"
RUNTIME_ZONE_ASSET_DIR = ROOT / "runtime" / "map_assets" / "zone"
DEFAULT_ZONE_ASSET_DIRS = [
    RUNTIME_ZONE_ASSET_DIR,
    Path(os.environ.get("WOWEE_MINIMANAGER_ZONE_DIR", "")) if os.environ.get("WOWEE_MINIMANAGER_ZONE_DIR") else None,
    Path(r"C:\Users\admin\code\wow_server\minimanager_remote\img\zone"),
]


def _load_zone_bounds() -> dict[str, dict[str, Any]]:
    try:
        with MAP_DATA_PATH.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError):
        return {}


ZONE_BOUNDS = _load_zone_bounds()


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


def _zone_score(x: float, y: float, bounds: dict[str, Any]) -> tuple[float, float]:
    min_x = min(float(bounds["top"]), float(bounds["bottom"]))
    max_x = max(float(bounds["top"]), float(bounds["bottom"]))
    min_y = min(float(bounds["left"]), float(bounds["right"]))
    max_y = max(float(bounds["left"]), float(bounds["right"]))
    area = max(1.0, (max_x - min_x) * (max_y - min_y))
    center_x = (min_x + max_x) / 2.0
    center_y = (min_y + max_y) / 2.0
    half_height = max(1.0, (max_x - min_x) / 2.0)
    half_width = max(1.0, (max_y - min_y) / 2.0)
    score = ((x - center_x) / half_height) ** 2 + ((y - center_y) / half_width) ** 2
    return score, area


def _zone_contains(map_id: int, x: float, y: float, bounds: dict[str, Any]) -> bool:
    if int(bounds.get("map_id", -1)) != map_id:
        return False
    min_x = min(float(bounds["top"]), float(bounds["bottom"]))
    max_x = max(float(bounds["top"]), float(bounds["bottom"]))
    min_y = min(float(bounds["left"]), float(bounds["right"]))
    max_y = max(float(bounds["left"]), float(bounds["right"]))
    return min_x <= x <= max_x and min_y <= y <= max_y


def _zone_for_position(map_id: int, x: float, y: float) -> tuple[str, dict[str, Any]] | None:
    best: tuple[str, dict[str, Any], float, float] | None = None
    for zone_id, bounds in ZONE_BOUNDS.items():
        if not _zone_contains(map_id, x, y, bounds):
            continue
        score, area = _zone_score(x, y, bounds)
        if best is None or score < best[2] or (score <= best[2] + 0.10 and area < best[3]):
            best = (zone_id, bounds, score, area)
    if best is None:
        return None
    return best[0], best[1]


def _map_zone_for_world(world: dict[str, Any]) -> dict[str, Any] | None:
    position = world.get("position", {})
    try:
        map_id = int(world.get("mapId", -1))
        x = float(position.get("x"))
        y = float(position.get("y"))
    except (TypeError, ValueError):
        return None

    explicit_zone = world.get("zoneId")
    if explicit_zone is not None:
        bounds = ZONE_BOUNDS.get(str(explicit_zone))
        if bounds and int(bounds.get("map_id", -1)) == map_id:
            return {"zoneId": int(explicit_zone), **bounds}

    found = _zone_for_position(map_id, x, y)
    if not found:
        return None
    zone_id, bounds = found
    return {"zoneId": int(zone_id), **bounds}


def _zone_asset_dirs() -> list[Path]:
    dirs: list[Path] = []
    for item in DEFAULT_ZONE_ASSET_DIRS:
        if item is None:
            continue
        try:
            path = item.expanduser().resolve()
        except OSError:
            continue
        if path.exists() and path.is_dir() and path not in dirs:
            dirs.append(path)
    return dirs


def _zone_asset_path(asset_name: str) -> Path | None:
    clean_name = posixpath.basename(asset_name)
    if clean_name != asset_name or not clean_name.lower().endswith(".png"):
        return None
    for directory in _zone_asset_dirs():
        candidate = directory / clean_name
        if candidate.exists() and candidate.is_file():
            return candidate
    return None


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
            map_zone = _map_zone_for_world(world)
            if map_zone is not None:
                asset = str(map_zone.get("asset", ""))
                team["mapZone"] = {
                    "zoneId": map_zone["zoneId"],
                    "mapId": int(map_zone["map_id"]),
                    "asset": asset,
                    "assetUrl": f"/map-assets/zone/{urllib.parse.quote(asset)}" if _zone_asset_path(asset) else "",
                    "left": float(map_zone["left"]),
                    "right": float(map_zone["right"]),
                    "top": float(map_zone["top"]),
                    "bottom": float(map_zone["bottom"]),
                    "assetWidth": int(map_zone["asset_width"]),
                    "assetHeight": int(map_zone["asset_height"]),
                }
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
    h2 {{ margin: 0; font-size: 14px; font-weight: 650; letter-spacing: 0; }}
    .meta {{ color: #aab2bd; font-size: 13px; }}
    .map-shell {{
      border: 1px solid #2a3037;
      border-radius: 6px;
      background: #15191e;
      margin-bottom: 18px;
      overflow: hidden;
    }}
    .map-head {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 10px 12px;
      border-bottom: 1px solid #2a3037;
    }}
    .map-wrap {{ position: relative; height: min(52vh, 520px); min-height: 280px; }}
    #leader-map {{ display: block; width: 100%; height: 100%; background: #0f1317; }}
    .map-tabs {{ display: flex; gap: 6px; flex-wrap: wrap; justify-content: flex-end; }}
    .map-tabs button {{
      border: 1px solid #3b4652;
      background: #1e252d;
      color: #dbe3ec;
      border-radius: 5px;
      padding: 5px 9px;
      font: inherit;
      font-size: 12px;
      cursor: pointer;
    }}
    .map-tabs button.active {{ background: #d3e5ff; color: #10151a; border-color: #d3e5ff; }}
    .map-legend {{
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      padding: 8px 12px 10px;
      border-top: 1px solid #2a3037;
      color: #c5ced8;
      font-size: 12px;
    }}
    .legend-item {{ display: inline-flex; align-items: center; gap: 6px; }}
    .dot {{ width: 9px; height: 9px; border-radius: 50%; display: inline-block; }}
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
    <section class="map-shell">
      <div class="map-head">
        <h2>Leader Map</h2>
        <div class="map-tabs" id="map-tabs"></div>
      </div>
      <div class="map-wrap">
        <canvas id="leader-map"></canvas>
      </div>
      <div class="map-legend" id="map-legend"></div>
    </section>
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
    let selectedMapId = "";
    const colors = ["#67d391", "#7db7ff", "#f2c166", "#ff8f8f", "#c49bff", "#58d6d1", "#f48bc2", "#a8d86d"];
    const zoneImages = new Map();
    function loadZoneImage(url) {{
      if (!url) return null;
      if (zoneImages.has(url)) return zoneImages.get(url);
      const image = new Image();
      image.onload = () => refresh();
      image.src = url;
      zoneImages.set(url, image);
      return image;
    }}
    function leaderPositions(teams) {{
      return teams.map((team, index) => {{
        const world = team.world || {{}};
        const pos = world.position || {{}};
        const zone = team.mapZone || null;
        const x = Number(pos.x);
        const y = Number(pos.y);
        const z = Number(pos.z);
        const mapId = String(world.mapId ?? "");
        if (!team.apiReachable || team.state !== "in_world" || !mapId || !Number.isFinite(x) || !Number.isFinite(y)) return null;
        const zoneId = zone ? String(zone.zoneId ?? "") : "";
        const assetUrl = zone ? String(zone.assetUrl || "") : "";
        return {{
          id: String(team.id || `leader-${{index + 1}}`),
          fleet: String(team.fleet || "default"),
          mapId,
          zoneId,
          groupId: zoneId && assetUrl ? `zone:${{zoneId}}` : `map:${{mapId}}`,
          groupLabel: zoneId && assetUrl ? `zone ${{zoneId}}` : `map ${{mapId}}`,
          zone,
          assetUrl,
          x,
          y,
          z: Number.isFinite(z) ? z : 0,
          color: colors[index % colors.length],
          activity: String(team.activity || "")
        }};
      }}).filter(Boolean);
    }}
    function groupedMaps(points) {{
      const groups = new Map();
      for (const point of points) {{
        if (!groups.has(point.groupId)) groups.set(point.groupId, {{ label: point.groupLabel, points: [] }});
        groups.get(point.groupId).points.push(point);
      }}
      return [...groups.entries()].sort((a, b) => Number(b[1].points.length) - Number(a[1].points.length) || a[1].label.localeCompare(b[1].label));
    }}
    function mapBounds(points) {{
      let minX = Math.min(...points.map(p => p.x));
      let maxX = Math.max(...points.map(p => p.x));
      let minY = Math.min(...points.map(p => p.y));
      let maxY = Math.max(...points.map(p => p.y));
      const spanX = Math.max(maxX - minX, 250);
      const spanY = Math.max(maxY - minY, 250);
      const padX = spanX * 0.35;
      const padY = spanY * 0.35;
      const midX = (minX + maxX) / 2;
      const midY = (minY + maxY) / 2;
      return {{
        minX: midX - spanX / 2 - padX,
        maxX: midX + spanX / 2 + padX,
        minY: midY - spanY / 2 - padY,
        maxY: midY + spanY / 2 + padY
      }};
    }}
    function drawMap(points, mapId) {{
      const canvas = document.getElementById("leader-map");
      const wrap = canvas.parentElement;
      const ctx = canvas.getContext("2d");
      const dpr = window.devicePixelRatio || 1;
      const width = Math.max(1, Math.floor(wrap.clientWidth));
      const height = Math.max(1, Math.floor(wrap.clientHeight));
      canvas.width = Math.floor(width * dpr);
      canvas.height = Math.floor(height * dpr);
      canvas.style.width = `${{width}}px`;
      canvas.style.height = `${{height}}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, width, height);
      ctx.fillStyle = "#0f1317";
      ctx.fillRect(0, 0, width, height);

      if (!points.length) {{
        ctx.fillStyle = "#aab2bd";
        ctx.font = "14px Inter, Segoe UI, Arial, sans-serif";
        ctx.textAlign = "center";
        ctx.fillText("No online leader positions", width / 2, height / 2);
        return;
      }}

      const zone = points.find(point => point.zone && point.assetUrl)?.zone || null;
      const zoneImage = zone ? loadZoneImage(String(zone.assetUrl || "")) : null;
      const hasZoneImage = zone && zoneImage && zoneImage.complete && zoneImage.naturalWidth > 0;
      const bounds = zone ? {{
        minX: Math.min(Number(zone.top), Number(zone.bottom)),
        maxX: Math.max(Number(zone.top), Number(zone.bottom)),
        minY: Math.min(Number(zone.left), Number(zone.right)),
        maxY: Math.max(Number(zone.left), Number(zone.right))
      }} : mapBounds(points);
      const toScreen = (point) => zone ? ({{
        x: (((point.y - Number(zone.left)) / (Number(zone.right) - Number(zone.left))) || 0) * width,
        y: (((point.x - Number(zone.top)) / (Number(zone.bottom) - Number(zone.top))) || 0) * height
      }}) : ({{
        x: ((point.y - bounds.minY) / (bounds.maxY - bounds.minY)) * width,
        y: ((bounds.maxX - point.x) / (bounds.maxX - bounds.minX)) * height
      }});

      if (hasZoneImage) {{
        ctx.drawImage(zoneImage, 0, 0, width, height);
        ctx.fillStyle = "rgba(15,19,23,0.22)";
        ctx.fillRect(0, 0, width, height);
      }}

      ctx.strokeStyle = "#202832";
      ctx.lineWidth = 1;
      const grid = 8;
      for (let i = 1; i < grid; i++) {{
        const gx = (width / grid) * i;
        const gy = (height / grid) * i;
        ctx.beginPath();
        ctx.moveTo(gx, 0);
        ctx.lineTo(gx, height);
        ctx.moveTo(0, gy);
        ctx.lineTo(width, gy);
        ctx.stroke();
      }}

      ctx.fillStyle = "#7f8a96";
      ctx.font = "12px Inter, Segoe UI, Arial, sans-serif";
      ctx.textAlign = "left";
      ctx.fillText(zone ? `${{points[0].groupLabel}} / map ${{points[0].mapId}} / ${{hasZoneImage ? "zone art" : "coordinate plot"}}` : `map ${{esc(mapId)}}`, 12, 20);
      ctx.textAlign = "right";
      ctx.fillText(`N ${{bounds.maxX.toFixed(0)}} / W ${{bounds.maxY.toFixed(0)}}`, width - 12, 20);
      ctx.fillText(`S ${{bounds.minX.toFixed(0)}} / E ${{bounds.minY.toFixed(0)}}`, width - 12, height - 12);

      for (const point of points) {{
        const p = toScreen(point);
        ctx.beginPath();
        ctx.arc(p.x, p.y, 9, 0, Math.PI * 2);
        ctx.fillStyle = point.color;
        ctx.fill();
        ctx.lineWidth = 2;
        ctx.strokeStyle = "#071014";
        ctx.stroke();
        ctx.fillStyle = "#edf0f2";
        ctx.font = "12px Inter, Segoe UI, Arial, sans-serif";
        ctx.textAlign = "left";
        ctx.fillText(point.id, p.x + 13, p.y - 2);
        ctx.fillStyle = "#aab2bd";
        ctx.fillText(`${{point.x.toFixed(1)}}, ${{point.y.toFixed(1)}}, ${{point.z.toFixed(1)}}`, p.x + 13, p.y + 13);
      }}
    }}
    function renderMap(data) {{
      const points = leaderPositions(data.teams || []);
      const groups = groupedMaps(points);
      const tabs = document.getElementById("map-tabs");
      if (!groups.length) {{
        selectedMapId = "";
        tabs.innerHTML = "";
        document.getElementById("map-legend").innerHTML = "<span class='muted'>No online leaders with positions</span>";
        drawMap([], "");
        return;
      }}
      if (!selectedMapId || !groups.some(([groupId]) => groupId === selectedMapId)) selectedMapId = groups[0][0];
      tabs.innerHTML = groups.map(([groupId, group]) => `<button type="button" class="${{groupId === selectedMapId ? "active" : ""}}" data-map="${{esc(groupId)}}">${{esc(group.label)}} (${{group.points.length}})</button>`).join("");
      for (const button of tabs.querySelectorAll("button")) {{
        button.onclick = () => {{
          selectedMapId = button.dataset.map || "";
          renderMap(data);
        }};
      }}
      const selected = groups.find(([groupId]) => groupId === selectedMapId)?.[1]?.points || [];
      document.getElementById("map-legend").innerHTML = selected.map(p => `<span class="legend-item"><span class="dot" style="background:${{p.color}}"></span>${{esc(p.id)}} <span class="muted">${{esc(p.fleet)}}</span></span>`).join("");
      drawMap(selected, selectedMapId);
    }}
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
      const messages = [...(team.recentChat || [])].reverse();
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
      renderMap(data);
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
    window.addEventListener("resize", refresh);
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
            if self.path.startswith("/map-assets/zone/"):
                asset_name = urllib.parse.unquote(self.path.removeprefix("/map-assets/zone/").split("?", 1)[0])
                asset_path = _zone_asset_path(asset_name)
                if asset_path is not None:
                    self._send(200, asset_path.read_bytes(), "image/png")
                    return
            self._send(404, b"not found", "text/plain; charset=utf-8")

    server = ThreadingHTTPServer((host, port), Handler)
    print(f"Team status dashboard listening on http://{host}:{port}")
    server.serve_forever()
