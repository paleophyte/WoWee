#!/usr/bin/env bash
# Offline real scene rendering. No login, account or gameplay operations.
set -uo pipefail
umask 077
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$root/build/bin/wowee_render_probe"
if [[ "$(uname -s)" != Darwin || $# -lt 1 || $# -gt 2 || ( $# == 2 && "$2" != --loading-screen ) || ! -x "$binary" || ! -f "${WOW_DATA_PATH:-}/expansions/tbc/manifest.json" ]]; then
    printf 'Build wowee_render_probe; set WOW_DATA_PATH to the data root containing expansions/tbc; run %s NEW_OUTPUT_DIRECTORY [--loading-screen]\n' "$0" >&2
    exit 2
fi
probe_mode=scene_terrain
if [[ "${2:-}" == --loading-screen ]]; then probe_mode=scene_loading; fi
mkdir "$1" || exit 2
output="$(cd "$1" && pwd)"
mkdir "$output/config" || exit 2
# Fresh local-only TBC selection. No credential, autologin or existing profile.
cat > "$output/config/login.cfg" <<'CONFIG'
version=3
active=127.0.0.1:9

[server 127.0.0.1:9]
expansion=tbc
assets=tbc
username=
CONFIG
python3 - "$output" "$WOW_DATA_PATH" <<'PY'
import json,sys,pathlib
output=pathlib.Path(sys.argv[1]);data=pathlib.Path(sys.argv[2])
(output/'isolated-render-probe.json').write_text(json.dumps({'config_root':str(output/'config'),'data_root':str(data.resolve())})+'\n')
PY
if [[ $? != 0 ]]; then exit 2; fi
git -C "$root" diff --binary > "$output/source.patch"
{
    date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
    sw_vers
    uname -m
    git -C "$root" rev-parse HEAD
    git -C "$root" status --short
    shasum -a 256 "$binary" "$WOW_DATA_PATH/expansions/tbc/manifest.json"
    brew list --versions molten-vk vulkan-loader
    printf 'WOW_DATA_PATH=%s\nVK_DRIVER_FILES=%s\n' "$WOW_DATA_PATH" "${VK_DRIVER_FILES:-<unset>}"
    otool -L "$binary"
    system_profiler SPDisplaysDataType
} > "$output/runtime.txt" 2>&1
shasum -a 256 "$root"/build/bin/assets/shaders/*.spv > "$output/shader-sha256.txt"
# Resource metadata is relative to repository root in this integration probe.
cd "$root" || exit 2
printf '{"scope":"isolated_tbc_qa","mode":"%s"}\n' "$probe_mode" | \
    WOWEE_CONFIG_ROOT="$output/config" WOWEE_WINDOW_HIDDEN=1 DYLD_PRINT_LIBRARIES=1 VK_LOADER_DEBUG=driver \
    "$binary" "$output/trace.jsonl" 2>&1 | tee "$output/probe.log"
results=("${PIPESTATUS[@]}")
printf '%s\n' "${results[1]}" > "$output/exit-status.txt"
if [[ "${results[1]}" != 0 ]]; then exit "${results[1]}"; fi
if [[ "${results[0]}" != 0 || "${results[2]}" != 0 ]]; then exit 1; fi
python3 - "$output/trace.jsonl" "$probe_mode" <<'PY'
import json,sys
rows=[json.loads(line) for line in open(sys.argv[1])]
assert rows[-1]['event']=='complete' and rows[-1]['state']=='pass'
assert len([r for r in rows if r['event']=='scene_loaded'])==4
if sys.argv[2]=='scene_loading':
    assert len([r for r in rows if r['event']=='loading_screen_presented'])==48
PY
