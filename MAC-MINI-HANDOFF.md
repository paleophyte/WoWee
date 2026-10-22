# Continue WoWee / WoW Idle on the Mac Mini

Prepared October 21, 2026, from the Windows checkout, the original chat, the
companion implementation/evidence records, and read-only checks on the Mini.
This is the current continuation guide; the older companion
`wow-idle/MAC-MINI-HANDOFF.md` and `MAC-MINI-SETUP.md` describe earlier setup.

## Server0066 deployed and healthy — October 21, 2026

Approved normal CMake build/install completed; exact tested candidate reproduced.
Installed mangosd SHA256: `0d48ddb77a970f104b8150c33f88e7c99b675801ee8af7ffc2bc9b57f44214e3`.
Fresh world PID992450, startup marker, authenticated read-only world response,
listeners and stable NRestarts0 pass. Five active configs unchanged; only mangosd
installed, all other install items up-to-date. Jizzle's latest saved position,
transport fields, health and offline state match before/after; no character edits.
Source/binary/config rollback evidence retained; no rollback needed. One-use
liveness credentials removed. Evidence: `logs/server0066-normal-deploy-20261021/REPORT.md`.

Current client functional changes34c9ce77a+d8a006756 are published. Remaining gate:
separately approved bounded user-operated bot ferry/accepted-quest confirmation.
No live session launched after deployment. R52 transport remains open; original
Metal assertion cause unknown. Historical sections below retain their old scopes.

## Mac SSH and rollback readiness — October 21, 2026

Mac SSH alias `wow-tbc` authenticates as `mangos-helper` with pinned host key.
Keys are installed privately in ~/.ssh; no private material is in this repo.
An isolated rebuild of current0065 reproduced the exact object, archive, linked
binary and installed RUNPATH projection hashes. Primary source/archive/installed
bytes remain unchanged; no service or character changes. Evidence:
`logs/server0066-rollback-build-20261021/REPORT.md`.
The existing permitted CMake build/install route avoids needing new admin access.
Concrete deploy/rollback scope is in the private handoff
`/Users/neko/code/wow-idle/server/handoffs/0066-deployment-reviewed-20261021/NORMAL-BUILD-PLAN.md`.
Deployment/restart and subsequent live validation still require explicit scope.

## Server0066 candidate reviewed — October 21, 2026

Source-bound fixtures reproduce the boarding-order defect in8active world-spline
cases;0066 passes23/23 cases on Windows and Linux. It stops/clears the old motion
before attaching, preserving local path geometry and routing/teleport policy.
Actual full changed-file TBC compilation and incremental server link pass. The
candidate is built, not deployed; this is not connected ferry/quest acceptance.
All48files in the private evidence capsule verify on this Mac. Receipt:
`/Users/neko/code/wow-idle/server/handoffs/0066-transport-attach-order-20261021/receipt.json`.
The patch and review/limits are in its `server/patches/0066-transport-attach-order.md`.

Primary0065 source64, installed binary and service are unchanged. Two read-only
Jizzle saves match: offline, alive, no transport; no character edits. Mac has
prepared concrete deploy/rollback readiness; deployment/restart and further live
validation require explicit scope. R52 transport issue remains open; original
Metal assertion cause remains unknown. Prior earned evidence is retained.

## Native ferry guard verified — October 21, 2026

Impossible WMO passenger offsets now retain the movement hold and produce one
local error, instead of attaching thousands of yards from the ship. Fresh valid
passenger data can resolve the hold. Focused21 assertions/1case, ferry370/19,
full replay3,609/113 all pass; normal Mac client builds. This prevents client
amplification of malformed attachment data; it does not fix its server origin
or prove successful quest travel. Evidence: `logs/quest-reliability-live-20261021-01/GUARD-REPORT.txt`.

Windows chat “Find WoW idle leveling projects” is explicitly authorized for
read-only server inspection and isolated candidate build/testing. No deployment,
server restart, live gameplay, or character edits authorized. Mac SSH access is now verified.
Boarding-before-stopping-old-world-spline is under investigation. New live
validation will need explicit scope after an underlying fix is verified.

