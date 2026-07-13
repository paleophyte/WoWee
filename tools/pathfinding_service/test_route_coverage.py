#!/usr/bin/env python3
"""Test bbox route coverage across landmarks, continents, and terrain types.

Usage:
    python tools/pathfinding_service/test_route_coverage.py <pathfinding-base-url>
"""

import json
import sys
import urllib.error
import urllib.request
import time

LANDMARKS: dict[str, dict] = {
    "goldshire":          {"mapId": 0,   "x": 63.52,     "y": -9480.09,  "z": 56.18,   "zone": "Elwynn"},
    "stormwind":          {"mapId": 0,   "x": 364.06,    "y": -9153.77,  "z": 90.48,   "zone": "Stormwind"},
    "dwarven-district":   {"mapId": 0,   "x": 514.03,    "y": -8346.46,  "z": 92.02,   "zone": "Stormwind/Dwarven"},
    "kharanos":           {"mapId": 0,   "x": -482.13,   "y": -5585.95,  "z": 397.02,  "zone": "Dun Morogh"},
    "ironforge":          {"mapId": 0,   "x": -834.06,   "y": -5021.00,  "z": 495.32,  "zone": "Ironforge"},
    "thelsamar":          {"mapId": 0,   "x": -1704.45,  "y": -5379.21,  "z": 368.82,  "zone": "Loch Modan"},
    "menethil":           {"mapId": 0,   "x": -3703.86,  "y": -4875.27,  "z": 37.82,   "zone": "Wetlands"},
    "southshore":         {"mapId": 0,   "x": -770.09,   "y": -3581.72,  "z": 43.60,   "zone": "Hillsbrad"},
    "tarren-mill":        {"mapId": 0,   "x": -20.43,    "y": -3282.22,  "z": 96.25,   "zone": "Hillsbrad"},
    "undercity":          {"mapId": 0,   "x": 1584.07,   "y": -2209.64,  "z": 90.73,   "zone": "Tirisfal"},
    "brill":              {"mapId": 0,   "x": 1110.07,   "y": -2868.17,  "z": 91.42,   "zone": "Tirisfal"},
    "lakeshire":          {"mapId": 0,   "x": -2277.58,  "y": -11338.74, "z": 31.47,   "zone": "Redridge"},
    "darkshire":          {"mapId": 0,   "x": -10229.62, "y": -10601.33, "z": 31.33,   "zone": "Duskwood"},
    "westfall":           {"mapId": 0,   "x": -11481.84, "y": -8777.82,  "z": 9.72,    "zone": "Westfall"},
    "sentinel-hill":      {"mapId": 0,   "x": -10455.47, "y": -8323.00,  "z": 33.54,   "zone": "Westfall"},
    "booty-bay":          {"mapId": 0,   "x": -14421.82, "y": -1409.11,  "z": 2.90,    "zone": "Stranglethorn"},
    "orggrimmar":         {"mapId": 1,   "x": -3313.42,  "y": -4409.50,  "z": 97.82,   "zone": "Orgrimmar/Durotar"},
    "crossroads":         {"mapId": 1,   "x": -2652.15,  "y": -455.90,   "z": 95.59,   "zone": "Barrens"},
    "ratchet":            {"mapId": 1,   "x": -3680.07,  "y": -951.36,   "z": 8.04,    "zone": "Barrens"},
    "thunder-bluff":       {"mapId": 1,   "x": -4225.87,  "y": 729.20,    "z": 134.49,  "zone": "Mulgore"},
    "exodar":             {"mapId": 530, "x": -11873.60, "y": -4007.31,  "z": -0.55,   "zone": "Azuremyst"},
    "azure-watch":        {"mapId": 530, "x": -12501.5,  "y": -4179.47,  "z": 50.06,   "zone": "Azuremyst"},
    "hellfire-pen":       {"mapId": 530, "x": -1262.26, "y": -1777.55,  "z": 69.79,   "zone": "Hellfire"},
    "zangarmarsh":         {"mapId": 530, "x": 3221.43,  "y": -1065.20,  "z": 5.06,    "zone": "Zangarmarsh"},
    "nagrand":            {"mapId": 530, "x": 5236.64,  "y": -757.88,   "z": 177.65,  "zone": "Nagrand"},
    "terokkar":           {"mapId": 530, "x": -3965.57, "y": 727.16,    "z": 33.56,   "zone": "Terokkar"},
    "shadowmoon":         {"mapId": 530, "x": -4526.35, "y": 3359.72,   "z": 10.52,   "zone": "Shadowmoon"},
}


