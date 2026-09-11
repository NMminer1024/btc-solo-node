#!/bin/sh
# Rockbase miners history API.
# Without query: serve the pre-rendered node (pool-level) cache.
# With ?address=<wallet>: render that wallet's per-user history on demand.

CACHE_FILE=${MINERS_HISTORY_CACHE:-/run/rockbase/miners-history-node.json}
DATA_DIR=${MINERS_HISTORY_DIR:-/root/config/rockbase-history/miners}
RETENTION_DAYS=${MINERS_HISTORY_RETENTION_DAYS:-7}
MAX_POINTS=${MINERS_HISTORY_MAX_POINTS:-720}

json_str() {
  printf '"'
  printf '%s' "$1" \
    | tr '\r' '\n' \
    | tr -d '\000-\010\013\014\016-\037' \
    | sed 's/\\/\\\\/g; s/"/\\"/g' \
    | tr -d '\n'
  printf '"'
}

json_num() {
  case "$1" in
    ''|*[!0-9.-]*) printf '%s' "${2:-0}" ;;
    *) printf '%s' "$1" ;;
  esac
}

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store"
echo ""

address=$(printf '%s' "$QUERY_STRING" | sed -n 's/.*address=\([A-Za-z0-9][A-Za-z0-9]*\).*/\1/p' | head -n1)

if [ -z "$address" ]; then
  if [ -r "$CACHE_FILE" ]; then
    cat "$CACHE_FILE"
    exit 0
  fi
  now=$(date +%s)
  cat <<EOF
{
  "ok": false,
  "generated_at": ${now},
  "retention_days": 7,
  "max_points": 720,
  "raw_points": 0,
  "sample_step": 1,
  "source": "${DATA_DIR}",
  "series": ["1m","5m","15m","1h","6h","1d","7d"],
  "points": []
}
EOF
  exit 0
fi

# Per-wallet mode: read user-<address>-*.jsonl, downsample to MAX_POINTS.
tmp_all="/run/rockbase/.miners-history-${address}.$$"
: > "$tmp_all"
trap 'rm -f "$tmp_all"' EXIT

if [ -d "$DATA_DIR" ]; then
  grep -h -v '^[[:space:]]*$' "$DATA_DIR"/user-"$address"-*.jsonl >> "$tmp_all" 2>/dev/null || true
fi

total=$(wc -l < "$tmp_all" 2>/dev/null | tr -d ' ')
case "$total" in ''|*[!0-9]*) total=0 ;; esac
step=1
if [ "$total" -gt "$MAX_POINTS" ] 2>/dev/null; then
  step=$(((total + MAX_POINTS - 1) / MAX_POINTS))
fi

{
  printf '{\n'
  printf '  "ok": %s,\n' "$([ "$total" -gt 0 ] && printf true || printf false)"
  printf '  "generated_at": %s,\n' "$(date +%s)"
  printf '  "address": %s,\n' "$(json_str "$address")"
  printf '  "retention_days": %s,\n' "$(json_num "$RETENTION_DAYS" 7)"
  printf '  "max_points": %s,\n' "$(json_num "$MAX_POINTS" 720)"
  printf '  "raw_points": %s,\n' "$total"
  printf '  "sample_step": %s,\n' "$step"
  printf '  "series": ["1m","5m","1h","1d","7d"],\n'
  printf '  "points": [\n'
  awk -v step="$step" -v total="$total" '
    $0 != "" && total > 0 && (NR % step == 0 || NR == total) {
      if (seen++) printf ",\n"
      printf "    %s", $0
    }
    END {
      if (seen) printf "\n"
    }
  ' "$tmp_all"
  printf '  ]\n'
  printf '}\n'
}
