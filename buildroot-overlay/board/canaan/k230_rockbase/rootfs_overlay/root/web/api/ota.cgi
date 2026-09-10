#!/bin/sh
# NM-miner OTA API
#   action=manifest  GET   fetch manifest.json from the configured OTA repo
#                          (60s cache in /run so per-minute client polls do not
#                          hammer the remote repo)
#   action=state     GET   current apply progress (rockbase-ota.sh state file)
#   action=apply     POST  start a real apply with the configured manifest URL
#                          (same nohup combo as tools/ota-push.sh)

MANIFEST_URL_FILE=/etc/rockbase-ota-manifest-url
MANIFEST_URL_DEFAULT="https://ota.nmminer.com/rockbase/manifest.json"
MANIFEST_CACHE=/run/rockbase/ota-manifest-cache.json
MANIFEST_CACHE_TTL=60
STATE_FILE=/run/rockbase/ota-state.json
APPLY_LOCKDIR=/run/rockbase/ota-apply.lock
APPLY_LOG_DIR=/root/block/ota

get_params() {
  params="$QUERY_STRING"
  if [ "$REQUEST_METHOD" = "POST" ]; then
    body=$(cat)
    if [ -n "$params" ] && [ -n "$body" ]; then
      params="${params}&${body}"
    elif [ -n "$body" ]; then
      params="$body"
    fi
  fi
}

get_param() {
  key=$1
  old_ifs=$IFS
  IFS='&'
  set -- $params
  IFS=$old_ifs
  for pair in "$@"; do
    case "$pair" in
      *=*)
        if [ "${pair%%=*}" = "$key" ]; then
          printf '%b' "$(printf '%s' "${pair#*=}" | sed 's/+/ /g; s/%/\\x/g')"
          return 0
        fi
        ;;
    esac
  done
  return 1
}

manifest_url() {
  url=""
  if [ -r "$MANIFEST_URL_FILE" ]; then
    url=$(sed -n '1p' "$MANIFEST_URL_FILE" | tr -d ' \t\r\n')
  fi
  [ -n "$url" ] || url="$MANIFEST_URL_DEFAULT"
  printf '%s\n' "$url"
}

emit_manifest() {
  mkdir -p /run/rockbase 2>/dev/null
  now=$(date +%s)
  stamp=0
  if [ -f "$MANIFEST_CACHE.ts" ]; then
    stamp=$(cat "$MANIFEST_CACHE.ts" 2>/dev/null || echo 0)
  fi
  case "$stamp" in ''|*[!0-9]*) stamp=0 ;; esac
  if [ -f "$MANIFEST_CACHE" ] && [ $((now - stamp)) -lt "$MANIFEST_CACHE_TTL" ]; then
    cat "$MANIFEST_CACHE"
    return 0
  fi
  if curl --cacert /etc/ssl/certs/ca-certificates.crt \
          --max-time 15 -fsSL -o "$MANIFEST_CACHE.tmp" "$(manifest_url)" 2>/dev/null; then
    mv "$MANIFEST_CACHE.tmp" "$MANIFEST_CACHE"
    echo "$now" > "$MANIFEST_CACHE.ts"
    cat "$MANIFEST_CACHE"
    return 0
  fi
  rm -f "$MANIFEST_CACHE.tmp"
  # serve stale cache rather than nothing when the repo is unreachable
  if [ -f "$MANIFEST_CACHE" ]; then
    cat "$MANIFEST_CACHE"
    return 0
  fi
  printf '{"ok":false,"error":"manifest unavailable"}'
  return 1
}

emit_state() {
  applying=false
  if [ -d "$APPLY_LOCKDIR" ]; then
    lock_pid=$(cat "$APPLY_LOCKDIR/pid" 2>/dev/null || true)
    if [ -n "$lock_pid" ] && kill -0 "$lock_pid" 2>/dev/null; then
      applying=true
    fi
  fi
  if [ -f "$STATE_FILE" ]; then
    printf '{"ok":true,"applying":%s,"state":%s}' "$applying" "$(cat "$STATE_FILE")"
  else
    printf '{"ok":true,"applying":%s,"state":null}' "$applying"
  fi
}

start_apply() {
  if [ "$REQUEST_METHOD" != "POST" ]; then
    printf '{"ok":false,"error":"POST required"}'
    return 1
  fi
  if [ -d "$APPLY_LOCKDIR" ]; then
    lock_pid=$(cat "$APPLY_LOCKDIR/pid" 2>/dev/null || true)
    if [ -n "$lock_pid" ] && kill -0 "$lock_pid" 2>/dev/null; then
      printf '{"ok":false,"error":"apply already running","applying":true}'
      return 1
    fi
    rm -rf "$APPLY_LOCKDIR" 2>/dev/null || true
  fi
  mkdir -p "$APPLY_LOG_DIR" 2>/dev/null
  nohup rockbase-ota.sh apply "$(manifest_url)" --force \
    > "$APPLY_LOG_DIR/apply-web-$(date +%Y%m%d-%H%M%S).log" 2>&1 &
  printf '{"ok":true,"started":true,"pid":%s}' "$!"
}

get_params
action=$(get_param action 2>/dev/null || true)
[ -n "$action" ] || action=state

echo "Content-Type: application/json; charset=utf-8"
echo ""

case "$action" in
  manifest) emit_manifest ;;
  state)    emit_state ;;
  apply)    start_apply ;;
  *)        printf '{"ok":false,"error":"unknown action"}' ;;
esac
