#!/usr/bin/env bash
# Prepare evidence or launch the ordinary client for a user-scoped Mac session.
# No scripted login, movement, character selection or server administration.
set -uo pipefail
umask 077
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$root/build/bin/wowee"
if [[ "$(uname -s)" != Darwin || $# != 2 || ( "$1" != --prepare-only && "$1" != --launch ) || ! -x "$binary" ]]; then
    printf 'Usage: bash %s --prepare-only|--launch NEW_OUTPUT_DIRECTORY\nBuild the normal wowee target first. --launch starts the ordinary client.\n' "$0" >&2
    exit 2
fi
mode="$1"
mkdir "$2" || exit 2
output="$(cd "$2" && pwd)"
started="$(date +%s)"
git -C "$root" diff --binary > "$output/source.patch"
{
    date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
    sw_vers
    uname -m
    git -C "$root" rev-parse HEAD
    git -C "$root" status --short
    shasum -a 256 "$binary"
    brew list --versions molten-vk vulkan-loader
    printf 'Capture log level: info\n'
    printf 'Mode=%s\nWOW_DATA_PATH=%s\nWOWEE_CONFIG_ROOT=%s\nVK_DRIVER_FILES=%s\nVK_ICD_FILENAMES=%s\n' \
        "$mode" "${WOW_DATA_PATH:-<normal-client-discovery>}" "${WOWEE_CONFIG_ROOT:-<normal-client-default>}" \
        "${VK_DRIVER_FILES:-<loader-discovery>}" "${VK_ICD_FILENAMES:-<unset>}"
    for manifest in "${VK_DRIVER_FILES:-}" "${VK_ICD_FILENAMES:-}"; do
        if [[ -n "$manifest" && -f "$manifest" ]]; then cat "$manifest"; fi
    done
    otool -L "$binary"
    system_profiler SPDisplaysDataType
} > "$output/runtime.txt" 2>&1
shasum -a 256 "$root"/build/bin/assets/shaders/*.spv > "$output/shader-sha256.txt"
if [[ -n "${WOW_DATA_PATH:-}" && -f "$WOW_DATA_PATH/expansions/tbc/manifest.json" ]]; then
    shasum -a 256 "$WOW_DATA_PATH/expansions/tbc/manifest.json" > "$output/asset-manifest-sha256.txt"
fi
if [[ "$mode" == --prepare-only ]]; then
    printf 'prepared_not_launched\n' > "$output/state.txt"
    printf 'Metadata prepared at %s. No client or game connection started.\n' "$output"
    exit 0
fi
printf 'running\n' > "$output/state.txt"
printf 'Starting ordinary WoWee. Session evidence: %s\n' "$output"
cd "$root" || exit 2
WOWEE_LOG_LEVEL=info WOWEE_LOG_FILE="r52-capture-$started-$$.log" DYLD_PRINT_LIBRARIES=1 VK_LOADER_DEBUG=driver "$binary" 2>&1 | tee "$output/client.log"
results=("${PIPESTATUS[@]}")
printf '%s\n' "${results[0]}" > "$output/exit-status.txt"
printf '%s\n' "${results[1]}" > "$output/logging-exit-status.txt"
printf 'exited\n' > "$output/state.txt"
# A system crash report may arrive after process exit. Copy only already-written
# reports for this executable from this session; never read other app reports.
python3 - "$output" "$started" <<'PY'
import pathlib,sys,shutil,json,hashlib
output=pathlib.Path(sys.argv[1]);started=int(sys.argv[2])
reports=pathlib.Path.home()/'Library/Logs/DiagnosticReports'
for path in reports.glob('wowee*'):
    if path.name.lower().startswith('wowee-') and path.is_file() and path.suffix in ('.ips','.crash') and path.stat().st_mtime>=started:
        dest=output/'crash-reports'/path.name;dest.parent.mkdir(exist_ok=True);shutil.copy2(path,dest)
manifest={str(p.relative_to(output)):hashlib.sha256(p.read_bytes()).hexdigest() for p in output.rglob('*') if p.is_file() and p.name!='SHA256SUMS.json'}
(output/'SHA256SUMS.json').write_text(json.dumps(manifest,indent=2)+'\n')
PY
archive_status=$?
printf 'Client exit %s; logging exit %s; archive exit %s. Evidence: %s\n' "${results[0]}" "${results[1]}" "$archive_status" "$output"
if [[ "${results[0]}" != 0 ]]; then exit "${results[0]}"; fi
if [[ "${results[1]}" != 0 ]]; then exit "${results[1]}"; fi
exit "$archive_status"
