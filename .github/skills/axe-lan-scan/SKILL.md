---
name: axe-lan-scan
description: 'Scan LAN for NM/Axe miner devices and manage their stratum pool config via AxeOS HTTP API. Use when: 扫局域网矿机, 扫描Axe, Axe设备统计, 矿机pool, 改pool, 切pool, 主池, stratum切换, swarm, probe, 矿机重启, 批量改矿机, NMAxe, NMQAxe, NMAxeGamma, 矿机列表, 哪些矿机连本地.'
---

# 局域网 Axe 矿机扫描与管理

通过 AxeOS HTTP API（端口 80，无鉴权）扫描局域网内所有 NM 兼容矿机
（NMAxe / NMAxeGamma / NMQAxe++ / NMMiner），统计数量、查看/修改 stratum pool、批量重启。

## 核心原则

- **不要依赖 `/alive`（swarm ICMP 扫描）做全量发现**：它基于 ping，不响应
  ICMP 的设备会被漏掉（实测 17/25）。**直接对全段 IP 并发打 `GET /probe`
  才是全量**（HTTP 探测，254 个 IP 并发 64 线程约 10 秒扫完）。
- **NM 设备判定标准**：`/probe` 响应中同时含 `hr` 和 `ver` 两个键。
- **改 pool 只动 `primary`**：保留原 `user`/`pwd` 和 `fallback`，PATCH 时把
  读到的完整 stratum 结构原样带回，只替换 `primary.url`。
- **stratum 变更需要重启才生效**：PATCH 后必须 `POST /api/system/restart`。
- **重启后 hashrate 显示 0 是正常的**：hr 是 3 分钟滚动平均，刚重启无样本。
- **改完必须全段重扫验证**：确认没有设备仍指向旧 pool。

## 关键 API（Base URL: `http://<device-ip>`，JSON，无鉴权）

| 端点 | 方法 | 用途 |
|------|------|------|
| `/probe` | GET | 轻量识别：返回 `model`/`hostname`/`ver`/`hr` 等；含 `hr`+`ver` 即 NM 设备 |
| `/api/system/info` | GET | 完整状态：`stratum.url`（in-use pool）、`miner.state`、`miner.hashRate` |
| `/api/setting/mining` | GET | 读 stratum 配置：`stratum.used/primary/fallback`（各含 url/user/pwd） |
| `/api/setting/mining` | PATCH | 写 stratum 配置，body: `{"stratum":{"primary":{...},"fallback":{...}}}` |
| `/api/system/restart` | POST | 重启设备（约 10-15 秒不可达） |
| `/alive` | GET | 设备自己做的 /24 ICMP 扫描缓存（**有漏报，仅参考**） |
| `/api/swarm/scan` | POST | 触发某台设备重新做子网扫描 |

完整文档：任一 Axe 设备的 `http://<ip>/api-doc.html`。

## 步骤一：全段扫描 + 统计

```bash
python3 - <<'EOF'
import json, urllib.request, concurrent.futures

ips = [f"192.168.124.{i}" for i in range(1, 255)]   # 按实际网段调整

def probe(ip):
    try:
        with urllib.request.urlopen(f'http://{ip}/probe', timeout=1.5) as r:
            j = json.loads(r.read())
            if 'hr' in j and 'ver' in j:
                return (ip, j.get('model'), j.get('hostname'), j.get('ver'), j.get('hr'))
    except Exception:
        pass
    return None

found = []
with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
    for res in ex.map(probe, ips):
        if res:
            found.append(res)

found.sort(key=lambda x: int(x[0].split('.')[-1]))
print(f"=== NM devices: {len(found)} ===")
for ip, model, host, ver, hr in found:
    print(f"{ip:18s} {model:12s} {host:22s} fw={ver}  hr={hr/1e9:.1f} GH/s")
EOF
```

## 步骤二：查看各设备 in-use pool

```bash
python3 - <<'EOF'
import json, urllib.request, concurrent.futures
from collections import Counter

ips = [...]   # 步骤一发现的设备 IP 列表

def info(ip):
    try:
        with urllib.request.urlopen(f'http://{ip}/api/system/info', timeout=3) as r:
            return (ip, json.loads(r.read()).get('stratum', {}).get('url', '?'))
    except Exception:
        return (ip, 'OFFLINE')

with concurrent.futures.ThreadPoolExecutor(max_workers=32) as ex:
    results = sorted(ex.map(info, ips), key=lambda x: int(x[0].split('.')[-1]))

for url, n in Counter(u for _, u in results).most_common():
    print(f"{n:3d} 台  {url}")
print()
for ip, url in results:
    print(f"{ip:18s} {url}")
EOF
```

## 步骤三：批量修改主池地址（保留 user/pwd/fallback）

