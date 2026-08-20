# Rockbase 网络接口合并方案：单镜像 USB 优先 + SDIO 回退

状态：**一期已实施并热部署验证通过**（2026-08-20）。构建层 + 运行时 netsel + wifi 联动 + 镜像命名统一均已完成，三种硬件组合（USB 有线 / USB WiFi / 无 USB）在真实板子上验证通过。二期热插拔未做。

## 实施记录（2026-08-20）

- 已落地文件：`tools/rockbase_net_mode.sh`（统一镜像，K230_NET 默认 `auto`）、`post-build.sh`（去掉编译期 unbind/无条件 modprobe）、新增 `rootfs_overlay/usr/bin/rockbase_netsel.sh` + `etc/init.d/S37netsel`、`rockbase_wifi.sh`（读取 netsel 决策）、`Makefile`/`post-image.sh`（镜像后缀统一为 `-net`）。
- **安全修正（重要，偏离原方案）**：原方案第 4 节要求 USB 路径下运行时 unbind SDIO 控制器。实测发现**运行时 `rmmod 8189fs` 会触发历史 freeze 嫌疑路径**（RTW 线程 + SDIO host claim，且通过该 WiFi 管理时还会杀掉 SSH 链路），1G 板（124.15）实测卡死一次，看门狗重启恢复。因此 `rockbase_netsel.sh` 的最终行为是：
  - 8189fs **未加载**时（统一镜像的正常启动路径：S37netsel 早于 S38wifi，8189fs 不再无条件加载）→ 直接 unbind SDIO 控制器，等价于原构建期安全行为；
  - 8189fs **已加载**时（热插拔等场景）→ **跳过 unbind 并告警**，绝不运行时 rmmod。热插拔重选路留给二期。
- SDIO WiFi 控制器设备名按板型不同（1G: `91580000.sdhci0`/mmc0，2G: `91581000.sdhci1`/mmc1），脚本通过扫描 `/sys/bus/mmc/devices/mmc*:0001` 的 vendor（0x024c=Realtek）动态定位，不硬编码。
- 验证结果：124.15（无 USB）→ `sdio_wifi`，8189fs 加载、wlan0 正常；124.30（USB 有线）→ `usb_eth`，eth0 识别、S38wifi 跳过；124.35（USB WiFi 狗 0bda:c820）→ `usb_wifi`，wlan0 正常连接。

## 背景 / 目标

当前 `K230_NET` 是构建期互斥开关（`sdio_wifi` / `usb`），两种镜像二选一，见 `rockbase-build` skill。
目标：合并为**单一镜像**，同时带 SDIO WiFi（RTL8189FS）与 USB 网络（有线 + WiFi）驱动，板子上电后运行时自动探测：

1. 优先使用 USB 有线网卡（`r8152`/`ax8817x`/`ax88179_178a`/`cdc_ether`，内核内置）
2. 其次使用 USB WiFi 加密狗（`mt7601u`/`rtw88_8821cu`，已按 `/etc/rockbase-usb-wifi.map` 支持自动识别加载）
3. 都没有则回退到板载 SDIO WiFi（`8189fs`）

## 可行性结论（已核实，不必重新调研）

调研发现 usb / sdio 两种镜像的驱动集合本来就高度重叠，**真正互斥的只有一件事**：`BR2_PACKAGE_RTL8189FS` 包开关 + `post-build.sh` 里"没编 8189fs 就 unbind SDIO 控制器"这段构建期特化。其余驱动早就同时存在：

- USB 有线驱动（`r8152`/`ax8817x`/`ax88179_178a`/`cdc_ether`）：内核 `=y` 内置，不受 `K230_NET` 控制。
- USB WiFi 驱动（`mt7601u`/`rtw88_8821cu`）：`buildroot-overlay/board/canaan/k230_rockbase/fragment/linux.fragment` 里一直 `=m`，不受 `K230_NET` 控制。

因此合并工作量集中在**运行时脚本逻辑**，不涉及内核/驱动层新增改动。

## 架构设计

### 1. 构建层改动

文件：`tools/rockbase_net_mode.sh`

- 废弃互斥语义，`RTL8189FS` 始终 `=y`（本来 defconfig 默认值），usb 模式不再 `unset_pkg`、不再删 `8189fs.ko`。
- 保留 `K230_NET` 变量仅作为**强制模式**测试开关（如需单独验证某条路径），默认值改为 `auto`（=不做任何构建期驱动增删，全部驱动都在）。

