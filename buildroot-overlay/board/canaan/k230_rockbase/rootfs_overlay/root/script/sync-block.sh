#!/bin/sh
# ============================================================
# sync-block.sh — K230 区块数据同步脚本
# 将本地 bitcoind 区块数据通过 rsync 推送到另一台 K230
#
# 用法: /root/script/sync-block.sh
#       按提示输入本地源目录、目标设备 IP、目标设备区块目录
#       任一目录直接回车使用默认值
# ============================================================

set -e

DEFAULT_LOCAL_DATADIR="/root/block"
LOCAL_DATADIR="$DEFAULT_LOCAL_DATADIR"
DEFAULT_REMOTE_DATADIR="/root/block"
REMOTE_DATADIR="$DEFAULT_REMOTE_DATADIR"

# ---- 颜色 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo ""
echo "============================================"
echo "  K230 区块数据同步脚本"
echo "============================================"
echo ""

shell_quote() {
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

# ============================================================
# 1. 输入本地源目录 / 目标 IP / 目标路径
# ============================================================
printf "请输入本地区块源路径 [默认: %s]: " "$DEFAULT_LOCAL_DATADIR"
read -r LOCAL_DATADIR_INPUT
if [ -n "$LOCAL_DATADIR_INPUT" ]; then
    LOCAL_DATADIR="$LOCAL_DATADIR_INPUT"
fi

printf "请输入目标设备 IP 地址: "
read -r TARGET_IP
if [ -z "$TARGET_IP" ]; then
    echo -e "${RED}错误: 必须输入目标 IP${NC}"
    exit 1
fi

printf "请输入目标设备区块路径 [默认: %s]: " "$DEFAULT_REMOTE_DATADIR"
read -r REMOTE_DATADIR_INPUT
if [ -n "$REMOTE_DATADIR_INPUT" ]; then
    REMOTE_DATADIR="$REMOTE_DATADIR_INPUT"
fi

REMOTE_DATADIR_Q=$(shell_quote "$REMOTE_DATADIR")
echo "本地源路径: $LOCAL_DATADIR"
echo "目标: $TARGET_IP"
echo "目标路径: $REMOTE_DATADIR"
echo ""

# ============================================================
# 2. 校验本地 bitcoind 已退出
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " [1/4] 检查本地 bitcoind 状态"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if pidof bitcoind > /dev/null 2>&1; then
    echo -e "  ${RED}✗ 本地 bitcoind 仍在运行！${NC}"
    echo ""
    echo "  请先停止: bitcoin-cli -datadir=$LOCAL_DATADIR stop"
    exit 1
fi
echo -e "  ${GREEN}✓${NC} 本地 bitcoind 已停止"
echo ""

# ============================================================
# 3. 测试到目标的 SSH 连通性
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " [2/4] 检查 SSH 连接 (root@$TARGET_IP)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

SSH_KNOWN_HOSTS="${TMPDIR:-/tmp}/sync-block-known_hosts.$$"
trap 'rm -f "$SSH_KNOWN_HOSTS"' EXIT INT TERM
if command -v sshpass >/dev/null 2>&1; then
    export SSHPASS=""
    SSH_AUTH="sshpass -e"
else
    SSH_AUTH=""
fi
SSH_CMD="$SSH_AUTH ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=$SSH_KNOWN_HOSTS -o GlobalKnownHostsFile=/dev/null"

SSH_ERR=$(mktemp)
if ! $SSH_CMD root@"$TARGET_IP" "echo OK" > /dev/null 2>"$SSH_ERR"; then
    echo -e "  ${RED}✗ 无法 SSH 连接到 $TARGET_IP${NC}"
    [ -s "$SSH_ERR" ] && sed 's/^/    /' "$SSH_ERR"
    echo ""
    echo "  请检查:"
    echo "    1. 目标设备是否开机通电"
    echo "    2. 网络是否连通 (ping $TARGET_IP)"
    echo "    3. 目标 SSH 服务是否启动"
    rm -f "$SSH_ERR"
    exit 1
fi
rm -f "$SSH_ERR"
echo -e "  ${GREEN}✓${NC} SSH 连接正常"
echo ""

# ============================================================
# 4. 校验远程 bitcoind 已退出
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " [3/4] 检查远程 bitcoind 状态"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 用 grep -q 检测，避免 grep -c 无匹配时退出码=1 被 || 误判
if $SSH_CMD root@"$TARGET_IP" "pidof bitcoind > /dev/null 2>&1" 2>/dev/null; then
    echo -e "  ${RED}✗ 远程 bitcoind 仍在运行${NC}"
    echo ""
    echo "  请在目标设备执行: bitcoin-cli -datadir=$REMOTE_DATADIR stop"
    exit 1
fi
echo -e "  ${GREEN}✓${NC} 远程 bitcoind 已停止"
echo ""

# ============================================================
# 5. 检查本地数据 && 远程状态
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " [4/4] 检查数据完整性"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 本地数据
if [ ! -d "$LOCAL_DATADIR/blocks" ]; then
    echo -e "  ${RED}✗ 本地缺少 $LOCAL_DATADIR/blocks/${NC}"
    exit 1
fi
if [ ! -d "$LOCAL_DATADIR/chainstate" ]; then
    echo -e "  ${RED}✗ 本地缺少 $LOCAL_DATADIR/chainstate/${NC}"
    exit 1
fi
LOCAL_SIZE=$(du -sh "$LOCAL_DATADIR" | awk '{print $1}')
echo "  本地区块数据: $LOCAL_SIZE"

# 远程磁盘空间
REMOTE_FREE=$($SSH_CMD root@"$TARGET_IP" "df -k $REMOTE_DATADIR_Q | tail -1" 2>/dev/null | awk '{print $4}')
LOCAL_USED=$(du -sk "$LOCAL_DATADIR" | awk '{print $1}')

if [ -n "$REMOTE_FREE" ] && [ "$REMOTE_FREE" -gt 0 ]; then
    REMOTE_FREE_MB=$((REMOTE_FREE / 1024))
    LOCAL_USED_MB=$((LOCAL_USED / 1024))
    echo "  远程可用空间: ${REMOTE_FREE_MB}M"
    if [ "$REMOTE_FREE" -lt "$LOCAL_USED" ]; then
        echo -e "  ${RED}✗ 远程空间不足！需要 ${LOCAL_USED_MB}M，可用 ${REMOTE_FREE_MB}M${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}✓${NC} 远程空间充足"
else
    echo -e "  ${YELLOW}⚠ 无法获取远程空间信息，跳过检查${NC}"
fi

# 远程已存在数据
REMOTE_HAS_DATA=$($SSH_CMD root@"$TARGET_IP" "[ -d $REMOTE_DATADIR_Q/blocks ] && echo yes || echo no" 2>/dev/null)
if [ "$REMOTE_HAS_DATA" = "yes" ]; then
    REMOTE_SIZE=$($SSH_CMD root@"$TARGET_IP" "du -sh $REMOTE_DATADIR_Q" 2>/dev/null | awk '{print $1}')
    echo -e "  ${YELLOW}⚠ 远程已有数据: $REMOTE_SIZE (将被清空后重新拷贝)${NC}"
fi
echo ""

# ============================================================
# 6. 确认
# ============================================================
echo "============================================"
echo "  确认信息"
echo "============================================"
echo "  本地路径:  $LOCAL_DATADIR"
echo "  目标路径:  $REMOTE_DATADIR"
echo "  数据大小:  $LOCAL_SIZE"
echo "  源  →  目标"
echo "  localhost  →  $TARGET_IP"
echo "============================================"
echo ""
echo -e "  ${YELLOW}⚠ 将先清空目标端 $REMOTE_DATADIR/ 再全量拷贝${NC}"
echo ""
printf "确认开始传输? (y/N): "
read -r CONFIRM
if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
    echo "已取消"
    exit 0
fi

# ============================================================
# 7. 清空目标目录
# ============================================================
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " [5/5] 清空目标端旧数据"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  正在清空 root@$TARGET_IP:$REMOTE_DATADIR/ ..."
$SSH_CMD root@"$TARGET_IP" "find $REMOTE_DATADIR_Q -mindepth 1 -delete 2>/dev/null; mkdir -p $REMOTE_DATADIR_Q"
if [ $? -ne 0 ]; then
    echo -e "  ${RED}✗ 清空失败${NC}"
    exit 1
fi
echo -e "  ${GREEN}✓${NC} 目标目录已清空"

# ============================================================
# 8. 传输
# ============================================================
echo ""
echo "⏳ 开始传输..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  rsync -avP --no-compress"
echo "  $LOCAL_DATADIR/  →  root@$TARGET_IP:$REMOTE_DATADIR/"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

rsync -avP --no-compress \
    -e "$SSH_CMD" \
    "$LOCAL_DATADIR/" \
    root@"$TARGET_IP":"$REMOTE_DATADIR/"

RSYNC_EXIT=$?

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ $RSYNC_EXIT -eq 0 ]; then
    echo -e "  ${GREEN}✓ 传输完成！${NC}"
    echo ""
    echo "  接下来在目标设备 ($TARGET_IP) 启动 bitcoind:"
    echo "    bitcoind -conf=/root/config/bitcoin.conf -datadir=$REMOTE_DATADIR -daemon"
else
    echo -e "  ${RED}✗ 传输失败，rsync 退出码: $RSYNC_EXIT${NC}"
    echo ""
    echo "  可重新运行脚本:"
    echo "    /root/script/sync-block.sh"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
