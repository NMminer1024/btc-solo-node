#!/bin/sh
# NM-miner Service Control API
#   GET  /api/service.cgi?action=state
#        -> {"bitcoind":{"running":1},"ckpool":{"running":0,"state":"waiting","wait_reason":"..."}}
#   POST /api/service.cgi?action=start&service=bitcoind|ckpool
#   POST /api/service.cgi?action=stop&service=bitcoind|ckpool
#
# start/stop run the busybox init scripts (S61bitcoind / S62ckpool).
# bitcoind stop is graceful and can take up to ~120s, so it runs in a
# nohup background worker; the frontend tracks progress via the 30s
# status refresh (bitcoin.running flips to 0).
# ckpool start is gated on rockbase_bitcoind_ready.sh (ONE_SHOT): if
# bitcoind is not ready the request returns ok=false + the concrete
# reason so the UI can explain why Stratum cannot start.

INIT_BITCOIND=/etc/init.d/S61bitcoind
INIT_CKPOOL=/etc/init.d/S62ckpool
READY_CHECK=/usr/bin/rockbase_bitcoind_ready.sh
BTC_CONF=/root/config/bitcoin.conf
BTC_DATADIR=/root/block
STATE_DIR=/run/rockbase
LOCK_DIR=/run/rockbase/svclock
STALE_LOCK_SECS=300
# User-intent marker: when the operator stops bitcoind from the web UI we
# drop this file so rockbase_supervisord.sh does not immediately restart it.
# Starting bitcoind (or a reboot) clears it. ckpool is NOT supervised, so it
# needs no marker.
BTC_USER_STOPPED_FLAG=${STATE_DIR}/bitcoind-user-stopped

json_str() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

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
                    printf '%s' "${pair#*=}"
                    return 0
                fi
                ;;
        esac
    done
    return 1
}

# --- re-entry guard: one lock dir per service, stale after STALE_LOCK_SECS ---
svc_lock() {
    svc="$1"
    lock="${LOCK_DIR}/${svc}"
    mkdir -p "${LOCK_DIR}" 2>/dev/null
    if [ -d "$lock" ]; then
        lock_ts=$(cat "${lock}/ts" 2>/dev/null || echo 0)
        now_ts=$(date +%s 2>/dev/null || echo 0)
        case "$lock_ts" in ''|*[!0-9]*) lock_ts=0 ;; esac
        if [ $((now_ts - lock_ts)) -gt "$STALE_LOCK_SECS" ]; then
            rm -rf "$lock" 2>/dev/null || true
        fi
    fi
    if ! mkdir "$lock" 2>/dev/null; then
        return 1
    fi
    date +%s > "${lock}/ts" 2>/dev/null
    return 0
}

svc_unlock() {
    rm -rf "${LOCK_DIR}/$1" 2>/dev/null || true
}

emit_state() {
    btc_running=0
    pidof bitcoind >/dev/null 2>&1 && btc_running=1
    ck_running=0
    pidof ckpool >/dev/null 2>&1 && ck_running=1
    ck_state=stopped
    ck_reason=""
    if [ "$ck_running" = "1" ]; then
        ck_state=running
    else
        BTC_CONF="$BTC_CONF" BTC_DATADIR="$BTC_DATADIR" \
            STATE_DIR="${STATE_DIR}/svc-readycheck" ONE_SHOT=1 RPC_TIMEOUT=5 \
            "$READY_CHECK" >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            ck_state=stopped
        else
            ck_state=waiting
            ck_reason=$(cat "${STATE_DIR}/svc-readycheck/bitcoind-sync-wait.reason" 2>/dev/null)
            [ -n "$ck_reason" ] || ck_reason=$(cat /run/rockbase/bitcoind-sync-wait.reason 2>/dev/null)
            [ -n "$ck_reason" ] || ck_reason="bitcoind is not ready"
        fi
    fi
    printf '{"bitcoind":{"running":%s},"ckpool":{"running":%s,"state":"%s","wait_reason":"%s"}}' \
        "$btc_running" "$ck_running" "$ck_state" "$(json_str "$ck_reason")"
}