## Ordinary quest session failed — October 21, 2026

Approved user-operated “Lifting the Curse” (290) attempt confirmed selected quest
survives Manual/Bot, but exposed a transport blocker: falling/warping after ferry
arrival, then return to Kalimdor/Feralas. Client/logger exited 0 at 19:19:20 CDT.
Attachment log accepted a world-sized offset and composed a position thousands
of yards from the ship; origin is under investigation. No character/server edits.
R52 reopened for relevant transport failure; original Metal assertion cause unknown.
Evidence: `logs/quest-reliability-live-20261021-01/failure-observation.json`.
No `strreplace` error in the closed attempt; other FrameXML errors remain separate.
Current goal stays active. Offline reproduction/fix precedes any newly scoped live
validation. Earlier successful ferry evidence below is retained at its old scope.

## Current reliability increment — October 21, 2026

TBC combat-log `strreplace` compatibility is fixed in the native Lua API.
A production-engine regression reproduces the prior nil-function failure;
after the fix, focused coverage passes 14 assertions and full replay passes
3,588 assertions / 112 cases. The normal Mac client builds successfully.
Evidence: `logs/combatlog-strreplace-20261021/REPORT.txt`.

The approved ordinary quest session above failed at ferry arrival. Prior
quest/control/earned turn-in evidence remains valid at its original scope. Other
FrameXML errors remain separately tracked; original Metal assertion cause unknown.

## Current state — validated October21,2026

**Current client:** `d17866e7b`, published on `codex/playerbot-client-sync`.
Server0065remains the retained reproducible deployment; no server changes made.
Copilot control, accepted-quest intent/activity/blockers and earned kill/collection
turn-ins remain verified at their documented scopes.93retained evidence hashes
rechecked successfully. Playerbots still executes gameplay; addon owns intent/UI.

**Mac acceptance:** full replay3,574assertions/111cases passes. Real scene/loading,
49tile allocation/fence, destination-overlap, shutdown-style flush and busy-worker
checks pass. Cleanup worst offline presentation~484ms→13.451ms, with the same
warm allocation baseline and no leaked retirement work.

**Final live confirmation:** tested binary matches8fc86122…; user-operated
map1→0→1round trip loads49/45destination tiles using fresh world positions.
User confirms both legs looked great; client/logger exit0. No GPU error,
watchdog stall or slow cleanup warning. Evidence:
`logs/r52-live-budgeted-roundtrip-20261021-01/arrival-acceptance.json`.
Audit: `logs/goal-completion-audit-20261021/REPORT.txt` and `requirements.json`.

**R52 resolution scope:** current Mac/TBC reported ferry route validated after
worker, world-position, resource-lifetime and cleanup fixes. Exact original lost
Metal assertion/unique original cause is not proven. No broader hardware/route
or unattended guarantee. The return pause has both offline reproduction/fix
and relevant user-confirmed Mac acceptance; no further release check is pending.

No agent gameplay input, grants, resets, teleports, character edits or deployment.
Live gameplay occurred only in explicitly approved user-driven sessions. Existing
normal logout/saved-relog proof remains historical; new clean exits are not
misrepresented as a fresh server-side logout/save audit.

One current Mac checklist: companion `MAC-TEST-CHECKLIST.md` (none unresolved for
this milestone). Next recommended small milestone: fix the repeated TBC combat-log
`strreplace` compatibility error, with focused offline event coverage. Other
FrameXML errors stay in roadmap; no broad UI work or new goal started.
Older sections below preserve qualified investigation history, not pending gates.

## Real multi-tile validation — October 21, 2026

`--real-pressure` passes three seven-by-seven source/destination cycles using
actual ADT/BLP data and production workers/finalization. All 49 Wetlands tiles
and all 45 available Dustwallow tiles load; four absent ADTs are recorded.
Resident chunks: 12,544 / 11,520; recorded draws: 7,132 / 6,947 with distance
culling active. Peak observed: 59,010 allocations / 306,930,448 bytes.
Warm clears consistently return to 90 allocations / 98,283,472 bytes.
Evidence: `logs/r52-real-pressure-20261021/REPORT.txt` and `probe/`.

