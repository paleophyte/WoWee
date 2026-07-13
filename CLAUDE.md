# WoWee repo notes for Claude

## Two remotes, two audiences

- `origin` (`paleophyte/WoWee`) - this fork. Headless bot-fleet work
  (`tools/headless_client/`, `tools/bot_fleet_manager/`, `tools/world_db_query/`,
  `tools/provisioning/`, `BOT_PLAN.md`, the `WOWEE_HEADLESS`/`WOWEE_HEADLESS_DEFAULT`
  build machinery) lives here only - none of it exists upstream, so it can't
  meaningfully be split out or PR'd there.
- `upstream` (`Kelsidavis/WoWee`) - the original project. Only bug fixes and
  improvements to code that exists in *both* trees (`src/game/`, `include/game/`,
  `src/core/`, `src/ui/`, `src/rendering/`, etc. - the GUI/shared client) belong here.

This file itself is fork-specific context and belongs only on
`feat/headless-bot-fleet` (and future headless-work branches) - not on
`master`, which should stay a clean upstream mirror so fix branches cut from
it stay clean too.

## Don't build the `wowee` GUI target on this machine

This is a VM with no GUI testing use - the user pulls this repo to their Mac
or Windows laptop (whichever matches the platform they want to test) and
builds/runs `wowee` there. `wowee`'s compile time is significant, so treat
that cycle as expensive and avoid spending it here by default.

- Default to building only `wowee_headless` to verify shared-code changes
  compile - that's almost always sufficient, since `wowee` and
  `wowee_headless` share the same source for anything outside
  `tools/headless_client/` and `WOWEE_HEADLESS`-gated branches.
- CI (GitHub Actions, `Build (windows-x86-64)`/`(x86-64)`/`(macOS arm64)`/etc.)
  already builds and validates `wowee` for every PR - that's the normal way
  a `wowee`-side compile error gets caught, not a local build here.
- If something seems like it could *specifically* require compiling `wowee`
  locally to prove (e.g. a GUI-only code path CI can't exercise, or CI itself
  is failing in a way that needs local repro) - stop and ask the user for
  confirmation first, explaining exactly why the headless-only build or CI
  isn't enough for this specific case. Don't just build it "to be thorough."

## When fixing a bug in shared code

If a fix touches a file that also exists upstream (not just something under
`tools/headless_client/` or gated behind `WOWEE_HEADLESS`), cut a small,
focused branch for it off current `master` *in addition to* whatever work is
happening on this feature branch - don't let it get stuck waiting for a
future retroactive extraction. Precedent: `fix/area-trigger-dbc-load-race`
was done this way and is already merged upstream as PR #91.

1. Before branching, `git fetch upstream` and diff the target function/file
   against `upstream/master` to confirm the bug is still actually there (not
   already fixed independently) - don't assume the local history summary is
   still accurate.
2. `git checkout -b fix/<short-name> master` (or `feat/<short-name>` for a
   genuine improvement, not a bug fix).
3. Apply *only* the shared-code hunks - if the same commit on this branch
   also touched a headless-only call site (e.g. a new HTTP endpoint in
   `tools/headless_client/main.cpp` that exercises the fix), leave that part
   out. Check `git diff master feat/headless-bot-fleet -- <file>` first;
   several shared files have accumulated unrelated headless-only hunks
   (env-var gating, new headless-only methods) mixed in from other work, so
   don't blindly `git checkout <branch> -- <file>` a whole file without
   checking what's actually in its diff.
4. Build `wowee_headless` against the new branch before committing (see
   "Don't build the `wowee` GUI target on this machine" above for why not
   both - CI covers `wowee` once pushed).
5. Push and open a PR against `paleophyte/WoWee` `master` first - never
   open the upstream PR before this one exists and its CI is clean.
6. Once that PR's local CI is green, `git fetch upstream` again and check
   `git log --oneline master..upstream/master`. This confirms the fix
   branch is still current *at the moment of forwarding*, not just when it
   was cut - upstream moves fast enough that new commits can land during
   the CI run itself.
   - **Not behind**: open the PR directly against `Kelsidavis/WoWee`
     `master` - this is pre-authorized, no need to check back first.
   - **Behind, and the new upstream commits don't touch this fix's files**:
     open the PR anyway: unrelated upstream churn isn't a reason to hold it.
   - **Behind, and a new upstream commit touches the same function/file**:
     do not open the PR yet. Re-run the full sync cycle first - merge
     upstream into `master`, merge `master` into every branch carrying this
     fix (the feature branch *and* the shared-code fix branch), resolve
     conflicts (favor whichever side has real, currently-working logic over
     a duplicate/dead code path - see the `updateTaxiAndMountState()`
     precedent from 2026-07-12 for the reasoning to apply), rebuild
     `wowee_headless`, push, and wait for CI to go green again (covering
     `wowee`) before re-checking upstream and opening the PR.

Keep each branch to one logical fix - small and reviewable, matching the
PR #91 precedent, not one giant "everything shared" branch.
