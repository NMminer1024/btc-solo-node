#!/bin/bash

set -euo pipefail

SDK_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST_DIR_DEFAULT="${SDK_ROOT}/seed/root-block"
REMOTE_DIR_DEFAULT="/root/block"

REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_USER="${REMOTE_USER:-}"
REMOTE_PASSWORD="${REMOTE_PASSWORD:-}"
REMOTE_DIR="${REMOTE_DIR:-}"
DEST_DIR="${DEST_DIR:-${DEST_DIR_DEFAULT}}"

# RDK X5 bitcoind 默认路径（snap 安装）
REMOTE_BITCOIND_PIDFILE="${REMOTE_BITCOIND_PIDFILE:-/home/sunrise/Desktop/bitcoind/data/bitcoind.pid}"
REMOTE_BITCOIND_STOP_SCRIPT="${REMOTE_BITCOIND_STOP_SCRIPT:-/home/sunrise/Desktop/bitcoind/stop.sh}"
REMOTE_BITCOIND_START_CMD="${REMOTE_BITCOIND_START_CMD:-nohup /snap/bin/bitcoin-core.daemon -conf=/home/sunrise/Desktop/bitcoind/bitcoin.conf -datadir=/home/sunrise/Desktop/bitcoind/data > /dev/null 2>&1 &}"

TMP_DIR="${DEST_DIR}.tmp"
SSH_KNOWN_HOSTS="${TMPDIR:-/tmp}/rockbase-seed-known_hosts.$$"

