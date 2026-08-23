#!/bin/sh
#
# rockbase_bitcoind_ready.sh - Gate ckpool startup on a clear bitcoind sync signal.
#

BTC_CONF=${BTC_CONF:-/root/config/bitcoin.conf}
BTC_DATADIR=${BTC_DATADIR:-/root/block}
STATE_DIR=${STATE_DIR:-/run/rockbase}
READY_FILE=${READY_FILE:-${STATE_DIR}/bitcoind-synced}
REASON_FILE=${REASON_FILE:-${STATE_DIR}/bitcoind-sync-wait.reason}
RPC_TIMEOUT=${RPC_TIMEOUT:-15}
MAX_HEADER_LAG=${MAX_HEADER_LAG:-2}
MAX_BLOCK_AGE=${MAX_BLOCK_AGE:-7200}
MIN_CONNECTIONS=${MIN_CONNECTIONS:-2}
STABLE_FILE=${STABLE_FILE:-${STATE_DIR}/bitcoind-ready-streak}
REQUIRED_STREAK=${REQUIRED_STREAK:-2}

mkdir -p "${STATE_DIR}"

json_num() {
    key="$1"
    sed -n "s/.*\"${key}\": *\\([0-9][0-9]*\\).*/\\1/p" | head -n1
}

json_bool() {
    key="$1"
    sed -n "s/.*\"${key}\": *\\(true\\|false\\).*/\\1/p" | head -n1
}

write_wait() {
    reason="$1"
    rm -f "${READY_FILE}"
    echo "${reason}" > "${REASON_FILE}"
    echo 0 > "${STABLE_FILE}"
    echo "${reason}"
    exit 1
}

pidof bitcoind >/dev/null 2>&1 || write_wait "bitcoind process is not running"

info=$(bitcoin-cli -rpcclienttimeout="${RPC_TIMEOUT}" -conf="${BTC_CONF}" -datadir="${BTC_DATADIR}" getblockchaininfo 2>/dev/null) \
    || write_wait "bitcoind RPC is not responding"
netinfo=$(bitcoin-cli -rpcclienttimeout="${RPC_TIMEOUT}" -conf="${BTC_CONF}" -datadir="${BTC_DATADIR}" getnetworkinfo 2>/dev/null) \
    || write_wait "bitcoind network RPC is not responding"

blocks=$(printf '%s\n' "${info}" | json_num blocks)
headers=$(printf '%s\n' "${info}" | json_num headers)
best_time=$(printf '%s\n' "${info}" | json_num time)
ibd=$(printf '%s\n' "${info}" | json_bool initialblockdownload)
connections=$(printf '%s\n' "${netinfo}" | json_num connections)
now=$(date +%s 2>/dev/null || echo 0)

[ -n "${blocks}" ] || write_wait "bitcoind blocks height is unavailable"
[ -n "${headers}" ] || write_wait "bitcoind headers height is unavailable"
[ -n "${best_time}" ] || write_wait "bitcoind best block time is unavailable"
[ -n "${connections}" ] || connections=0

[ "${ibd}" = "false" ] || write_wait "bitcoind is still in initial block download"

lag=$((headers - blocks))
[ "${lag}" -lt 0 ] && lag=0
[ "${lag}" -le "${MAX_HEADER_LAG}" ] || write_wait "bitcoind header lag is ${lag} blocks (blocks=${blocks}, headers=${headers})"

age=$((now - best_time))
[ "${age}" -lt 0 ] && age=0
[ "${age}" -le "${MAX_BLOCK_AGE}" ] || write_wait "bitcoind best block is too old (${age}s)"

[ "${connections}" -ge "${MIN_CONNECTIONS}" ] || write_wait "bitcoind has only ${connections} peer connections"

streak=$(cat "${STABLE_FILE}" 2>/dev/null || echo 0)
case "${streak}" in
    ''|*[!0-9]*) streak=0 ;;
esac
streak=$((streak + 1))
echo "${streak}" > "${STABLE_FILE}"

if [ "${streak}" -lt "${REQUIRED_STREAK}" ]; then
    rm -f "${READY_FILE}"
    reason="bitcoind ready check ${streak}/${REQUIRED_STREAK} (blocks=${blocks}, headers=${headers}, lag=${lag}, age=${age}s, peers=${connections})"
    echo "${reason}" > "${REASON_FILE}"
    echo "${reason}"
    exit 1
fi

cat > "${READY_FILE}" <<EOF
ready=true
blocks=${blocks}
headers=${headers}
lag=${lag}
best_block_time=${best_time}
age=${age}
connections=${connections}
checked_at=${now}
EOF
rm -f "${REASON_FILE}"
echo "bitcoind ready (blocks=${blocks}, headers=${headers}, lag=${lag}, age=${age}s, peers=${connections})"
exit 0
