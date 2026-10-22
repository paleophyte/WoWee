# Scriptable copilot sessions

## Saved-profile launch (first increment)

```
./build/bin/wowee --login-profile 'My TBC' --character 'Jizzle'
./build/bin/wowee --login-profile '10.102.172.4:3724' --realm 'My Realm' --character 'Jizzle' --startup-timeout 180
```

Use the saved server label or auth host:port shown in the login profile, and the exact character/realm spelling. An exact match must be unique. A remembered account and valid saved password hash are required; no credentials are accepted on the command line. Normal authentication and world-entry callbacks are reused. Multiple realms require an explicit realm. A missing/offline realm, missing character, ambiguous name or startup deadline fails rather than choosing another identity. Exit2 reports a launch failure. --help runs without opening the client. No arguments preserves normal interactive launch.

The startup deadline covers the login/world-entry frame loop, not synchronous initialization before the loop. It is not a gameplay duration limit or an external watchdog for a frozen process. An external supervisor is still needed to bound an unresponsive client. Login failure does not reconnect automatically. Server control state is unchanged by these arguments: saved-profile launch does not guarantee Manual or Bot. Do not use this increment alone as an unattended test harness.

Offline acceptance: parser and exact identity selection tests, normal client build, help/error commands without connection. Real saved-profile login remains an explicitly scoped live check.

## Control and status design

The local control increment below implements a per-session endpoint using Unix sockets. The intended platform coverage is an owner-only Unix socket on macOS/Linux and named pipe on Windows. A small CLI exposes status, manual, bot and stop; no public network listener or arbitrary Lua/console execution. A caller may connect remotely through authenticated SSH to run the local CLI.

Requests need identifiers, bounded waits and explicit outcomes: requested, confirmed, refused, timed out or disconnected. Reuse the addon/server command protocol for intent and server acknowledgement. Never report a UI click or sent command as confirmed control. A startup --control bot option can submit the same request only after the selected character is verified in-world and the copilot handshake is ready. Unsupported protocol or missing acknowledgement must fail clearly.

Status should contain session identity, connection/loading state, requested and acknowledged control, selected quest and its acknowledgement, immediate activity, objective progress, Notes, map/position/transport, heartbeat age and last error. Produce atomic JSON status plus ordered JSONL events in a unique evidence directory. Exclude secrets.

## Bounded run and shutdown design

The third increment below implements session duration, startup/control/stop deadlines and observable terminal reasons. Let the supervisor stop on success, deadline, disconnect, loss of control acknowledgement, transport coordinate rejection or stalled progress. Start with explicit, narrow completion conditions; unknown does not mean success. Manual takeover must cancel or inhibit automatic re-enabling of Bot.

A normal stop must request Manual, wait for server confirmation, request logout, wait for logout/disconnect, flush evidence and exit. Report each step separately and return nonzero when the stop cannot be confirmed. A crashed or disconnected client cannot prove the bot stopped. For dependable unattended runs, Playerbots needs a bounded server-side control lease or equivalent verified disconnect behavior, so a lost supervisor cannot leave it active indefinitely. This must preserve the selected quest and earned progress.

Keep gameplay in Playerbots. The harness issues intent/control commands and observes results; it must not steer movement, manufacture quests/items, teleport or edit character state. Live acceptance and any server lease deployment need explicit scope.

## Acceptance sequence

1. Offline parser/identity resolution and launch failure paths; no network.
2. Scoped ordinary saved-profile login to the requested character; no Bot request.
3. Local control request/acknowledgement tests including stale, refused, disconnect and timeout outcomes.
4. Scoped bounded Bot run followed by confirmed Manual and logout.
5. Prove watchdog/lease expiry stops control after client/supervisor loss before leaving an unattended test running.

## Local control interface (second increment)

Implemented on desktop macOS/Linux; Windows named pipes are not implemented.
The endpoint is opt-in and must be a new socket in an existing directory owned
by your account with no group/other permissions. The server checks peer UID.
It never removes a pre-existing endpoint, including a stale one from a crash.
Inspect/remove a stale socket yourself after verifying its process is gone.

```sh
session_dir=$(mktemp -d "$HOME/.wowee-session.XXXXXX")
./build/bin/wowee --control-socket "$session_dir/control.sock"
# Optional: combine with --login-profile / --realm / --character.
python3 tools/wowee_control.py --socket "$session_dir/control.sock" status
python3 tools/wowee_control.py --socket "$session_dir/control.sock" bot
python3 tools/wowee_control.py --socket "$session_dir/control.sock" manual
python3 tools/wowee_control.py --socket "$session_dir/control.sock" stop
```

The addon must include RemoteControl.lua; refresh an installed copy with
`python3 tools/update_bot_addon.py --target /absolute/path/to/addons/WoweeBotStatus`
while the client is closed. Saved variables are preserved. A missing/old addon
returns unavailable, rather than bypassing it with native gameplay commands.