shell_quote() {
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

validate_manifest() {
    local db_dir="$1"
    local label="$2"
    local current_file="${db_dir}/CURRENT"
    local manifest_file

    if [ ! -f "${current_file}" ]; then
        echo "ERROR: ${label} missing CURRENT: ${current_file}" >&2
        exit 1
    fi

    manifest_file=$(tr -d '\r\n' < "${current_file}")
    if [ -z "${manifest_file}" ]; then
        echo "ERROR: ${label} CURRENT is empty: ${current_file}" >&2
        exit 1
    fi

    if [ ! -f "${db_dir}/${manifest_file}" ]; then
        echo "ERROR: ${label} manifest missing: ${db_dir}/${manifest_file}" >&2
        exit 1
    fi
}

check_dependencies() {
    local missing=()
    command -v ssh >/dev/null 2>&1 || missing+=("ssh")
    command -v rsync >/dev/null 2>&1 || missing+=("rsync")
    command -v sshpass >/dev/null 2>&1 || missing+=("sshpass")

    if [ ${#missing[@]} -gt 0 ]; then
        echo "ERROR: 缺少依赖: ${missing[*]}" >&2
        echo "请在 WSL 中安装缺少的包后再运行。" >&2
        exit 1
    fi
}

prompt_input() {
    local prompt="$1"
    local var_name="$2"
    local default_value="$3"
    local input=""

    if [ -n "${default_value}" ]; then
        printf "%s [%s]: " "$prompt" "$default_value"
    else
        printf "%s: " "$prompt"
    fi
    read -r input
    if [ -z "${input}" ]; then
        input="${default_value}"
    fi
    printf -v "$var_name" '%s' "$input"
}

prompt_password() {
    local prompt="$1"
    local var_name="$2"
    local input=""

    printf "%s: " "$prompt"
    read -r -s input
    echo
    if [ -z "${input}" ]; then
        echo "ERROR: 不能为空" >&2
        exit 1
    fi
    printf -v "$var_name" '%s' "$input"
}

check_dependencies

if [ -z "${REMOTE_HOST}" ]; then
    prompt_input "请输入远端设备 IP/主机名" REMOTE_HOST ""
fi
if [ -z "${REMOTE_USER}" ]; then
    prompt_input "请输入远端登录用户名" REMOTE_USER "root"
fi
if [ -z "${REMOTE_PASSWORD}" ]; then
    prompt_password "请输入远端登录密码" REMOTE_PASSWORD
fi

mkdir -p "$(dirname "${DEST_DIR}")"
rm -rf "${TMP_DIR}"

trap 'rm -f "${SSH_KNOWN_HOSTS}"' EXIT INT TERM

SSH_COMMON_OPTS="-o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=${SSH_KNOWN_HOSTS} -o GlobalKnownHostsFile=/dev/null"
SSH_CMD=(sshpass -p "${REMOTE_PASSWORD}" ssh ${SSH_COMMON_OPTS})

REMOTE_TARGET="${REMOTE_USER}@${REMOTE_HOST}"

echo ""
echo "============================================"
echo "  Rockbase seed 拉取脚本"
echo "============================================"
echo "  远端地址: ${REMOTE_TARGET}"
echo "  本地目录: ${DEST_DIR}"
echo ""

if ! "${SSH_CMD[@]}" "${REMOTE_TARGET}" "echo OK" >/dev/null 2>&1; then
    echo "ERROR: 无法通过 SSH 登录到远端设备，请检查主机名、用户名或密码。" >&2
    exit 1
fi

echo "✓ SSH 登录成功"

# ── 检查远端 bitcoind 是否在运行，若在则优雅停止 ──
# 注意：使用 [b] 方括号技巧避免 pgrep -f 匹配到自身 shell 进程
REMOTE_BITCOIND_WAS_RUNNING=false
if "${SSH_CMD[@]}" "${REMOTE_TARGET}" "pgrep -f '[b]itcoind.*-conf.*Desktop/bitcoind' > /dev/null 2>&1 || pgrep -f '[b]itcoin-core.daemon' > /dev/null 2>&1"; then
    REMOTE_BITCOIND_WAS_RUNNING=true
    echo ""
    echo "检测到远端 bitcoind 正在运行，正在优雅停止..."

    if "${SSH_CMD[@]}" "${REMOTE_TARGET}" "test -x $(shell_quote "${REMOTE_BITCOIND_STOP_SCRIPT}")" >/dev/null 2>&1; then
        "${SSH_CMD[@]}" "${REMOTE_TARGET}" "bash $(shell_quote "${REMOTE_BITCOIND_STOP_SCRIPT}")" || true
    else
        "${SSH_CMD[@]}" "${REMOTE_TARGET}" "bitcoin-cli -datadir=$(shell_quote "${REMOTE_DIR}") stop" 2>/dev/null || true
    fi

    echo "等待 bitcoind 退出..."
    BITCOIND_STOPPED=false
    for i in $(seq 1 60); do
        if ! "${SSH_CMD[@]}" "${REMOTE_TARGET}" "pgrep -f '[b]itcoind.*-conf.*Desktop/bitcoind' > /dev/null 2>&1 || pgrep -f '[b]itcoin-core.daemon' > /dev/null 2>&1" 2>/dev/null; then
            echo "✓ bitcoind 已退出"
            BITCOIND_STOPPED=true
            break
        fi
        printf "."
        sleep 2
    done

    # 如果还没退出，强制 kill
    if [ "${BITCOIND_STOPPED}" = false ]; then
        echo ""
        echo "bitcoind 未在 120s 内退出，发送 SIGTERM..."
        "${SSH_CMD[@]}" "${REMOTE_TARGET}" "pkill -TERM -f '[b]itcoind.*-conf.*Desktop/bitcoind' || pkill -TERM -f '[b]itcoin-core.daemon'" 2>/dev/null || true
        sleep 10
    fi
else
    echo "远端 bitcoind 未运行，直接同步"
fi

while true; do
    if [ -z "${REMOTE_DIR}" ]; then
        prompt_input "请输入远端区块数据目录" REMOTE_DIR "${REMOTE_DIR_DEFAULT}"
    fi

    if "${SSH_CMD[@]}" "${REMOTE_TARGET}" "test -d $(shell_quote "${REMOTE_DIR}")" >/dev/null 2>&1; then
        break
    fi

    echo "ERROR: 远端目录不存在: ${REMOTE_DIR}" >&2
    echo "请重新输入远端目录。" >&2
    REMOTE_DIR=""
done

echo "✓ 远端目录存在"
echo "  远端目录: ${REMOTE_DIR}"

REMOTE_MKDIR_CMD="mkdir -p $(shell_quote "${REMOTE_DIR}")"
"${SSH_CMD[@]}" "${REMOTE_TARGET}" "${REMOTE_MKDIR_CMD}" >/dev/null 2>&1

if [ -d "${DEST_DIR}" ] && [ -n "$(find "${DEST_DIR}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
    echo ""
    echo "警告: 将清空本地 seed 目录后再开始同步。"
    echo "  本地目录: ${DEST_DIR}"
    printf "确认继续? (y/N): "
    CONFIRM_CLEAR=y # auto-confirm
    if [ "${CONFIRM_CLEAR}" != "y" ] && [ "${CONFIRM_CLEAR}" != "Y" ]; then
        echo "已取消"
        exit 0
    fi
fi

echo "清空本地 seed 目录: ${DEST_DIR}"
rm -rf "${DEST_DIR}"
mkdir -p "${DEST_DIR}"

echo "开始从远端复制 seed 到本地目录..."
sshpass -p "${REMOTE_PASSWORD}" rsync -aH --delete --partial --append-verify --info=progress2 \
    --exclude='.lock' \
    --exclude='.cookie' \
    --exclude='bitcoind.pid' \
    --exclude='LOCK' \
    -e "ssh ${SSH_COMMON_OPTS}" \
    "${REMOTE_TARGET}:${REMOTE_DIR}/" "${DEST_DIR}/"

echo "✓ rsync 复制完成"

validate_manifest "${DEST_DIR}/chainstate" "copied chainstate"
validate_manifest "${DEST_DIR}/blocks/index" "copied blocks/index"

du -sh "${DEST_DIR}" "${DEST_DIR}/blocks" "${DEST_DIR}/blocks/index" "${DEST_DIR}/chainstate"

echo "Rockbase block seed 已更新: ${DEST_DIR}"

# ── 如果之前停止了 bitcoind，重新拉起 ──
if [ "${REMOTE_BITCOIND_WAS_RUNNING}" = true ]; then
    echo ""
    echo "正在重新拉起远端 bitcoind..."
    "${SSH_CMD[@]}" "${REMOTE_TARGET}" "${REMOTE_BITCOIND_START_CMD}"
    sleep 3
    if "${SSH_CMD[@]}" "${REMOTE_TARGET}" "pgrep -f '[b]itcoind.*-conf.*Desktop/bitcoind' > /dev/null 2>&1 || pgrep -f '[b]itcoin-core.daemon' > /dev/null 2>&1" 2>/dev/null; then
        echo "✓ 远端 bitcoind 已重新启动"
    else
        echo "⚠ 远端 bitcoind 可能未成功启动，请手动检查"
    fi
fi
