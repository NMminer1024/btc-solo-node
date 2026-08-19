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
| `192.168.124.8` | 1G | mmc0=WiFi, mmc1=SD |
| `192.168.124.15` | 1G | mmc0=WiFi, mmc1=SD |
| `192.168.124.21` | 1G | mmc0=WiFi, mmc1=SD |
| `192.168.124.23` | 2G | mmc0=SD, mmc1=WiFi |
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

## 固化后若需编译镜像（网络模式验收）

若本次固化后还要编译并烧录镜像，且涉及 `K230_NET`，烧录后必须做互斥验收：

- `usb` 目标：`8189fs.ko` 不存在、有 `wpa_supplicant/iwconfig`
- `sdio_wifi` 目标：`8189fs.ko` 存在、`lsmod` 有 `8189fs`、有 `wlan0`、有 `wpa_supplicant/iwconfig`

`usb` 验收命令：

```bash
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
find /lib/modules -name "8189fs.ko" | head -3
[ -f /lib/modules/*/kernel/drivers/net/wireless/8189fs/8189fs.ko ] && echo "FAIL:8189fs.ko present" || echo "OK:no 8189fs.ko"
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
'
```

`sdio_wifi` 验收命令：

```bash
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
find /lib/modules -name "8189fs.ko" | head -3
lsmod | grep 8189 || true
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
[ -d /sys/class/net/wlan0 ] && echo "OK:wlan0 present" || echo "FAIL:no wlan0"
'
```

任一 `FAIL` 都按"镜像模式不一致"处理，先排查误烧录（目录后缀 `-usb-` / `-sdio-`）再继续业务验证。

---

## 变更追踪

热调试期间，在 session memory 中维护一个清单：

```markdown
## Hot-debug changed files
- [ ] `path/to/file1` — 改动描述
- [ ] `path/to/file2` — 改动描述
```

固化时逐项勾销。