R52 remains unresolved. No production changes or dependency workaround is
supported by these passes. Next is isolated normal world rendering with actual
M2/WMO/water and transition while source work remains in flight. This probe has
no screenshot acceptance, account, connection or gameplay/server state changes.
Use the explicit TBC WOW_DATA_PATH shown below with `--real-pressure` to repeat.
Earlier next-step paragraphs are retained as historical context.

## Real-data terrain validation — October 21, 2026

GitHub authentication is repaired; all earlier commits through `cf4724d78`
are published. Extracted TBC assets are installed and verified as below.
The new opt-in `--real-terrain` probe passes three offline Wetlands/Dustwallow
cycles through actual ADT/BLP reads, production workers and incremental upload.
Each tile records 256 terrain draws. Once reusable textures warm, clears through
frame fences return to 25 allocations / 95,533,008 bytes with no repeated growth.
Evidence: `logs/r52-real-terrain-20261021/REPORT.txt` and `probe/`.

R52 remains open. This covers one real tile at a time, without M2/WMO/water
rendering or live arrival. Next is bounded multi-tile real-data pressure.
No server, account or gameplay state changed. Historical missing-assets and
GitHub-blocker paragraphs below are superseded by this section.

```sh
WOW_DATA_PATH='/Users/neko/Library/Application Support/Wowee/Data/expansions/tbc' \
  bash tools/run_macos_terrain_probe.sh NEW_OUTPUT_DIRECTORY --real-terrain
```

## Local game assets installed — October 21, 2026

The previous missing-assets prerequisite is cleared. The user's original
`/Users/neko/code/WoW-243-Client.zip` is preserved and unpacked at
`/Users/neko/code/WoW-243-Client`. WoWee assets are installed at the standard
macOS data root `/Users/neko/Library/Application Support/Wowee/Data`, with the
TBC manifest under `expansions/tbc/manifest.json`. All 114,111 extracted manifest
files passed size/CRC verification; zero extraction failures. The extractor
skipped 8,949 zero/invalid-size MPQ entries. Client expansion metadata is seeded.

A fresh isolated TBC profile initializes the actual renderer, spawner and world
loader and renders three hidden frames on Apple M4 with no game connections
requested. This is startup coverage, not map loading or an R52 crash fix.
No personal login configuration, account, server or gameplay state changed.
Paths and evidence: `logs/asset-setup-20261021/asset-setup.json` and `REPORT.txt`;
companion `MAC-LOCAL-ASSETS.json` has the same reusable setup paths.
Use those paths for the next isolated offline real-data R52 increment.
GitHub publication is working; see the current section above.

## Latest local Mac validation — October 21, 2026

The Mini now builds both requested targets. The original small terrain probe
passes on Apple M4 with actual loaded MoltenVK 1.4.2 and Vulkan Loader
1.4.363.0. A new opt-in `--streaming-pressure` mode also passes: three cycles
of twelve full 256-chunk tiles, nine resident at a time, replacing three tiles
before cleanup of each replacement. Each chunk uses 145 vertices, 768 indices and three
64x64 R8 alpha images, uploaded in asynchronous 16-chunk batches. Peak observed
allocations: 15,878 / 153,965,824 bytes. Every clear returns to 6 allocations /
94,867,712 bytes. Persistent renderer buffers are part of that baseline.

Evidence is retained locally under `logs/r52-mac-validation-20261021-cbxXsY/`
and `logs/r52-streaming-20261021-aAtMW2/` (full-geometry subdirectory).
These are Mac results, not reused Windows results. Run the extended check with:

```sh
bash tools/run_macos_terrain_probe.sh NEW_OUTPUT_DIRECTORY --streaming-pressure
```

The subsequent `--framed-retirement` mode also passes on this Mac. It draws two
full tiles with four generated 256x256 RGBA8 reusable textures and mipmaps,
retires a tile after recording its draws but before submission, and advances
real swapchain frame slots. All three cycles retain 3,086 allocations until
both frame-slot fences have been waited, then retain exactly the surviving
256-chunk tile (1,550 allocations). Clearing through frames returns to the
14-allocation / 96,281,680-byte baseline including reusable textures and probe
frame resources. Evidence: `logs/r52-framed-20261021-zU4M9z/probe/`.

