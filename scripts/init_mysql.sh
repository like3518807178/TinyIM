#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE_FILE="$ROOT_DIR/docker-compose.yml"
SCHEMA_FILE="$ROOT_DIR/sql/schema.sql"

if ! command -v docker >/dev/null 2>&1; then
  echo "[ERROR] docker command not found"
  exit 1
fi

if docker compose version >/dev/null 2>&1; then
  COMPOSE_CMD=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE_CMD=(docker-compose)
else
  echo "[ERROR] docker compose/docker-compose not found"
  exit 1
fi

if [[ ! -f "$SCHEMA_FILE" ]]; then
  echo "[ERROR] schema file not found: $SCHEMA_FILE"
  exit 1
fi

"${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" up -d mysql

echo "[INFO] waiting mysql to be ready..."
for i in {1..60}; do
  if "${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" exec -T mysql sh -lc 'mysqladmin ping -h127.0.0.1 -uroot -p"$MYSQL_ROOT_PASSWORD" --silent' >/dev/null 2>&1; then
    READY=1
    break
  fi
  sleep 1
done

if [[ "${READY:-0}" != "1" ]]; then
  echo "[ERROR] mysql is not ready after timeout"
  exit 1
fi

echo "[INFO] applying schema: $SCHEMA_FILE"
"${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" exec -T mysql sh -lc 'mysql -uroot -p"$MYSQL_ROOT_PASSWORD"' < "$SCHEMA_FILE"

echo "[INFO] verify tables in tinyim"
"${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" exec -T mysql sh -lc 'mysql -uroot -p"$MYSQL_ROOT_PASSWORD" -e "USE tinyim; SHOW TABLES;"'

echo "[OK] MySQL initialized successfully"
