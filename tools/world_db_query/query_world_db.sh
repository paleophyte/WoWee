#!/bin/bash
# Runs a single read-only SELECT against the CMaNGOS world database.
#
# Exists so mangos-helper can query the world DB (e.g. gameobject/
# gameobject_template for road signposts, per BOT_PLAN's road-survey work)
# without ever being handed the DB credentials directly - this script reads
# them out of mangosd.conf itself (which mangos-helper cannot read - it's
# owned by josh/root) and connects internally. The sudoers grant should
# only permit executing this exact script path, as root, matching the
# scoping already used for MoveMapGen.
#
# Deploy: place this file somewhere root-owned and not writable by
# mangos-helper (e.g. /home/josh/mangos-tbc/tools/query_world_db.sh),
# chmod 750 root:root, then grant mangos-helper sudo NOPASSWD execution of
# that exact path (see mangos-helper.sudoers in the WoWee repo).
#
# Usage: sudo /path/to/query_world_db.sh "SELECT ... FROM gameobject ..."
set -euo pipefail

CONF=/home/josh/mangos-tbc/run/etc/mangosd.conf

if [ "$#" -ne 1 ]; then
    echo "usage: $0 \"SELECT ...\"" >&2
    exit 2
fi
QUERY="$1"

# Only a single, plain SELECT - no writes, no stacked statements, no
# filesystem access via LOAD_FILE/INTO OUTFILE.
NORMALIZED=$(echo "$QUERY" | tr '[:upper:]' '[:lower:]')
if [[ "$NORMALIZED" != select* ]]; then
    echo "ERROR: only SELECT statements are allowed" >&2
    exit 1
fi
if echo "$NORMALIZED" | grep -qE '\b(insert|update|delete|drop|alter|create|grant|revoke|truncate|replace|call|into[[:space:]]+outfile|into[[:space:]]+dumpfile|load_file)\b'; then
    echo "ERROR: query contains a disallowed keyword" >&2
    exit 1
fi
# tr, not grep -o|wc -l, so a query with zero semicolons (the common case)
# doesn't trip grep's "no match" exit 1 and silently kill the script under
# set -e before this check ever runs.
SEMI_COUNT=$(tr -dc ';' <<< "$QUERY" | wc -c)
if [ "$SEMI_COUNT" -gt 1 ]; then
    echo "ERROR: only a single statement is allowed" >&2
    exit 1
fi

if [ ! -r "$CONF" ]; then
    echo "ERROR: cannot read $CONF" >&2
    exit 1
fi

LINE=$(grep -E '^\s*WorldDatabaseInfo\s*=' "$CONF" | head -1 || true)
if [ -z "$LINE" ]; then
    echo "ERROR: WorldDatabaseInfo not found in $CONF" >&2
    exit 1
fi
# Format: WorldDatabaseInfo = "host;port;user;pass;dbname"
INFO=$(echo "$LINE" | sed -E 's/^[^"]*"([^"]*)".*/\1/')
DBHOST=$(echo "$INFO" | cut -d';' -f1)
DBPORT=$(echo "$INFO" | cut -d';' -f2)
DBUSER=$(echo "$INFO" | cut -d';' -f3)
DBPASS=$(echo "$INFO" | cut -d';' -f4)
DBNAME=$(echo "$INFO" | cut -d';' -f5)

mysql -h "$DBHOST" -P "$DBPORT" -u "$DBUSER" -p"$DBPASS" "$DBNAME" -e "$QUERY"