文件：`buildroot-overlay/board/canaan/k230_rockbase/post-build.sh`（`auto_boot_proc` 函数）

- 去掉编译期"没有 SDIO 驱动就 unbind mmc0"逻辑（现在两种驱动都在，编译期不知道运行时会选哪条路径）。
- 去掉无条件 `modprobe 8189fs`。
- 这两件事下沉到运行时脚本（见下）。

### 2. 运行时层：新增网络选择脚本

新增 `rootfs_overlay/usr/bin/rockbase_netsel.sh`，早于 `S38wifi` 执行（新增 `rootfs_overlay/etc/init.d/S37netsel`），做统一决策，结果写入状态文件（建议 `/tmp/rockbase-net-mode`，值为 `usb_eth` / `usb_wifi` / `sdio_wifi`），供 `S38wifi`/`rockbase_wifi.sh` 读取：

```
优先级判定：
1. 探测 USB 有线网卡：driver path 含 /usb/ 且非 wlan 类接口（eth*/enx*）存在且已 up
   → 模式 = usb_eth；不 modprobe 8189fs，不启动 wifi 服务；走 udhcpc（S40network 已有 ifup eth0 逻辑，无需改）
2. 否则探测 USB WiFi 加密狗：复用现有 rockbase_usb_wifi.sh detect --autoload
   （按 VID:PID 匹配 /etc/rockbase-usb-wifi.map 自动加载 mt7601u/rtw88_8821cu）
   → 若出现对应 wlan 接口，模式 = usb_wifi；WLAN_DEV 指向该接口，跳过 8189fs
3. 否则回退：modprobe 8189fs，走原 SDIO WiFi 流程
   → 模式 = sdio_wifi
```

超时策略：参考现有 `USB_WIFI_WAIT_SEC`（约 3~5s）短轮询，避免 USB 枚举未完成时误判成"无 USB 设备"而回退 SDIO。

### 3. 现有脚本需要反转的逻辑

`rootfs_overlay/usr/bin/rockbase_wifi.sh` 的 `wifi_select_iface` / `wifi_ensure_device`：

- **现状**（反的）：先看 `wlan0`（8189fs）是否存在，不存在才退回 USB WiFi helper。
- **改为**：先读取 `S37netsel` 的决策结果；`usb_eth` 模式下 `S38wifi` 直接跳过启动（不执行 `wifi_start_service`）；`usb_wifi` 模式下直接使用探测到的 USB WiFi 接口；只有 `sdio_wifi` 模式才 `modprobe 8189fs`。

### 4. SDIO 控制器 unbind 时机后移到运行时

原编译期"没编 8189fs 就 unbind mmc0"是为了减少空转 SDIO 控制器的中断噪声（与历史 freeze 排查的 RTL8189FS/SDIO 主嫌疑相关，见 `rockbase-freeze-milestones.md`）。合并后只能在运行时做：

- `rockbase_netsel.sh` 判定为 `usb_eth`/`usb_wifi` 时，运行时执行 `echo <sdhci0-device-id> > /sys/bus/platform/drivers/sdhci-dwcmshc-kendryte/unbind`。
- 判定为 `sdio_wifi` 时保持绑定不动。
- 这样保留了"USB 优先时避免 SDIO 干扰"的既有 freeze 缓解收益，又不牺牲回退能力。

### 5. 热插拔（二期，独立验证，不要和一期混着做）

利用已运行的 `mdev -d`（`rootfs_overlay/etc/init.d/S01mdev`）加一条 hotplug 规则：USB 网络接口 add/remove 时触发重新执行 `rockbase_netsel.sh` 决策（例如拔掉 USB 网卡后自动 rebind SDIO、`modprobe 8189fs` 拉起 `wlan0`）。这部分逻辑较新、风险较高，建议单独立项验证，不要在一期（静态上电探测）里一起做。

## 需要改动的文件清单

| 文件 | 改动 |
|------|------|
| `tools/rockbase_net_mode.sh` | 简化为始终保留 RTL8189FS，不再做 usb/sdio 互斥 |
| `buildroot-overlay/board/canaan/k230_rockbase/post-build.sh` | 去掉编译期 unbind SDIO 逻辑，去掉无条件 modprobe 8189fs |
| 新增 `rootfs_overlay/usr/bin/rockbase_netsel.sh` | 核心探测 + 优先级决策 + 运行时 unbind SDIO |
| 新增 `rootfs_overlay/etc/init.d/S37netsel` | 早于 S38wifi 执行 |
| `rootfs_overlay/usr/bin/rockbase_wifi.sh` | `wifi_select_iface`/`wifi_ensure_device` 改顺序：读取 netsel 决策，`usb_eth` 模式下跳过 wifi 启动 |
| `.github/skills/rockbase-build/SKILL.md` | 更新网络模式章节，去掉 `-sdio`/`-usb` 镜像区分说明，改为单一"全功能网络"镜像 |
| `docs/README.md` | 补充本文档链接 |

