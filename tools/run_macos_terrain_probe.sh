#!/usr/bin/env bash
# Generated terrain only: no MPQs, account, server or existing player profile.
# Failure to create a Metal/Vulkan device is a failed test, never a skipped pass.
set -uo pipefail
umask 077

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$root/build/bin/wowee_terrain_texture_probe"
if [[ "$(uname -s)" != Darwin ]]; then
    printf 'This probe runner requires macOS.\n' >&2
    exit 2
fi
if [[ $# != 1 || ! -x "$binary" ]]; then
    printf 'Build wowee_terrain_texture_probe, then run: bash %s NEW_OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi
# Refuse to overwrite an earlier trial, including a partial/failed one.
if ! mkdir "$1"; then exit 2; fi
output="$(cd "$1" && pwd)"
mkdir "$output/config" || exit 2
{
    date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
    sw_vers
    uname -m
    git -C "$root" rev-parse HEAD
    shasum -a 256 "$binary"
    brew list --versions molten-vk vulkan-loader
    printf 'VK_DRIVER_FILES=%s\nVK_ICD_FILENAMES=%s\n' "${VK_DRIVER_FILES:-<unset>}" "${VK_ICD_FILENAMES:-<unset>}"
    for manifest in "${VK_DRIVER_FILES:-}" "${VK_ICD_FILENAMES:-}"; do
        if [[ -n "$manifest" && -f "$manifest" ]]; then cat "$manifest"; fi
    done
    otool -L "$binary"
    system_profiler SPDisplaysDataType
} > "$output/runtime.txt" 2>&1

cd "$root/build/bin" || exit 2
WOWEE_CONFIG_ROOT="$output/config" DYLD_PRINT_LIBRARIES=1 VK_LOADER_DEBUG=driver \
    "$binary" "$output/result.json" 2>&1 | tee "$output/probe.log"
results=("${PIPESTATUS[@]}")
printf '%s\n' "${results[0]}" > "$output/exit-status.txt"
printf 'Probe exit: %s; evidence: %s\n' "${results[0]}" "$output"
if [[ "${results[0]}" != 0 ]]; then exit "${results[0]}"; fi
if [[ "${results[1]}" != 0 ]]; then exit "${results[1]}"; fi
if [[ ! -s "$output/result.json" ]]; then
    printf 'Probe returned success without an allocation result.\n' >&2
    exit 1
fi