The subsequent `--framed-pressure` mode passes three cycles of the reported
49-tile scale: 12,544 chunks are uploaded through 16-chunk incremental batches,
with a real frame drawn between tiles. Peak: 75,278 allocations / 374,005,840
bytes. Removing one tile preserves all 12,288 remaining chunk draws and waits
for both frame-slot fences before retirement; every clear returns to the same
14-allocation / 96,281,680-byte baseline. The two-tile regression also passes.
This mode spaces generated chunks across a tile to avoid artificial per-chunk
overdraw, but repeats the tile geometry and uses only four small shared RGBA8
textures. Evidence: `logs/r52-framed-20261021-zU4M9z/pressure49-final/`.

The `--texture-pressure` extension also passes three 49-tile cycles with
196 generated 512x512 RGBA8 reusable textures and mipmaps (four distinct textures
per tile). Every image is required to upload. Peak: 75,470 allocations /
648,863,824 bytes. Every clear through frame slots returns to 206 allocations /
371,139,664 bytes, including the persistent reusable texture set. Evidence:
`logs/r52-textures-20261021-VQcfI1/probe/`.

The `--worker-transitions` mode now passes three cycles through actual worker
queues, cache lookup, nested upload batching, processReadyTiles finalization,
map reset and soft reset. Generated decoded tiles enter through the existing
cache/test-access friendship; no alternate worker/finalizer is used. A source
map is interrupted with ready/queued work and partially uploaded terrain.
For the busy reset, the fixture restarts the pool under the held cache lock to
ensure six real workers are blocked in prepareTile; reset must wait at their
join before the gate is released. Reset itself runs serially on a helper thread
only to permit that gate release; no concurrent render-thread access occurs.
Old queues/cache/finalizers are empty afterward, and GPU allocations return to
14 / 96,281,680 bytes through real frame slots. A fresh destination tile at the
same coordinates fully loads and records 256 draws, followed by a clean soft
reset. Evidence: `logs/r52-workers-20261021-sR4Y8x/busy-worker-probe/`.

The `--blp-pressure` mode also passes three 49-tile cycles through the actual
BLPLoader: generated 512x512 BLP2 DXT1/DXT3/DXT5 mip chains plus a decoded-DXT5
path. All 196 images must upload; 147 must reach the compressed GPU upload path.
The successful run records 42,820,120 compressed bytes (205,520,847 decoded-byte
estimate). Peak: 75,470 allocations / 484,543,696 bytes; clears return 206 allocations /
206,819,536 bytes. An initial probe-only mistake called decodeBaseLevel on an
already-decoded image; its failed run is retained and corrected, with no
production loader change. Evidence: `logs/r52-blp-20261021-wra7W8/corrected-probe/`.

R52 remains unresolved. Generated bytes and cached input do not cover actual
ADT/BLP game files, M2/WMO/water or full-world rendering. No extracted asset
manifest exists in the checked Data/TBC paths, no Mac config/profile exists in
the default/portable locations, and WOW_DATA_PATH is unset. An asset directory
path has been requested for isolated offline checks. The missing original
Metal assertion still prevents a direct crash comparison; none of these passing
synthetic workloads proves the original gameplay abort fixed.
CLI publication retries were rejected: the saved credential belongs to joshand,
which cannot write this fork; paleophyte has no CLI credential. The connected
GitHub integration also rejected tree writes (403). Local commits are preserved;
publication needs working write access, not more identical retries.
The next investigation should address those gaps with bounded generated inputs,
not an unbounded OOM test or a claimed gameplay-crash fix.