## 风险点

1. **两个 wlan 设备命名冲突**：8189fs 与 USB WiFi 若都注册为 `wlan0` 可能抢名。决策逻辑必须保证同一时刻只 `modprobe` 需要的那一个驱动。
2. **USB 枚举时序**：USB 设备内核态已探测完成，但用户态脚本执行时节点可能还没建好，需要短超时轮询而非一次性判断。
3. **回归 SDIO freeze 风险**：若决策逻辑有误导致 USB 存在时 8189fs 仍被加载/SDIO 控制器仍绑定，等于走回历史 freeze 老路。验收必须严格检查这一点。**实施中已实测证实**：运行时 `rmmod 8189fs` + unbind 控制器会卡死（124.15 实测，pstore 显示卡死前 RTW_XMIT/RTW_CMD 线程活跃）。最终实现改为"8189fs 未加载才 unbind，已加载则跳过"，见上方实施记录。
4. **镜像体积/内存**：全功能镜像会比单一模式略大（多带一份 `8189fs.ko` 或反之），量级为几十到几百 KB，可忽略。
5. **非网络 USB 设备（如 U 盘）不会被误判**：探测逻辑是按接口类型/白名单精确匹配（扫描 `/sys/class/net` + `/etc/rockbase-usb-wifi.map` 的 VID:PID 白名单），不是"检测到任意 USB 设备就当网络设备"，U 盘走 `usb-storage` 驱动不会出现在 `/sys/class/net`，也不在 WiFi VID:PID 表里，会被正确判定为"无网络设备"并回退 SDIO。**实现时必须保持这种精确匹配的判定方式，不要简化成"lsusb 有设备就走 USB 模式"**。
   - 边界情况 1：板子只有一个 USB Host 口且无 Hub 时，插 U 盘会物理占满唯一端口，此时"探测不到网络设备"是硬件限制导致的正常结果，验收时需区分是硬件占用还是探测逻辑本身有问题。
   - 边界情况 2：极少数廉价双模 USB WiFi 狗（插入先枚举成虚拟光驱，需要 `usb-modeswitch` 才切换成网卡模式）不在当前 `rockbase-usb-wifi.map` 支持范围内，会被当成"无网络设备"处理，属于已知限制。

## 验收标准

- 插 USB 有线网卡 → `eth*` up；`8189fs` 未加载；`ip link` 无来自 8189fs 的 `wlan0`；SDIO 控制器已 unbind。
- 插 USB WiFi 狗（无有线）→ 对应 `wlan*` up 且能正常连接；`8189fs` 未加载。
- 什么都不插 → `8189fs` 加载，`wlan0` 走 SDIO 路径，行为与现有 `sdio_wifi` 镜像一致。
- 上电顺序切换（先插 USB 再拔）→ 二期热插拔单独验证，不纳入一期验收范围。

## 实施顺序建议（给后续 agent）

1. 先做构建层改动（保留双驱动），跑一次全量编译，确认镜像里 `8189fs.ko` 和 USB 驱动模块同时存在。
2. 写 `rockbase_netsel.sh` + `S37netsel`，先只做"探测 + 写状态文件 + 打日志"，不接管 modprobe/unbind，热部署上板验证探测结果准确（分别用有线/无线/都不插三种硬件组合验证）。
3. 探测逻辑验证无误后，再接入 modprobe 8189fs 的门控 + SDIO unbind 动作。
4. 改 `rockbase_wifi.sh` 优先级顺序，联调验证三种模式下 `S38wifi`/`S40network` 行为符合验收标准。
5. 更新 `rockbase-build` skill 文档，去掉 `K230_NET` 互斥镜像的描述。
6. 二期再单独做热插拔 mdev 规则。

## 相关背景文档

- `.github/skills/rockbase-build/SKILL.md` — 现有 K230_NET 构建规则（本方案实施后需要更新）
- `docs/rockbase-current-handoff.md` — USB 网络绕过 SDIO 的历史决策背景（2026-08-13 章节）
- `docs/rockbase-freeze-milestones.md` — RTL8189FS/SDIO freeze 排查历史