def test_route(base: str, start_name: str, end_name: str) -> dict:
    start = LANDMARKS[start_name]
    end = LANDMARKS[end_name]
    if start["mapId"] != end["mapId"]:
        return {"status": "SKIP", "reason": "different maps"}

    payload = {
        "mapId": start["mapId"],
        "start": {"x": start["x"], "y": start["y"], "z": start["z"]},
        "end": {"x": end["x"], "y": end["y"], "z": end["z"]},
        "coordinateSpace": "wowee-canonical",
        "outputSpace": "wowee-canonical",
        "arrivalRadius": 5.0,
        "maxLegs": 4,
        "minProgressYards": 15.0,
        "tileMode": "bbox",
        "tileMargin": 2,
    }
    url = base.rstrip("/") + "/route"
    if not url.startswith(("http://", "https://")):
        raise ValueError(f"refusing non-http(s) URL: {url}")
    t0 = time.monotonic()
    try:
        req = urllib.request.Request(url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
        # url is validated to http(s) above; this calls our own local pathfinding service, not attacker-controlled input.
        with urllib.request.urlopen(req, timeout=30) as resp:  # nosemgrep: python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected
            result = json.loads(resp.read().decode())
        elapsed = time.monotonic() - t0
        if not result.get("ok"):
            return {"status": "FAIL", "error": result.get("error", "unknown"), "elapsed": round(elapsed, 2)}
        status = result.get("routeStatus", "unknown")
        legs = result.get("legs", [])
        total_waypoints = sum(leg.get("waypointCount", 0) for leg in legs)
        total_progress = sum(float(leg.get("progressYards", 0)) for leg in legs)
        return {
            "status": "OK" if status == "complete" else "PARTIAL",
            "routeStatus": status,
            "legs": len(legs),
            "waypoints": total_waypoints,
            "progressYards": round(total_progress, 1),
            "loadedTiles": result.get("loadedTiles", 0),
            "elapsed": round(elapsed, 1),
        }
    except urllib.error.HTTPError as exc:
        return {"status": "HTTP", "code": exc.code, "elapsed": round(time.monotonic() - t0, 1)}
    except (OSError, urllib.error.URLError, TimeoutError) as exc:
        return {"status": "NET", "error": str(exc), "elapsed": round(time.monotonic() - t0, 1)}
    except json.JSONDecodeError as exc:
        return {"status": "JSON", "error": str(exc), "elapsed": round(time.monotonic() - t0, 1)}


def main():
    if len(sys.argv) < 2:
        print("Usage: test_route_coverage.py <pathfinding-base-url>")
        return 1
    base = sys.argv[1]

    scenarios = [
        ("short city",         "stormwind",      "goldshire"),
        ("short city",         "stormwind",      "dwarven-district"),
        ("medium zone",       "goldshire",      "lakeshire"),
        ("medium zone",        "goldshire",      "darkshire"),
        ("medium zone",        "goldshire",      "westfall"),
        ("long cross-zone",   "stormwind",      "kharanos"),
        ("long cross-zone",   "stormwind",      "ironforge"),
        ("long cross-zone",   "ironforge",      "thelsamar"),
        ("long cross-zone",   "thelsamar",      "menethil"),
        ("long cross-zone",   "stormwind",      "menethil"),
        ("long cross-zone",   "goldshire",      "menethil"),
        ("long cross-zone",   "menethil",       "southshore"),
        ("long cross-zone",   "southshore",     "tarren-mill"),
        ("long cross-zone",   "tarren-mill",    "undercity"),
        ("long cross-zone",   "brill",          "undercity"),
        ("long cross-zone",    "stormwind",      "southshore"),
        ("long inland",       "booty-bay",      "undercity"),
        ("kalimdor",          "orggrimmar",     "crossroads"),
        ("kalimdor",          "crossroads",     "ratchet"),
        ("kalimdor",          "crossroads",     "thunder-bluff"),
        ("kalimdor",          "orggrimmar",     "thunder-bluff"),
        ("kalimdor",          "orggrimmar",     "ratchet"),
        ("outland short",    "exodar",         "azure-watch"),
        ("outland cross",    "hellfire-pen",  "zangarmarsh"),
        ("outland cross",     "hellfire-pen",  "nagrand"),
        ("outland cross",    "hellfire-pen",   "terokkar"),
        ("outland long",     "hellfire-pen",   "shadowmoon"),
        ("outland long",    "exodar",         "hellfire-pen"),
        ("outland long",     "exodar",          "shadowmoon"),
    ]

    print(f"Testing {len(scenarios)} route pairs against {base}")
    print(f"{'Category':<22} {'From':<18} {'To':<18} {'Result':<8} {'Legs':<5} {'WPs':<5} {'Tiles':<5} {'Time':<6} Detail")
    print("-" * 110)

    results = {"OK": 0, "PARTIAL": 0, "FAIL": 0, "SKIP": 0, "NET": 0, "HTTP": 0}
    for cat, start_name, end_name in scenarios:
        if start_name not in LANDMARKS or end_name not in LANDMARKS:
            print(f"{'MISSING':<22} {start_name:<18} {end_name:<18}")
            results["SKIP"] = results.get("SKIP", 0) + 1
            continue
        r = test_route(base, start_name, end_name)
        s = r["status"]
        elapsed = r.get("elapsed", "")
        detail = ""
        if s == "OK":
            detail = f"progress={r.get('progressYards')}y"
        elif s == "PARTIAL":
            detail = f"routeStatus={r.get('routeStatus')} progress={r.get('progressYards')}y"
        elif s == "FAIL":
            detail = r.get("error", "")
        elif s == "NET":
            detail = r.get("error", "")
        elif s == "HTTP":
            detail = str(r.get("code", ""))
        elif s == "SKIP":
            detail = r.get("reason", "")
        print(
            f"{cat:<22} {start_name:<18} {end_name:<18} "
            f"{s:<10} {str(r.get('legs', '')):<5} {str(r.get('waypoints', '')):<5} "
            f"{str(r.get('loadedTiles', '')):<5} {str(elapsed):<6} {detail}"
        )
        results[s] = results.get(s, 0) + 1

    total = sum(results.values())
    print(f"\n{'='*110}")
    print(f"Total: {total}  OK={results.get('OK', 0)}  PARTIAL={results.get('PARTIAL', 0)}  "
          f"FAIL={results.get('FAIL', 0)}  SKIP={results.get('SKIP', 0)}  "
          f"NET={results.get('NET', 0)}  HTTP={results.get('HTTP', 0)}")
    return 0 if results.get("FAIL", 0) == 0 and results.get("NET", 0) == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())