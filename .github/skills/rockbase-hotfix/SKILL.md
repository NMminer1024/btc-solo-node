---
name: rockbase-hotfix
description: 'Hot-deploy files to live Rockbase boards for validation, then solidify changes into image source. Use when: hot-deploy, hotfix, 热部署, scp to board, deploy to board, 验证, solidify, 固化, 热修复, 部署到板子.'
---

# Rockbase 热部署与固化流程

## 核心规则（来自 AGENTS.md）

1. **先热部署验证，后固化到源码。** 改任何功能/页面，先用 `scp`/`ssh` 把改动文件部署到真实板子，在线验证通过后，再把改动写回镜像源
2. **不要颠倒顺序**，除非该改动无法热部署（如内核/驱动/DTS 改动）
3. **追踪所有改动的业务文件**，固化时不要靠猜

---

## 热部署工作流

### Step 1: 确认目标板子

**板子 IP 以 `AGENTS.md`「板子基线」表为准（唯一权威源）。**

> ⚠️ IP 是 DHCP 会变，**不要**在这里维护硬编码 IP 表（历史曾因 IP 过期连错板子）。
> 真机必须用 **MAC（wlan0，不变）做唯一标识**，每次部署前先确认目标 MAC 当前 IP。
> 局域网 6 台真机（2026-09-24 起全部 1G）：IP 用 22 端口扫描 + MAC 采集定位，见 `AGENTS.md` 或 repo memory `freeze-baseline.md`「批量真机巡检」。

### Step 2: 部署 Web 文件到板子

```bash
# Web 根目录在板子上
scp buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/<file> root@192.168.124.X:/root/web/
```

### Step 3: 部署脚本/配置

```bash
# 部署脚本/守护进程/init.d 文件时用 -p 保留权限，否则 scp 默认会丢 +x 位
scp -p <local_file> root@192.168.124.X:<target_path>
```

### Step 4: 在线验证

```bash
ssh root@192.168.124.X '<验证命令>'
```

#### ⚠️ 部署脚本/守护进程后必查清单（2026-09-24 踩坑固化）

1. **执行权限丢失**：`scp`（不带 `-p`）覆盖脚本后权限会变 `-rw-r--r--`，init.d `start` 报 `Permission denied` 但**不显示**（被 `>/dev/null 2>&1` 吞掉），daemon 静默起不来。
   - **症状**：web 卡片/功能无数据，`/tmp/*.json` 产物文件不存在，`pgrep` 查不到进程。
   - **修复**：`ssh root@<IP> 'ls -l <target_path>'` 核对 x 位，丢了就 `chmod +x <target_path>` 后重启对应 init.d 服务。
2. **init.d `start` 是静默失败的**：`Sxx<name> start` 报错被重定向吞掉，只返回 0。必须靠**查进程 + 查产物文件**双重确认真的起来了，不能只看 `start` 返回 OK。
3. **busybox `pgrep -f` 不可靠**（会误报进程不存在）：核实守护进程改用
   ```bash
   ssh root@<IP> 'ps -eo pid,ppid,args | grep <name> | grep -v grep'
   ```
   判读：`ppid=1` 才是被 init 收养的常驻 daemon；`ppid` 还是 SSH 会话 PID 的只是 `nohup ... &` 临时存活，会话退出后可能还在但不算规范启动。

### Step 5: 验证通过后固化

将改动写回仓库中对应的源文件（通常在 `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/` 下）。

---

## 固化规则（"打进镜像"）

当用户说"打进镜像"时：

1. **只固化已验证的改动**，把热部署中确认过的文件改动写入镜像源
2. **不要编译或导出新镜像**，除非用户明确要求编译
3. 固化完成后，如果需要构建/导出，需等到用户明确指示

---

## 无法热部署的改动

内核、驱动、DTS、U-Boot、Buildroot 包配置等改动无法热部署，必须：
1. 先在 `git` 中记录改动
2. 重编内核/镜像
3. 重新烧录 SD 卡
4. 按 freeze 记录清单验证

---

## 固化后若需编译镜像（统一网络镜像验收）

2026-08-20 起为统一网络镜像（USB + SDIO 驱动都在，运行时 `rockbase_netsel.sh` 自动选择，目录后缀 `-net-`）。烧录后验收：

- 镜像层：`8189fs.ko` 存在、有 `wpa_supplicant/iwconfig`、有 `/usr/bin/rockbase_netsel.sh`
- 运行时：`/tmp/rockbase-net-mode` 的 `NET_MODE` 与所插硬件匹配
  - 插 USB 有线网卡 → `usb_eth`，`eth*` up，`lsmod` 无 `8189fs`
  - 插 USB WiFi 狗 → `usb_wifi`，`wlan*` 连接，`lsmod` 无 `8189fs`
  - 什么都不插 → `sdio_wifi`，`lsmod` 有 `8189fs`，`wlan0` 走 SDIO

```bash
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
echo "[netsel]"; cat /tmp/rockbase-net-mode 2>/dev/null
find /lib/modules -name "8189fs.ko" | head -3
lsmod | grep 8189fs || echo "(8189fs not loaded)"
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
[ -x /usr/bin/rockbase_netsel.sh ] && echo "OK:netsel present" || echo "FAIL:no netsel"
'
```

任一 `FAIL` 都按"镜像与模式不一致"处理，先排查误烧录（目录后缀 `-net-`；旧镜像为 `-usb-`/`-sdio-`）再继续业务验证。

---

## 变更追踪

热调试期间，在 session memory 中维护一个清单：

```markdown
## Hot-debug changed files
- [ ] `path/to/file1` — 改动描述
- [ ] `path/to/file2` — 改动描述
```

固化时逐项勾销。
