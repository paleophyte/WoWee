# WoWee Provisioning Tools

These scripts create CMaNGOS test accounts and characters for headless automation.

## Account Creation

Use CMaNGOS SOAP/server commands instead of direct SQL inserts. Modern CMaNGOS account rows include SRP verifier/salt fields, and the server already knows how to generate them correctly.

There are two supported account creation paths:

- Preferred for a private Linux CMaNGOS host: SSH to the server and call the server-local SOAP endpoint from there.
- Useful for local/dev setups: call SOAP directly from this machine or run the older tiny HTTP provisioning service.

### SSH + Server-Local SOAP

`create_account_ssh.py` reads server connection details from `tools/.env`, opens SSH, and runs the SOAP request locally on the CMaNGOS host. This lets `SOAP.IP` stay bound to `127.0.0.1` on the Linux box.

Create `tools/.env` locally:

```dotenv
MANGOS_HOST=10.0.0.10
MANGOS_PORT=22
MANGOS_USER=mangos
MANGOS_SSH_KEY_PATH=C:\Users\admin\.ssh\mangos_codex
MANGOS_SOAP_URL=http://127.0.0.1:7878/
MANGOS_SOAP_USERNAME=GMACCOUNT
MANGOS_SOAP_PASSWORD=GMPASSWORD
```

`tools/.env` is ignored by git.

Create or update an account:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password bot-password \
  --expansion 1
```

For bot accounts that need to use `.bot add` commands, set GM level to 1 (Moderator):

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password bot-password \
  --expansion 1 \
  --gmlevel 1
```

The script runs:

```text
account create BOT001 ********
account set addon BOT001 1
account set gmlevel BOT001 1 -1
```

Use `--dry-run` to confirm the SSH target and commands without connecting:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password bot-password \
  --expansion 1 \
  --gmlevel 1 \
  --dry-run
```

Useful options:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password bot-password \
  --expansion 1 \
  --gmlevel 1 \
  --env tools/.env \
  --soap-url http://127.0.0.1:7878/ \
  --verbose
```

If the account already exists and you only need to set the expansion:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password existing-password \
  --expansion 1 \
  --skip-create
```

For TBC, `--expansion 1` is normally the useful account expansion value.

### GM Level for Bot Accounts

Bot fleet accounts need GM level 1+ to run `.bot add` commands. Pass `--gmlevel 1` during account creation. The `--realm-id` argument (default `-1`, all realms) controls which realm gets the GM assignment. Level 0 (default) keeps the account as a regular player.

### Direct SOAP / Provisioning Service

`create_account_direct_soap.py` is kept for cases where the SOAP endpoint is reachable from the machine running WoWee, or where you want to run a small HTTP provisioning service that forwards to SOAP.

Requires `defusedxml` (`pip install defusedxml`) to parse SOAP responses safely.

Enable SOAP in your CMaNGOS world server config, create or choose a GM account that is allowed to run SOAP commands, then run:

```bash
python tools/provisioning/create_account_direct_soap.py create \
  --soap-url http://127.0.0.1:7878/ \
  --admin-user GMACCOUNT \
  --admin-pass GMPASSWORD \
  --account BOT001 \
  --password bot-password \
  --expansion 1
```

If the CMaNGOS host should expose a tiny provisioning endpoint, run this on the Linux box:

```bash
python tools/provisioning/create_account_direct_soap.py serve \
  --bind 127.0.0.1 \
  --port 8790 \
  --soap-url http://127.0.0.1:7878/ \
  --admin-user GMACCOUNT \
  --admin-pass GMPASSWORD \
  --token choose-a-long-random-token
```

Then create an account with:

```bash
curl -X POST http://127.0.0.1:8790/accounts \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer choose-a-long-random-token" \
  -d '{"account":"BOT001","password":"bot-password","expansion":1}'
```

Keep this service bound to localhost or behind your own trusted network boundary.

## Character Creation

Character creation uses `wowee_headless` against the normal auth/world servers. Start from a working headless settings file so realm, client build, expansion profile, and auth host already match your server:

```bash
python tools/provisioning/create_character.py \
  --settings tools/headless_client/settings.json \
  --wowee-headless build/bin/wowee_headless.exe \
  --account BOT001 \
  --password bot-password \
  --name Codexbotone \
  --race human \
  --class warrior \
  --gender male
```

On Windows, the wrapper automatically adds `C:/msys64/ucrt64/bin` to `PATH` when it exists so MSYS2-built runtime DLLs such as `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, and OpenSSL DLLs can be found. If your MSYS2 install is somewhere else, pass:

```bash
python tools/provisioning/create_character.py \
  --settings tools/headless_client/settings.json \
  --wowee-headless build/bin/wowee_headless.exe \
  --msys-ucrt-bin C:/path/to/msys64/ucrt64/bin \
  --account BOT001 \
  --password bot-password \
  --name Codexbotone \
  --race human \
  --class warrior
```

The script writes a temporary settings file with:

```json
{
  "provision": {
    "createCharacter": {
      "enabled": true,
      "exitAfterCreate": true
    }
  }
}
```

`wowee_headless` logs in, reaches the character list, creates the character if it does not already exist, and exits.

Supported race names: `human`, `orc`, `dwarf`, `night_elf`, `undead`, `tauren`, `gnome`, `troll`, `blood_elf`, `draenei`.

Supported TBC class names: `warrior`, `paladin`, `hunter`, `rogue`, `priest`, `shaman`, `mage`, `warlock`, `druid`.

## Batch Roster Provisioning

Use `provision_roster.py` when you are ready to create more than one account/character pair. Start by copying the example roster:

```bash
copy tools\provisioning\roster.example.json tools\provisioning\roster.json
```

`tools/provisioning/roster.json` is ignored by git because it may contain account passwords.

Example roster shape:

```json
{
  "defaults": {
    "settings": "tools/headless_client/settings.json",
    "woweeHeadless": "build/bin/wowee_headless.exe",
    "accountPassword": "change-me",
    "expansion": 1,
    "gmlevel": 1,
    "race": "human",
    "class": "warrior",
    "gender": "male"
  },
  "accounts": [
    {
      "account": "BOT001",
      "password": "change-me",
      "gmlevel": 1,
      "characters": [
        { "name": "Botone" }
      ]
    }
  ]
}
```

Set `gmlevel` in `defaults` for all accounts, or override per-account. Set `realmId` (default `-1`, all realms) to target a specific realm for the GM assignment. Omit or set to `0` to keep the account as a regular player.

Dry-run the roster first:

```bash
python tools/provisioning/provision_roster.py tools/provisioning/roster.json --dry-run
```

Then run it:

```bash
python tools/provisioning/provision_roster.py tools/provisioning/roster.json
```

Useful switches:

```bash
python tools/provisioning/provision_roster.py tools/provisioning/roster.json --skip-accounts
python tools/provisioning/provision_roster.py tools/provisioning/roster.json --skip-characters
python tools/provisioning/provision_roster.py tools/provisioning/roster.json --continue-on-error
```

By default, roster provisioning uses `create_account_ssh.py` for account creation and `create_character.py` for character creation.