do_start() {
    svc="$1"
    case "$svc" in
        bitcoind)
            if pidof bitcoind >/dev/null 2>&1; then
                rm -f "${BTC_USER_STOPPED_FLAG}" 2>/dev/null
                printf '{"ok":true,"already":true}'
                return 0
            fi
            if ! svc_lock bitcoind; then
                printf '{"ok":false,"error":"bitcoind start already in progress"}'
                return 0
            fi
            # operator wants it back up: clear the user-stopped marker so the
            # supervisor resumes guarding it
            rm -f "${BTC_USER_STOPPED_FLAG}" 2>/dev/null
            # Prefer the init script (handles resize_done / blockdata mount /
            # clock gates). Fall back to a direct daemon launch on boards
            # that ship without S61bitcoind.
            if [ -x "$INIT_BITCOIND" ]; then
                out=$("$INIT_BITCOIND" start 2>&1)
                rc=$?
            else
                out=$(bitcoind -conf="$BTC_CONF" -datadir="$BTC_DATADIR" -daemon 2>&1)
                rc=$?
                sleep 2
            fi
            svc_unlock bitcoind
            if [ $rc -eq 0 ] && pidof bitcoind >/dev/null 2>&1; then
                printf '{"ok":true}'
            else
                printf '{"ok":false,"error":"%s"}' "$(json_str "$out")"
            fi
            ;;
        ckpool)
            if pidof ckpool >/dev/null 2>&1; then
                printf '{"ok":true,"already":true}'
                return 0
            fi
            # gate: bitcoind must be ready, otherwise explain why
            BTC_CONF="$BTC_CONF" BTC_DATADIR="$BTC_DATADIR" \
                STATE_DIR="${STATE_DIR}/svc-readycheck" ONE_SHOT=1 RPC_TIMEOUT=15 \
                "$READY_CHECK" >/dev/null 2>&1
            if [ $? -ne 0 ]; then
                reason=$(cat "${STATE_DIR}/svc-readycheck/bitcoind-sync-wait.reason" 2>/dev/null)
                [ -n "$reason" ] || reason="bitcoind is not ready"
                printf '{"ok":false,"error":"not_ready","reason":"%s"}' "$(json_str "$reason")"
                return 0
            fi
            if ! svc_lock ckpool; then
                printf '{"ok":false,"error":"ckpool start already in progress"}'
                return 0
            fi
            out=$("$INIT_CKPOOL" start 2>&1)
            rc=$?
            svc_unlock ckpool
            if [ $rc -eq 0 ] && pidof ckpool >/dev/null 2>&1; then
                printf '{"ok":true}'
            else
                printf '{"ok":false,"error":"%s"}' "$(json_str "$out")"
            fi
            ;;
        *)
            printf '{"ok":false,"error":"unknown service"}'
            ;;
    esac
}

do_stop() {
    svc="$1"
    case "$svc" in
        bitcoind)
            if ! pidof bitcoind >/dev/null 2>&1; then
                printf '{"ok":true,"already":true}'
                return 0
            fi
            if ! svc_lock bitcoind; then
                printf '{"ok":false,"error":"bitcoind stop already in progress"}'
                return 0
            fi
            # mark operator intent so rockbase_supervisord.sh leaves it down
            mkdir -p "${STATE_DIR}" 2>/dev/null
            date +%s > "${BTC_USER_STOPPED_FLAG}" 2>/dev/null
            # graceful stop can take up to ~120s; run async so the HTTP
            # request returns immediately. Pick the best stop entry point:
            # /root/bitcoind/stop.sh (present on all boards) > init script >
            # raw bitcoin-cli stop. Args: $1=lock $2=init $3=conf $4=datadir
            nohup sh -c '
                lock="$1"; init="$2"; conf="$3"; datadir="$4"
                if [ -x /root/bitcoind/stop.sh ]; then
                    /root/bitcoind/stop.sh >/dev/null 2>&1
                elif [ -x "$init" ]; then
                    "$init" stop >/dev/null 2>&1
                else
                    bitcoin-cli -conf="$conf" -datadir="$datadir" stop >/dev/null 2>&1
                fi
                rm -rf "$lock"
            ' sh "${LOCK_DIR}/bitcoind" "$INIT_BITCOIND" "$BTC_CONF" "$BTC_DATADIR" >/dev/null 2>&1 &
            printf '{"ok":true,"async":true}'
            ;;
        ckpool)
            if ! pidof ckpool >/dev/null 2>&1; then
                # still kill the auto-start waiter if one is running
                "$INIT_CKPOOL" stop >/dev/null 2>&1
                printf '{"ok":true,"already":true}'
                return 0
            fi
            if ! svc_lock ckpool; then
                printf '{"ok":false,"error":"ckpool stop already in progress"}'
                return 0
            fi
            out=$("$INIT_CKPOOL" stop 2>&1)
            rc=$?
            svc_unlock ckpool
            if [ $rc -eq 0 ]; then
                printf '{"ok":true}'
            else
                printf '{"ok":false,"error":"%s"}' "$(json_str "$out")"
            fi
            ;;
        *)
            printf '{"ok":false,"error":"unknown service"}'
            ;;
    esac
}

get_params
action=$(get_param action)
service=$(get_param service)

# busybox httpd CGI mode: the script must emit a full header block (terminated
# by a blank line) before the body, otherwise the body is parsed as headers
# and dropped. status.cgi does the same.
echo "Content-Type: application/json; charset=utf-8"
echo ""

case "$action" in
    state)
        emit_state
        ;;
    start|stop)
        if [ "$REQUEST_METHOD" != "POST" ]; then
            printf '{"ok":false,"error":"POST required"}'
        elif [ "$action" = "start" ]; then
            do_start "$service"
        else
            do_stop "$service"
        fi
        ;;
    *)
        printf '{"ok":false,"error":"unknown action"}'
        ;;
esac
