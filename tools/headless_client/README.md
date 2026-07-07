# WoWee Headless Client

`wowee_headless` is a minimal terminal/world client for automation. It logs into a WoW-compatible auth server, selects a realm and character, enters the world, then exposes a small localhost HTTP API for chat.

## Settings

Copy `settings.example.json` to `settings.json` and fill in:

- `auth.host`, `auth.port`, `auth.account`, `auth.password`
- `client.*` for the client version/build. WotLK 3.3.5a defaults are already set.
- `client.expansion`: `classic`, `tbc`, `wotlk`, or `turtle`
- `realm.name` or `realm.index`
- `character.name`
- `bots.names` for automatic `.bot add <name>` commands after world entry
- `automation.onEnterWorldCommands` for arbitrary chat/GM commands after world entry
- `api.bind` and `api.port`

Run:

```bash
wowee_headless tools/headless_client/settings.json
```

While running, press `Esc` to request an in-game logout and exit after the server responds. Press `Ctrl-C` to quit the process immediately.

Example bot startup:

```json
{
  "bots": {
    "enabled": true,
    "names": ["Soulweaver", "Leatherfang"]
  },
  "automation": {
    "commandDelaySeconds": 0.25,
    "onEnterWorldCommands": [".bot add Wildbrew"]
  }
}
```

Both sections are optional. `bots.names` expands to `.bot add <name>`, while `automation.onEnterWorldCommands` sends each command exactly as written.

## Emote Text

For friendlier `TEXT_EMOTE` messages, the headless client can load emote strings from extracted client DBC files. Provide these files in either `Data/DBFilesClient/` or `Data/db/`:

- `EmotesText.dbc`
- `EmotesTextData.dbc`
- `Emotes.dbc`

If `Data/manifest.json` exists from `asset_extract`, the normal asset manifest is used. If not, `wowee_headless` falls back to a DBC-only mode and still checks the loose DBC paths above. Without these files, it uses a small built-in fallback table.

## API

Status:

```bash
curl http://127.0.0.1:8787/status
```

Read chat:

```bash
curl "http://127.0.0.1:8787/chat?after=0&limit=50"
```

Send say:

```bash
curl -X POST http://127.0.0.1:8787/chat \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"say\",\"message\":\"hello from automation\"}"
```

Send whisper:

```bash
curl -X POST http://127.0.0.1:8787/chat \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"whisper\",\"target\":\"Playername\",\"message\":\"hello\"}"
```

Send channel:

```bash
curl -X POST http://127.0.0.1:8787/chat \
  -H "Content-Type: application/json" \
  -d "{\"type\":\"channel\",\"target\":\"world\",\"message\":\"hello\"}"
```

Supported send types: `say`, `yell`, `whisper`, `channel`, `party`, `guild`, `raid`, `officer`.
