# Scriptable copilot sessions

## Saved-profile launch (first increment)

```
./build/bin/wowee --login-profile 'My TBC' --character 'Jizzle'
./build/bin/wowee --login-profile '10.102.172.4:3724' --realm 'My Realm' --character 'Jizzle' --startup-timeout 180
```

Use the saved server label or auth host:port shown in the login profile, and the exact character/realm spelling. An exact match must be unique. A remembered account and valid saved password hash are required; no credentials are accepted on the command line. Normal authentication and world-entry callbacks are reused. Multiple realms require an explicit realm. A missing/offline realm, missing character, ambiguous name or startup deadline fails rather than choosing another identity. Exit2 reports a launch failure. --help runs without opening the client. No arguments preserves normal interactive launch.

The startup deadline covers the login/world-entry frame loop, not synchronous initialization before the loop. It is not a gameplay duration limit or an external watchdog for a frozen process. An external supervisor is still needed to bound an unresponsive client. Login failure does not reconnect automatically. Server control state is unchanged by these arguments: saved-profile launch does not guarantee Manual or Bot. Do not use this increment alone as an unattended test harness.

Offline acceptance: parser and exact identity selection tests, normal client build, help/error commands without connection. Real saved-profile login remains an explicitly scoped live check.

## Next increment: local control and confirmed status

Add a per-session local endpoint, preferably an owner-only Unix socket on macOS/Linux and named pipe on Windows. A small CLI should expose status, manual, bot and stop; no public network listener or arbitrary Lua/console execution. A caller may connect remotely through authenticated SSH to run the local CLI.

Requests need identifiers, bounded waits and explicit outcomes: requested, confirmed, refused, timed out or disconnected. Reuse the addon/server command protocol for intent and server acknowledgement. Never report a UI click or sent command as confirmed control. A startup --control bot option can submit the same request only after the selected character is verified in-world and the copilot handshake is ready. Unsupported protocol or missing acknowledgement must fail clearly.

Status should contain session identity, connection/loading state, requested and acknowledged control, selected quest and its acknowledgement, immediate activity, objective progress, Notes, map/position/transport, heartbeat age and last error. Produce atomic JSON status plus ordered JSONL events in a unique evidence directory. Exclude secrets.

## Bounded run and shutdown

Add a session duration, startup/control/stop deadlines and observable terminal reason. Let the supervisor stop on success, deadline, disconnect, loss of control acknowledgement, transport coordinate rejection or stalled progress. Start with explicit, narrow completion conditions; unknown does not mean success. Manual takeover must cancel or inhibit automatic re-enabling of Bot.

A normal stop must request Manual, wait for server confirmation, request logout, wait for logout/disconnect, flush evidence and exit. Report each step separately and return nonzero when the stop cannot be confirmed. A crashed or disconnected client cannot prove the bot stopped. For dependable unattended runs, Playerbots needs a bounded server-side control lease or equivalent verified disconnect behavior, so a lost supervisor cannot leave it active indefinitely. This must preserve the selected quest and earned progress.

Keep gameplay in Playerbots. The harness issues intent/control commands and observes results; it must not steer movement, manufacture quests/items, teleport or edit character state. Live acceptance and any server lease deployment need explicit scope.

## Acceptance sequence

1. Offline parser/identity resolution and launch failure paths; no network.
2. Scoped ordinary saved-profile login to the requested character; no Bot request.
3. Local control request/acknowledgement tests including stale, refused, disconnect and timeout outcomes.
4. Scoped bounded Bot run followed by confirmed Manual and logout.
5. Prove watchdog/lease expiry stops control after client/supervisor loss before leaving an unattended test running.
