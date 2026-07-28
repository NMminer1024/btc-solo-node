#!/bin/sh
# Rockbase miners API. Keep CGI path cheap: only serve the background cache.

CACHE_FILE=${CACHE_FILE:-/run/rockbase/miners.json}
STALE_SECONDS=${MINERS_STALE_SECONDS:-300}

json_str() {
  printf '"'
  printf '%s' "$1" \
    | tr '\r' '\n' \
    | tr -d '\000-\010\013\014\016-\037' \
    | sed 's/\\/\\\\/g; s/"/\\"/g' \
    | tr -d '\n'
  printf '"'
}

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store"
echo ""

if [ -r "$CACHE_FILE" ]; then
  cat "$CACHE_FILE"
  exit 0
fi

now=$(date +%s)
printf '{\n'
printf '  "ok": false,\n'
printf '  "generated_at": %s,\n' "$now"
printf '  "next_refresh_seconds": 60,\n'
printf '  "offline_threshold_seconds": 1800,\n'
printf '  "stale_threshold_seconds": %s,\n' "$STALE_SECONDS"
printf '  "error": %s,\n' "$(json_str "miners cache is warming up")"
printf '  "source": {"pool_status": "/root/ckpool/logs/pool/pool.status", "users_dir": "/root/ckpool/logs/users"},\n'
printf '  "pool": {"available": false, "stale": true, "users": 0, "workers": 0, "accepted": 0, "rejected": 0, "bestshare": 0},\n'
printf '  "miners": []\n'
printf '}\n'
