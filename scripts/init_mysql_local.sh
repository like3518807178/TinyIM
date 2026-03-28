#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCHEMA_FILE="$ROOT_DIR/sql/schema.sql"

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-root123}"

if ! command -v mysql >/dev/null 2>&1; then
  echo "[ERROR] mysql client not found"
  exit 1
fi

if [[ ! -f "$SCHEMA_FILE" ]]; then
  echo "[ERROR] schema file not found: $SCHEMA_FILE"
  exit 1
fi

echo "[INFO] applying schema to $MYSQL_HOST:$MYSQL_PORT as $MYSQL_USER"
mysql -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" < "$SCHEMA_FILE"

echo "[INFO] verify tables in tinyim"
mysql -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" -e "USE tinyim; SHOW TABLES;"

echo "[OK] Local MySQL initialized successfully"
