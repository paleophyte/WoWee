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
4. Build both `wowee` and `wowee_headless` targets against the new branch
   before committing.
5. Push and open a PR against `paleophyte/WoWee` `master` (not upstream
   directly - the user reviews and forwards to upstream themselves once
   local CI is clean).

Keep each branch to one logical fix - small and reviewable, matching the
PR #91 precedent, not one giant "everything shared" branch.
