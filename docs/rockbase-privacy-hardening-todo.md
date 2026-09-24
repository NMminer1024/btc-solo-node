# Rockbase 产品隐私加固 TODO

> 审计日期：2026-09-24
> 审计对象：192.168.124.31（MAC e0:95:bc:bb:62:bf）
> 审计方式：SSH 上真机全量扫描

## 现状总结

| 层面 | 状态 |
|------|------|
| C 源码（.c/.h） | ✅ 真机上不存在 |
| 构建残留（Makefile/.git/.o/.a） | ✅ 无 |
| 二进制 debug symbols | ✅ 已 strip |
| 调试工具（gdb/strace） | ✅ 未安装 |
| Shell 脚本 | ❌ 全部明文可读（~15 个 rockbase_*.sh + OTA 脚本） |
| Web 前端（JS/HTML/CSS） | ❌ 明文可读 |
| CGI 脚本 | ❌ 明文可读 |
| 配置文件 | ❌ 明文可读（含密码、地址） |
| U-Boot env | ❌ fw_printenv 可读 |
| 设备树 DTB | ❌ 可 dtc 反编译 |
| 内核版本串 | ❌ 暴露开发机名 bitpony@DESKTOP-KC5S6JM |
| SSH 安全 | ❌ PermitEmptyPasswords yes + PermitRootLogin yes |
| 调试脚本 | ❌ cpu_watch.sh / sd_bench.sh 等仍在镜像中 |

---

## P0 — 安全漏洞（立即修复）

### [ ] 1. 关闭 SSH 空密码登录
- **文件**：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/ssh/sshd_config`
- **改动**：`PermitEmptyPasswords no`
- **验证**：`ssh root@board` 空密码应被拒绝

### [ ] 2. 禁止 root 密码直接登录
- **文件**：同上
- **改动**：`PermitRootLogin prohibit-password`
- **配套**：确保部署流程中预置 SSH key（`/etc/ssh/authorized_keys`）
- **注意**：需确认 OTA / 工厂产线流程不依赖 root 密码登录

### [ ] 3. 移除 telnet
- **位置**：buildroot 配置中取消 `BR_PACKAGE_HOST_TELNET` 或对应 busybox applet
- **验证**：`which telnet` 应无输出

### [ ] 4. ckpool 认证密码去硬编码
- **文件**：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/ckpool/ckpool.conf`
- **方案**：
  - 方案 A：首次启动时由 init 脚本随机生成，写入 `/root/config/ckpool/ckpool.conf`（config 分区持久化）
  - 方案 B：每批次产线烧录时注入不同密码
- **同步修改**：`btcaddress` 也不应硬编码在镜像中（改为出厂配置或留空）

---

## P1 — 减少架构泄露