`status` returns immediately with observed control, freshness/age, generation,
acknowledgement, pending mode, last failure, and active local request outcome.
It does not claim that stale cached control is current. `manual`, `bot` and
`stop` wait up to15 seconds by default (`--timeout 1..30`). The same addon
controller used by the buttons queries fresh server state, then requests an
explicit mode only if necessary. A matching current acknowledgement is required
for `confirmed`. A fresh query confirming an already-correct mode also suffices;
a cached mode alone does not. Manual/stop supersedes an outstanding Bot request.
Human takeover supersedes remote intent; it is never automatically reissued.

**Stop means server-confirmed Manual only. It leaves the client connected.**
Use the managed `quit` command below for logout/process exit. Do not treat this
endpoint alone as an unattended supervisor or disconnect safety mechanism.

Responses are one JSON object per request. Each includes protocol version1,
request id, command, outcome, and a control snapshot. Outcomes include observed,
requested (internal pending state), confirmed, refused, busy, superseded,
disconnected, unavailable, addon_error, timed_out, cancelled and invalid_request.
CLI exit0 means a status observation was returned or requested control confirmed;
exit2 means failure/unconfirmed. For status, inspect control.fresh and its
outcome/failure fields before treating it as authoritative. No command toggles
blindly, executes arbitrary Lua, or accepts credentials. SSH can invoke this
local CLI on the Mac; there is no public control listener.

A caller disconnect or timeout cancels an unconfirmed local intent and suppresses
its replay. It cannot undo a command already accepted by the server. Such results
report that control may still be active. A stalled main thread may prevent the
socket from answering; the CLI's independent timeout reports unconfirmed. A
server-side lease/disconnect guarantee is still needed for crash-safe unattended
runs. No new server deployment is required for this interface; it reuses WIDLE.

## Bounded lifecycle and supervisor (third increment)

Implemented offline on desktop macOS/Linux. Neither example below requests Bot
by itself. Use the local `bot` command explicitly after verifying the intended
character. Real login, control and shutdown still require scoped live validation.

```sh
# Once: create a parent for new per-run evidence directories.
mkdir -m 700 "$HOME/.wowee-runs"
python3 tools/wowee_session.py --profile 'My TBC' --character Jizzle \
  --seconds 1200 --output "$HOME/.wowee-runs/run-001"
# From another terminal, against this session:
python3 tools/wowee_control.py --socket "$HOME/.wowee-runs/run-001/control.sock" status
python3 tools/wowee_control.py --socket "$HOME/.wowee-runs/run-001/control.sock" bot
python3 tools/wowee_control.py --socket "$HOME/.wowee-runs/run-001/control.sock" quit
```

`quit` is new; `stop` continues to mean Manual only. Quit initiates a managed
sequence: request a fresh confirmed Manual mode through the same addon
controller, request ordinary logout without the interactive force-exit flag,
wait for a **new actual SMSG_LOGOUT_COMPLETE**, disconnect, and exit. A refused
logout, expired countdown, cached mode, cancelled logout, superseding control,
missing addon or lost connection cannot establish success. No retry re-enables
Bot. Once shutdown starts, new socket control mutations are refused. Losing the
quit caller does not abandon a safe shutdown already in progress.

The native `--session-seconds 1..86400` timer starts when the requested character
is in-world; it requires saved-profile launch and `--control-socket`. On expiry
it initiates that same managed quit. `--stop-timeout 1..120` (default45 seconds)
bounds the whole Manual/logout/disconnect sequence, including the normal20-second
logout wait. Native timers need a responsive frame loop. `SIGTERM`/`SIGINT` and
window close request managed quit when this endpoint is enabled. A second signal
forces exit2; it does not certify that Bot stopped. Interactive clients without
this endpoint keep their existing exit behavior.

The independent supervisor covers freezes during initialization, play and
cleanup. Default startup deadline180 seconds; requested duration; stop deadline45
seconds; final cleanup grace30 seconds. It asks for managed quit with SIGTERM at
the applicable deadline, then kills an unresponsive child after the allowed stop
and cleanup time. A native terminal receipt starts the cleanup grace immediately.
It always reaps its child. A forced kill, nonzero client exit, missing receipt or
incomplete shutdown is **unconfirmed/exit2**, including a hang after successful
logout. It does not call a frozen/disconnected Bot stopped merely because the
client process is gone. Server-side lease/disconnect safety remains unresolved.

Each socket-enabled run requires a fresh private directory, with no existing
`session.json` or `session-events.jsonl`. Paths must be canonical and short enough
for the platform's Unix socket path limit. Native receipts are owner-only:
atomic `session.json`, ordered flushed `session-events.jsonl`, fresh Manual
evidence, separate logout/disconnect confirmations, terminal reason and exit
code. Shutdown flushes addon variables before disconnect/teardown and marks
`client_shutdown_complete` only after native cleanup. The supervisor adds
`client.log`, `supervisor-start.json` and final `supervisor.json`, checking both
receipt confirmations and the actual child exit status.

A local quit response confirms the protocol sequence and impending exit; it is
not proof that graphics cleanup or OS process exit has finished. Use the final
supervisor result for that. A failed Manual request may leave server control
active; receipts say so. No grants, resets, teleports, character edits, alternate
server credentials or native gameplay execution are involved.