The initial full Mac replay run failed one assertion (110/111 cases passed).
The accepted-quest case consumed a diagnostic queued by earlier target selection;
`UnitFactionGroup` returns zero Lua values when race is unknown, which the
interface probe passed directly to `tostring`. A deterministic queued-probe
regression reproduces that failure in isolation before the fix. Coercing the
empty faction result to one nil value fixes it: focused 32/32 assertions and
full Mac replay 3,574/3,574 assertions in 111 cases now pass. The original failed
run remains retained. Historical companion text about a queued GitHub run
and an active old Goal is stale; this handoff and current local evidence govern.

## Objective and immediate focus

The product is an interactive WoWee copilot for the existing TBC CMaNGOS
Playerbots server: switch between **I am playing** and **the bot is playing**
on the same character, with server-confirmed control, preserved quest intent,
clear immediate activity, and useful blocker/outcome reporting. Gameplay
execution belongs to Playerbots; the addon expresses intent and reports state.

The next development focus is **R52: the Mac Metal/MoltenVK terrain-allocation
abort**. Keep this investigation separate from already verified control,
quest, and ferry improvements. Do not reopen the whole roadmap during migration.

The previous migration notes report that the persistent Goal was paused.
This preparation does not resume that Goal or start new development. When the
user resumes work on the Mac, begin with the validation steps below.

## Conversation and workspaces

Original chat: **Find WoW idle leveling projects**,
`01a14672-0ebf-72b1-8630-fd73de5d7210`. It remains on Windows as the full history.
The explicit cross-host handoff found the matching Mini project, but failed
before transferring artifacts with:

> Paginated chats cannot be continued on another host yet

Continue in a new chat opened in the Mini's saved **WoWee** project. This
document carries the working context; it is not a copy of the full transcript.
No local worktree handoff is needed.

| Purpose | Windows | Mac Mini |
|---|---|---|
| Client Git repository | `C:/Users/admin/code/WoWee` | `/Users/neko/code/WoWee` |
| Companion documents, server patches, scripts and evidence; not a Git repository | `C:/Users/admin/code/wow-idle` | `/Users/neko/code/wow-idle` |

Repository: `https://github.com/paleophyte/WoWee.git`.
Keep branch **`codex/playerbot-client-sync`**. At the start of this preparation,
Windows, GitHub and the clean Mini checkout all matched
**`1540641e91e9bed468ba154c4e962b6f95970222`**. The commit adding this guide
preserves the native source at that revision and adds documentation/ignore rules.

The preceding migration copied **10,117 companion files**, checksum-verified
them, and retained the Windows originals. The Mini's implementation, roadmap
and setup documents were confirmed present again during this preparation.
Private server credentials were excluded. Setup/archive records are retained
privately at `/Users/neko/code/.wow-idle-migration-20261021-01/`.

Read these companion files before changing behavior:

1. `/Users/neko/code/wow-idle/IMPLEMENTATION.md` — implementation and evidence.
2. `/Users/neko/code/wow-idle/ROADMAP.md` — priorities and R52's remaining scope.
3. `/Users/neko/code/wow-idle/MAC-TEST-CHECKLIST.md` — optional gameplay checks.
4. `/Users/neko/code/wow-idle/diagnostics/2026-10-21-macos-probe-client-publication.json`
   and `2026-10-21-terrain-alpha-acceptance.json` — tooling publication and
   before/after allocation evidence. Historical paths and platform labels must
   retain their original provenance.

## Completed work and limits of the evidence

- Server **0065** was recorded deployed and healthy in the preceding session.
  Server-confirmed Manual/Bot handoff, accepted-quest reporting/selection,
  separate plan versus immediate activity, and earned kill/collection turn-ins
  have retained evidence. This preparation does not recheck the live server.
- **R57, `7af010401`:** finish/cancel old terrain workers before clearing
  map-owned queues and caches. The native worker-race regression and focused
  rendered map-transition checks passed on Windows.
- **R58, `b2ea395d7`:** select arrival terrain from the fresh server world
  snapshot rather than passenger-local offsets. Rendered passenger login and
  live map transfer retained the ship attachment and normal logout.
- **R43:** a real Manual Wetlands-to-Theramore crossing, keyboard walk-off,
  logout and independent saved ashore reconnect passed on **b2ea395d7**. This
  covers that route/build; it is not a fresh crossing on the later graphics tree.
