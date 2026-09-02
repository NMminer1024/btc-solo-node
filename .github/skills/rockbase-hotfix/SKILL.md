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

板子 IP（详见 `AGENTS.md` 或 `docs/rockbase-project-overview.md`）：

| IP | 型号 | MMC 拓扑 |
|----|------|----------|
| `192.168.124.15` | 1G | mmc0=WiFi, mmc1=SD |
| `192.168.124.23` | 2G | mmc0=WiFi, mmc1=SD（已统一接线） |
| `192.168.124.30` | 1G | mmc0=WiFi, mmc1=SD（usb 模式镜像） |
| `192.168.124.35` | 1G | mmc0=WiFi, mmc1=SD (SD64G) |

### Step 2: 部署 Web 文件到板子

```bash
# Web 根目录在板子上
scp buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/<file> root@192.168.124.X:/root/web/
```

### Step 3: 部署脚本/配置

```bash
scp <local_file> root@192.168.124.X:<target_path>
```

### Step 4: 在线验证

```bash
ssh root@192.168.124.X '<验证命令>'
```

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
