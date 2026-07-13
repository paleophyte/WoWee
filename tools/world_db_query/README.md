# World DB Query

A narrowly-scoped wrapper letting `mangos-helper` run read-only `SELECT`
queries against the CMaNGOS world database, without ever being handed the
DB credentials directly (they live in `mangosd.conf`, which `mangos-helper`
can't read on its own).

Originally built to scrape road signpost `gameobject`/`gameobject_template`
rows for road-network mapping (see BOT_PLAN.md), but usable for any
read-only investigative query against the world DB.

## Deploy

Initial setup (manual, on the server - not done by the agent):

1. Copy `query_world_db.sh` to the server, e.g.:
   ```
   scp query_world_db.sh you@server:/home/josh/mangos-tbc/tools/query_world_db.sh
   ```
2. Make it root-owned and not writable by `mangos-helper`:
   ```
   sudo chown root:root /home/josh/mangos-tbc/tools/query_world_db.sh
   sudo chmod 750 /home/josh/mangos-tbc/tools/query_world_db.sh
   ```
3. Install the sudoers grant (see `mangos-helper.sudoers` in the repo root,
   `PROJECT_WORLD_DB_QUERY` and `PROJECT_INSTALL` aliases):
   ```
   sudo visudo -f /etc/sudoers.d/mangos-helper
   ```

After that initial setup, `mangos-helper` can redeploy this one script on
its own (fixes, tweaks) via the scoped `PROJECT_INSTALL` grant:
```
scp query_world_db.sh mangos-helper@server:/tmp/query_world_db.sh
ssh mangos-helper@server "sudo install -m 0750 -o root -g root /tmp/query_world_db.sh /home/josh/mangos-tbc/tools/query_world_db.sh"
```
Only this exact source/destination pair is permitted - not a general
file-install grant.

## Usage

```
sudo /home/josh/mangos-tbc/tools/query_world_db.sh "SELECT gt.name, go.map, go.position_x, go.position_y, go.position_z FROM gameobject go JOIN gameobject_template gt ON go.id = gt.entry WHERE go.map = 0 AND gt.name LIKE '%Sign%' LIMIT 100"
```

Only single `SELECT` statements are allowed - the script rejects anything
containing write keywords (`INSERT`/`UPDATE`/`DELETE`/`DROP`/etc.),
`LOAD_FILE`/`INTO OUTFILE`, or a stacked second statement.