- **R59, `266e9f911`:** retire unused per-chunk terrain alpha textures through
  existing GPU fences, retaining live-tile textures and reusable tilesets.
  The real generated-terrain Vulkan probe reproduced allocation growth before
  the fix (baseline 6, clear rounds 18/30/42, final 66); after the fix, clears
  and the final sequence return to 6. Partial-unload and pre-fence checks pass.
  This demonstrates a resource-lifetime fix, not the cause of the Mac abort.
- The retained Windows validation built four native targets, passed the
  **native replay suite's 3,574 assertions / 111 cases**, and passed focused GPU
  allocation and renderer-startup checks. These are historical Windows results,
  not a newly run full CTest suite or a passing Mac test.
- **`1540641e9`:** adds only the isolated macOS probe workflow and runner script.
  [GitHub run 39617270958](https://github.com/paleophyte/WoWee/actions/runs/39617270958)
  was previously queued; checked during this preparation, its `probe` job is
  **completed / cancelled**. Do not count it as Mac validation.

## Unresolved issues

- **R52 remains open.** The reported SIGABRT reaches IOGPU/MoltenVK image
  allocation, VMA, `VkTexture::upload` and incremental terrain loading. The
  original exact Metal assertion was lost. Installed package versions do not
  establish the libraries actually loaded by the failing process.
- No native build or generated-terrain GPU test had run on the Mini during
  setup. Read-only inspection again found no built terrain-probe executable.
  A failed device initialization is a failed test, never a skipped pass.
- Companion controllers can contain Windows paths, process flags and toolchain
  assumptions. Review/port a controller before running it on macOS. Keep the
  companion and client directories as siblings; avoid silently pointing a
  controller at a different worktree.
- The asset-free probe needs no game data or account. Ordinary gameplay needs
  the user's own extracted assets and privately provisioned local profile.
  Git does not supply those assets or credentials. Server administration from
  the Mini also needs separate private credential provisioning.
- Broader routes, sustained unattended behavior and roadmap features retain
  their separate status. The historical `BOT_PLAN.md` concerns earlier headless
  fleet experiments and is not the current interactive copilot backlog.

## Confirmed Mini setup

Connected host: **Mac-mini.local**. Login/home: `neko` / `/Users/neko`.
Read-only SSH checks confirmed **arm64**, macOS **27.0.1**, Homebrew under
`/opt/homebrew`, and a clean checkout at the source revision above.

Installed packages observed: MoltenVK **1.4.2**, Vulkan Loader and headers
**1.4.363.0**, CMake **4.4.4**, Ninja **1.13.2**, SDL3 **3.4.12**, GLM **1.0.3**,
OpenSSL **3.6.3**, zlib **1.3.2**, FFmpeg **8.1.2_1**, Unicorn **2.1.4**,
StormLib **9.40**, shaderc **2026.4**.

The existing `build/CMakeCache.txt` is configured for Release, Ninja, deployment
target 13.0, `/opt/homebrew`, and `WOWEE_BUILD_TESTS=ON`. Required imgui,
vk-bootstrap and FSR2 submodules are initialized. The optional FSR3 SDK is
uninitialized and is not needed for this work. Configuration success is not
native build/test success. Do not upgrade dependencies speculatively.

## Build and test on the Mini

First inspect local state. If there are changes or unexpected commits, preserve
them and reconcile before updating; do not reset or clean the checkout.

```sh
cd /Users/neko/code/WoWee
git status --short --branch
git fetch origin
git switch codex/playerbot-client-sync
git merge --ff-only origin/codex/playerbot-client-sync
git submodule update --init extern/imgui extern/vk-bootstrap extern/FidelityFX-FSR2
```

The main checkout already has a configured build. If reconfiguration is needed,
use the same settings as the isolated workflow:

```sh
cd /Users/neko/code/WoWee
BREW="$(brew --prefix)"
export PKG_CONFIG_PATH="$BREW/lib/pkgconfig:$(brew --prefix ffmpeg)/lib/pkgconfig:$(brew --prefix openssl@3)/lib/pkgconfig:$(brew --prefix vulkan-loader)/lib/pkgconfig:$(brew --prefix shaderc)/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_PREFIX_PATH="$BREW" \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" -DWOWEE_BUILD_TESTS=ON
```

Build the probe and native replay regression (these targets also depend on the
normal client and its shader build), then run replay:

```sh
cmake --build build --target wowee_terrain_texture_probe test_game_replay --parallel 3
./build/bin/test_game_replay
```

Run the actual generated-terrain allocation test. It exercises Vulkan/Metal,
requires usable GPU/window access, and captures runtime metadata, loaded dylibs,
stderr, allocations and exit status. It needs no extracted game assets, account
or server. The script refuses to overwrite an existing output directory.

```sh
export VK_DRIVER_FILES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"
test -f "$VK_DRIVER_FILES"
mkdir -p logs
bash tools/run_macos_terrain_probe.sh "$PWD/logs/terrain-probe-$(date +%Y%m%d-%H%M%S)"
```

Inspect `runtime.txt`, `probe.log`, `result.json` and `exit-status.txt` together.
Bind results to the source revision and binary hash recorded by the script.
A passing allocation probe narrows the investigation; it does not prove the
original gameplay crash fixed. No deliberate gameplay crash is required.

For an optional ordinary gameplay session after private assets/profile setup:

```sh
cmake --build build --target wowee --parallel 3
DYLD_PRINT_LIBRARIES=1 bash tools/run_logged.sh
```

The launcher saves stdout/stderr under `logs/`. Review paths and runtime logs
before sharing them. No addon or server update accompanies this handoff.

## Review of previously untracked files

- `BOT_PLAN.md`: reviewed and preserved in Git with a historical-context notice.
- `TODO.md`: older taxi/fleet test history includes local account/setup
  instructions; retained locally, excluded from the public commit. It is not
  needed to resume R52. The current state is summarized here and in the already
  copied companion workspace.
- `client/`: local game installation, executable backup and machine settings;
  excluded. This is not missing client source.
- `tools/bot_fleet_manager/`: untracked files are fleet profiles and generated
  runtime maps, route state and surveys; excluded. No new source was found there.
- `tools/headless_client/settings.json` and provisioning rosters: local
  configuration/account data; excluded.
- `vcpkg_installed/`: generated Windows dependency packages and metadata;
  excluded. The Mini uses its own Homebrew dependencies.

The native/addon/probe source needed for the current task was already tracked
and pushed at the recorded source revision. These exclusions preserve the local
files without publishing them; future tool source and example profiles remain
eligible for tracking. The separate companion workspace remains a private,
non-Git transfer and is not folded into the client repository.

## Next steps and operating boundaries

1. Open a new chat in the Mini's WoWee project and point it at this document.
   Confirm actual working directory, branch and local changes before acting.
2. When asked to resume development, build/run the native replay and asset-free
   terrain probe above on the Mini. Save failure evidence as well as successes.
3. Use demonstrated Mac results to narrow R52. Capture the actual loaded
   runtime; avoid a speculative driver setting or package upgrade. Make a small
   source change only when the evidence supports it, then run relevant checks
   and commit/push to the same branch.
4. Preserve existing earned results. Previous records leave QA actor 1859
   healthy/offline on the Theramore pier (map 1, no transport), with QA processes
   and ferry tunnel stopped. Do not reset, grant, teleport, inject passengers or
   relocate a character to manufacture an acceptance result. Do not change the
   primary player's character/account during setup.
5. Server changes, if later required, need explicit scope, private access and a
   reproducible/reversible deployment. This preparation performs no server
   deployment or gameplay. The existing public fork/branch remains the client
   publication path; do not publish private credentials or the companion archive.

Suggested first message in the new Mac chat:

> Read `/Users/neko/code/WoWee/MAC-MINI-HANDOFF.md` and the referenced companion
> implementation and roadmap. Verify the checkout and setup, then report readiness
> and the next R52 validation step. Preserve local files and historical evidence;
> do not resume the paused Goal or perform server/gameplay changes during setup.
