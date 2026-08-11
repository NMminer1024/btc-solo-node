#!/bin/sh
# Rockbase miners history API. CGI only serves the background cache.

CACHE_FILE=${MINERS_HISTORY_CACHE:-/run/rockbase/miners-history-node.json}

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store"
echo ""

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
  "source": "/root/block/rockbase-history/miners",
  "series": ["1m","5m","15m","1h","6h","1d","7d"],
  "points": []
}
EOF
