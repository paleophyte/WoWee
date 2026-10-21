# Continue WoWee / WoW Idle on the Mac Mini

Prepared October 21, 2026, from the Windows checkout, the original chat, the
companion implementation/evidence records, and read-only checks on the Mini.
This is the current continuation guide; the older companion
`wow-idle/MAC-MINI-HANDOFF.md` and `MAC-MINI-SETUP.md` describe earlier setup.

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
