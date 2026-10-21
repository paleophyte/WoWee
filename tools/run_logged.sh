#!/usr/bin/env bash
# Keep native driver/Metal stderr as well as normal WoWee output after a crash.
set -uo pipefail
umask 077

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${WOWEE_BINARY:-$root/build/bin/wowee}"
log_dir="${WOWEE_LOG_DIR:-$root/logs}"
if [[ ! -x "$binary" ]]; then
    printf 'WoWee executable not found: %s\n' "$binary" >&2
    exit 1
fi
mkdir -p "$log_dir" || exit 1
log="$log_dir/wowee-$(date +%Y%m%d-%H%M%S)-$$.log"
printf 'Saving WoWee stdout and stderr to %s\n' "$log"
"$binary" "$@" 2>&1 | tee "$log"
results=("${PIPESTATUS[@]}")
printf 'WoWee exited with status %s. Log: %s\n' "${results[0]}" "$log" >&2
if [[ "${results[0]}" != 0 ]]; then exit "${results[0]}"; fi
exit "${results[1]}"