### [ ] 5. 关键 Shell 脚本编译为二进制
- **工具**：[shc](https://github.com/neurobin/shc)（shell to C compiler）或 [compilebox](https://github.com/0x6rss/compilebox)
- **需保护的脚本**（按敏感度排序）：
  1. `/usr/sbin/rockbase-ota.sh` — OTA A/B 升级完整机制（~500 行）
  2. `/usr/bin/rockbase_netsel.sh` — 网络选择逻辑
  3. `/usr/bin/rockbase_supervisord.sh` — 服务管理架构
  4. `/usr/bin/rockbase_statusd.sh` — 状态采集（56KB，最大脚本）
  5. `/usr/bin/rockbase_miners_cache.sh` — 矿机缓存逻辑
  6. `/usr/bin/rockbase_miners_historyd.sh` — 矿机历史
  7. `/usr/bin/rockbase_serial_healthd.sh` — 串口健康检测
  8. `/usr/bin/rockbase_heartbeatd.sh` — 心跳
  9. `/usr/bin/rockbase_ledd.sh` — LED 控制
  10. `/usr/bin/rockbase_bitcoind_ready.sh` — bitcoind 就绪检测
  11. `/usr/bin/rockbase_usb_wifi.sh` — USB WiFi 管理
  12. `/usr/bin/rockbase_wifi.sh` — WiFi 管理
  13. `/usr/bin/rockbase_timezone.sh` — 时区
  14. `/usr/bin/rockbase_boot_audit.sh` — 启动审计
- **实施步骤**：
  1. 在 buildroot 中添加 shc 作为 host 包（或交叉编译 C 包装器）
  2. 构建时自动将 `.sh` → shc 编译 → 输出二进制
  3. 二进制命名保持原名（去掉 `.sh`），init.d 脚本中更新路径
  4. 保留 `.sh` 源码在 SDK 仓库中，不打包进镜像
- **注意**：shc 生成的二进制依赖 libc，需确认目标 glibc 兼容；busybox 环境可能需静态链接

### [ ] 6. Web 前端 JS 混淆
- **文件**：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/app.js`
- **工具**：[terser](https://terser.org/)（`npx terser app.js -c -m -o app.min.js`）
- **改动**：
  - 构建流程中加一步 terser 压缩
  - 镜像中只放 `app.min.js`，`index.html` 引用改为 `app.min.js`
  - `world-map.js` 同理（如果是自研的）
- **验证**：`cat /root/web/app.min.js` 应不可读

### [ ] 7. 内核版本串脱敏
- **位置**：kernel 构建配置
- **改动**：
  ```makefile
  export KBUILD_BUILD_USER=rockbase
  export KBUILD_BUILD_HOST=build
  ```
  或在顶层 Makefile / buildroot 配置中设置
- **当前泄露**：`Linux version 6.6.36 (bitpony@DESKTOP-KC5S6JM) (riscv64-unknown-linux-gnu-gcc ...)`
- **目标**：`Linux version 6.6.36 (rockbase@build) (...)`

### [ ] 8. CGI 脚本保护
- **文件**：`/root/web/api/*.cgi`（10 个）+ `/root/web/index.cgi`
- **检查**：确认是 shell 脚本还是 C 二进制
  - 如果是 shell：同 #5 用 shc 编译
  - 如果是 C：确认已 strip（当前看起来是 shell）
- **httpd.conf**：检查是否暴露了 server version 等信息

---

## P2 — 进阶保护

### [ ] 9. U-Boot 环境变量保护
- **当前状态**：`fw_printenv` 可读所有变量（含 OTA 分区切换逻辑 `altbootcmd`）
- **方案**：
  - 方案 A：U-Boot 编译时启用 `CONFIG_ENV_IS_IN_MMC` + 加密（如果支持）
  - 方案 B：将 OTA 相关变量（`altbootcmd`、`ota_part`）移到自定义加密区域
  - 方案 C（最低成本）：至少把 `altbootcmd` 中的逻辑简化，减少信息量
- **优先级**：中等（需要 U-Boot 侧改动，工作量较大）

### [ ] 10. 设备树 DTB 保护
- **当前状态**：`/boot/k.dtb` 可用 `dtc -I dtb -O dts /boot/k.dtb` 反编译
- **方案**：
  - 方案 A：U-Boot 中加载加密 DTB，运行时解密（需改 U-Boot）
  - 方案 B：接受泄露（DTB 主要暴露硬件配置，竞品用同芯片也能看到）
- **优先级**：低（K230 是公开芯片，DTB 信息价值有限）

### [ ] 11. rootfs 加密（长期）
- **方案**：dm-crypt / LUKS 加密 rootfs 分区
- **前提**：
  - 密钥存储方案（TPM？eFuse？U-Boot 环境变量？）
  - 启动时间影响评估
  - OTA 流程适配（A/B 两个分区都要加密）
- **优先级**：低（工作量大，当前产品阶段可暂缓）

---

## P3 — 产品化清理

### [ ] 12. 移除开发调试脚本
- **文件**（不应出现在产品镜像中）：
  - `/root/script/cpu_watch.sh`
  - `/root/script/cpu_page.sh`
  - `/root/script/sd_bench.sh`
  - `/root/script/sensor.sh`
  - `/root/script/bluetooth.sh`
  - `/root/script/rndis.sh`
  - `/root/script/acm.sh`
  - `/root/script/rgb-cycle.sh`
  - `/root/script/rgb-cycle.bin`
  - `/root/script/sync-block.sh`
- **操作**：从 `rootfs_overlay/root/script/` 中删除，或改为仅 debug 镜像包含
- **注意**：确认 `sync-block.sh` 和 `rgb-cycle.sh` 是否有生产用途

### [ ] 13. 移除/限制 bitcoind debug 日志
- **文件**：`/root/block/debug.log`
- **改动**：bitcoind 启动参数加 `-debuglogfile=/dev/null` 或限制日志级别
- **原因**：debug.log 包含 peer IP、内部通信细节

### [ ] 14. 统一产品标识
- **hostname**：确认为品牌名（当前 `canaan`）
- **SSH banner**：`/etc/profile.d/canaan_banner.sh` 改为品牌 banner
- **Web 页面 title**：确认不暴露内部项目名
- **OTA manifest URL**：`/etc/rockbase-ota-manifest-url` 中的域名确认是生产域名

### [ ] 15. 清理 /root 目录权限
- **当前**：`/root` 是 `drwx------`（700），但 SSH root 登录后可直接访问
- **建议**：配合 #2（禁止 root 密码登录）后，风险降低
- **额外**：`/root/config/` 目录包含运行时配置，确认权限正确

---

## 实施顺序建议

```
第 1 批（1-2 天）：#1 #2 #3 #4 #7 #12 #13 #14
  → 纯配置改动 + 删文件，风险低，收益大

第 2 批（3-5 天）：#5 #6 #8
  → 需要引入 shc/terser 工具链，改构建流程

第 3 批（按需）：#9 #10 #11 #15
  → 涉及 U-Boot / 内核改动，工作量大，按产品阶段决定
```

## 验证清单

每批改动完成后，重新导出镜像并烧录测试板，执行以下检查：

```bash
# 1. 确认无源码
find / -name "*.c" -o -name "*.h" -o -name "Makefile" -o -name ".git" 2>/dev/null

# 2. 确认脚本已编译
file /usr/bin/rockbase_*.sh 2>/dev/null  # 应无输出或显示 ELF
strings /usr/bin/rockbase-ota 2>/dev/null | head  # 应不可读

# 3. 确认 JS 已混淆
wc -c /root/web/app.js  # 应显著小于源码
head -c 200 /root/web/app.js  # 应不可读

# 4. 确认 SSH 安全
grep -E "PermitEmpty|PermitRoot" /etc/ssh/sshd_config

# 5. 确认内核版本串
cat /proc/version  # 不应包含 bitpony@DESKTOP

# 6. 确认调试脚本已移除
ls /root/script/  # 应只剩生产必需脚本

# 7. 确认 telnet 已移除
which telnet  # 应无输出
```
