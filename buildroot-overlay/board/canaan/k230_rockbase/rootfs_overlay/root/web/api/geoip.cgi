#!/bin/sh
# geoip.cgi - offline IP geolocation via local GeoLite2-City.mmdb.
#
#   GET /api/geoip.cgi?ips=1.2.3.4,5.6.7.8
#
# Response:
#   {"ok":true,"results":{"1.2.3.4":{"lat":..,"lon":..,"city":"..","country":".."},...}}
#
# rockbase-geoip prints one JSON object per input IP; we re-emit them as a
# map keyed by IP. IPs not found in the database are omitted from results.

GEOIP_BIN=/usr/bin/rockbase-geoip

get_param() {
  key=$1
  old_ifs=$IFS
  IFS='&'
  set -- $QUERY_STRING
  IFS=$old_ifs
  for pair in "$@"; do
    case "$pair" in
      *=*)
        name=${pair%%=*}
        value=${pair#*=}
        if [ "$name" = "$key" ]; then
          printf '%b' "$(printf '%s' "$value" | sed 's/+/ /g; s/%/\\x/g')"
          return 0
        fi
        ;;
    esac
  done
  return 1
}

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store, no-cache, must-revalidate"
echo "Pragma: no-cache"
echo ""

# mmdb ships in rootfs (/usr/share/, updated via OTA); /root/config/ is a
# hot-fix override. rockbase-geoip resolves the same search order.
if [ ! -x "$GEOIP_BIN" ] || { [ ! -r /root/config/GeoLite2-City.mmdb ] && [ ! -r /usr/share/GeoLite2-City.mmdb ]; }; then
  printf '{"ok":false,"error":"geoip unavailable"}'
  exit 0
fi

ips=$(get_param ips 2>/dev/null || true)
[ -n "$ips" ] || { printf '{"ok":true,"results":{}}'; exit 0; }

# Cap at 200 IPs and one per line for the tool.
lines=$(printf '%s' "$ips" | tr ',' '\n' | sed 's/[[:space:]]//g' | grep -E '^[0-9a-fA-F:.]+$' | head -n 200)
[ -n "$lines" ] || { printf '{"ok":true,"results":{}}'; exit 0; }

# Build {"ip":{...},...} from the tool's one-object-per-line output.
# Each line looks like: {"ip":"1.2.3.4","lat":..,"lon":..,"city":"..","country":".."}
# or:                   {"ip":"bad","error":"not found"}
results=""
first=1
while IFS= read -r line; do
  [ -n "$line" ] || continue
  ip=$(printf '%s' "$line" | sed -nE 's/.*"ip":"([^"]*)".*/\1/p')
  [ -n "$ip" ] || continue
  # drop the leading {"ip":"..."," and the trailing }
  body=$(printf '%s' "$line" | sed -E 's/^\{"ip":"[^"]*",//; s/\}$//')
  if [ "$first" = "1" ]; then
    first=0
  else
    results="${results},"
  fi
  results="${results}\"${ip}\":{${body}}"
done <<EOF
$(printf '%s\n' "$lines" | "$GEOIP_BIN" 2>/dev/null)
EOF

printf '{"ok":true,"results":{%s}}' "$results"