**先读后写**：GET 拿到完整 stratum 结构，只替换 `primary.url`，其余原样带回。

```bash
python3 - <<'EOF'
import json, urllib.request, concurrent.futures

NEW_URL = "stratum+tcp://192.168.124.15:3333"   # 目标主池
ips = [...]   # 要改的设备 IP 列表

def patch(ip):
    with urllib.request.urlopen(f'http://{ip}/api/setting/mining', timeout=5) as r:
        st = json.loads(r.read()).get('stratum', {})
    prim = dict(st.get('primary', {}))
    fb = dict(st.get('fallback', {}))
    old = prim.get('url')
    prim['url'] = NEW_URL
    body = json.dumps({"stratum": {"primary": prim, "fallback": fb}}).encode()
    req = urllib.request.Request(f'http://{ip}/api/setting/mining', data=body,
                                 headers={'Content-Type': 'application/json'}, method='PATCH')
    with urllib.request.urlopen(req, timeout=5) as r:
        return (ip, old, r.read().decode())

with concurrent.futures.ThreadPoolExecutor(max_workers=9) as ex:
    for ip, old, resp in sorted(ex.map(patch, ips), key=lambda x: int(x[0].split('.')[-1])):
        print(f"{ip:18s} {old} -> {NEW_URL}  resp={resp}")
EOF
```

## 步骤四：批量重启使配置生效

```bash
python3 - <<'EOF'
import json, urllib.request, concurrent.futures

ips = [...]

def restart(ip):
    try:
        req = urllib.request.Request(f'http://{ip}/api/system/restart', data=b'{}',
                                     headers={'Content-Type': 'application/json'}, method='POST')
        with urllib.request.urlopen(req, timeout=5) as r:
            return (ip, r.read().decode().strip())
    except Exception as e:
        return (ip, f"ERR: {e}")

with concurrent.futures.ThreadPoolExecutor(max_workers=9) as ex:
    for ip, resp in sorted(ex.map(restart, ips), key=lambda x: int(x[0].split('.')[-1])):
        print(f"{ip:18s} {resp}")
EOF
```

## 步骤五：验证（重启后等 ~60 秒）

```bash
sleep 60; python3 - <<'EOF'
import json, urllib.request, concurrent.futures

ips = [...]

def check(ip):
    try:
        with urllib.request.urlopen(f'http://{ip}/api/system/info', timeout=3) as r:
            j = json.loads(r.read())
            return (ip, j.get('stratum', {}).get('url', '?'),
                    j.get('miner', {}).get('state', '?'), j.get('miner', {}).get('hashRate', 0))
    except Exception:
        return (ip, 'OFFLINE', '-', 0)

with concurrent.futures.ThreadPoolExecutor(max_workers=9) as ex:
    for ip, url, state, hr in sorted(ex.map(check, ips), key=lambda x: int(x[0].split('.')[-1])):
        print(f"{ip:18s} pool={url:42s} state={state:10s} hr={hr/1e9:.1f} GH/s")
EOF
```

最后再跑一遍**步骤一的全段扫描 + 步骤二的 pool 统计**，确认：
1. 目标设备全部指向新 pool 且 `state=running`
2. 旧 pool 上设备数为 0（`grep` 旧 IP 即可）

## 检查旧节点是否还有残留连接（SSH 到旧 stratum 节点）

```bash
ssh root@<old-node-ip> '
echo "=== stratum 进程 ==="
ps | grep -iE "stratum|ckpool" | grep -v grep || echo "(无)"
echo "=== 3333 监听 ==="
netstat -tln 2>/dev/null | grep 3333
echo "=== 3333 活跃连接 ==="
ss -tnp 2>/dev/null | grep 3333 || echo "(无活跃连接)"
'
```

## 踩坑记录

- **`/alive` 漏报**：ICMP 扫描不响应 ping 的设备全部漏掉。2026-09-24 实测
  swarm 报 17 台，全段 HTTP probe 实为 25 台。**永远用全段 probe 做最终统计**。
- **host key 变化**：Rockbase 板子重刷后 SSH host key 会变，遇到
  `REMOTE HOST IDENTIFICATION HAS CHANGED` 用
  `ssh-keygen -f ~/.ssh/known_hosts -R <ip>` 清掉旧 key。
- **网段**：本机（开发机）通常直连 192.168.124.0/24（eth0）；193/123 网段
  经 eth2 路由可达，但矿机一般只在 124 段。
- **DGB 设备**：个别设备可能挖 DigiByte（pool 含 `dgb-stratum`），统计 BTC
  算力时注意剔除。
- **并发度**：probe 用 64 线程、timeout 1.5s 足够快；PATCH/restart 用 9 线程
  避免对 ESP32 造成压力。
