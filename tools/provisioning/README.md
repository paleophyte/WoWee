# WoWee Provisioning Tools

These scripts create CMaNGOS, AzerothCore, or VMangos test accounts and characters for headless automation.

## Account Creation

Use SOAP/server commands instead of direct SQL inserts. Modern CMaNGOS and AzerothCore account rows include SRP verifier/salt fields, and the server already knows how to generate them correctly.

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

For AzerothCore, you can either add AC-specific values or rely on the same SSH host/key values as `MANGOS_*`. AzerothCore SOAP uses the `urn:AC` namespace and is normally kept bound to `127.0.0.1`:

```dotenv
AC_HOST=10.0.0.10
AC_PORT=22
AC_USER=mangos
AC_SSH_KEY_PATH=C:\Users\admin\.ssh\mangos_codex
AC_SOAP_URL=http://127.0.0.1:7879/
AC_SOAP_USERNAME=SERVERADMIN
AC_SOAP_PASSWORD=GMPASSWORD
```

For VMangos, the helper can reuse the same SSH host/key and SOAP admin values as `MANGOS_*`, but defaults SOAP to the VMangos world server port:

```dotenv
VMANGOS_SOAP_URL=http://127.0.0.1:7880/
```

If `VMANGOS_SOAP_USERNAME` is not set, the helper uses `SERVERADMIN` for VMangos and can still reuse `MANGOS_SOAP_PASSWORD`. VMangos enforces the older 16-character password limit; when the VMangos profile reads a shared env SOAP password, it uses the first 16 characters.

VMangos' administrator level is `6` in this build. The SOAP admin account must be level `6` or higher for SOAP commands; normal bot roster accounts still default to moderator level `1`.

Create or update an account:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password bot-password \
  --expansion 1
```

Create or update an AzerothCore account:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --server-type azerothcore \
  --password bot-password \
  --expansion 2
```

New accounts default to GM level 1 (Moderator) so automation can run moderator-only bot commands. Pass `--gmlevel 0` when you need a regular player account:

```bash
python tools/provisioning/create_account_ssh.py BOT001 \
  --password bot-password \
  --expansion 1 \
  --gmlevel 1
```

The script runs the server's account commands:

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

For TBC on CMaNGOS, `--expansion 1` is normally the useful account expansion value. For AzerothCore WotLK, use `--server-type azerothcore`; it defaults account expansion to `2`. For VMangos Vanilla, use `--server-type vmangos`; it defaults account expansion to `0`.

### GM Level for Bot Accounts

Bot fleet accounts need GM level 1+ to run moderator-only bot commands. The account helpers default to `--gmlevel 1`. The `--realm-id` argument (default `-1`, all realms) controls which realm gets the GM assignment. Pass `--gmlevel 0` to keep the account as a regular player.

### Direct SOAP / Provisioning Service

`create_account_direct_soap.py` is kept for cases where the SOAP endpoint is reachable from the machine running WoWee, or where you want to run a small HTTP provisioning service that forwards to SOAP.

Requires `defusedxml` (`pip install defusedxml`) to parse SOAP responses safely.

Enable SOAP in your CMaNGOS world server config, create or choose a GM account that is allowed to run SOAP commands, then run:

```bash
python tools/provisioning/create_account_direct_soap.py create \
  --server-type cmangos \
  --soap-url http://127.0.0.1:7878/ \
  --admin-user GMACCOUNT \
  --admin-pass GMPASSWORD \
  --account BOT001 \
  --password bot-password \
  --expansion 1
```

For AzerothCore direct SOAP, pass `--server-type azerothcore`. For VMangos direct SOAP, pass `--server-type vmangos` and the VMangos SOAP URL, normally `http://127.0.0.1:7880/` from the server host.

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
    "serverType": "cmangos",
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

Roster provisioning defaults to `gmlevel: 1`. Set `gmlevel` in `defaults` for all accounts, or override per-account. Set `realmId` (default `-1`, all realms) to target a specific realm for the GM assignment. Set `gmlevel` to `0` to keep an account as a regular player.

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

To target AzerothCore from the command line:

```bash
python tools/provisioning/provision_roster.py tools/provisioning/roster.json \
  --server-type azerothcore \
  --auth-host 10.102.172.4
```

To target VMangos from the command line:

```bash
python tools/provisioning/provision_roster.py tools/provisioning/roster.json \
  --server-type vmangos \
  --auth-host 10.102.172.4
```

When `serverType` is `azerothcore`, or when `--server-type azerothcore` is passed, roster provisioning defaults to account expansion `2`, GM level `1`, auth port `3725`, realm `AzerothCore`, client expansion `wotlk`, and client version `3.3.5.12340`. When `serverType` is `vmangos`, roster provisioning defaults to account expansion `0`, GM level `1`, auth port `3726`, realm `VMangos`, client expansion `classic`, and client version `1.12.1.5875`. CMaNGOS defaults remain expansion `1`, GM level `1`, auth port `3724`, realm `MaNGOS`, client expansion `tbc`, and client version `2.4.3.8606`. The CLI flag overrides the roster default for quick retargeting.
