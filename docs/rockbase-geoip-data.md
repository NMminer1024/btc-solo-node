# GeoLite2-City.mmdb 数据文件管理

`rockbase-geoip` 工具依赖 MaxMind GeoLite2-City 数据库（约 65MB）。

**默认搜索顺序**（`rockbase-geoip` 无参数时，`/api/geoip.cgi` 同规则）：
1. `/root/config/GeoLite2-City.mmdb` — config 分区，仅作**热修逃生舱**
   （临时 scp 覆盖验证，不随 OTA 更新）；
2. `/usr/share/GeoLite2-City.mmdb` — rootfs，**常规唯一真源**，随 OTA 更新。

## 许可约束

- GeoLite2 免费数据**可用于产品**（含嵌入式设备）。
- 该文件**已进入 git 跟踪**（`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/GeoLite2-City.mmdb`），
  任何 clone + 构建都会把 `/usr/share/GeoLite2-City.mmdb` 打进镜像，无需手动 provision。
- 注意：文件约 65MB，超过 GitHub 推荐的 50MB 单文件大小，push 时会触发 GH001 警告（可正常提交）。
- 若需更新数据，仍需 MaxMind 账号（免费）+ license key 下载。

## 更新方式

1. 注册 https://dev.maxmind.com/geoip/geolite2-free-geolocation-data
2. 创建 license key，用官方 `geoipupdate` 工具拉取：

```sh
# GeoIP.conf:
#   AccountID <你的账号ID>
#   LicenseKey <你的key>
#   EditionIDs GeoLite2-City
./geoipupdate -f GeoIP.conf -d /tmp/geoip
```

3. 覆盖 overlay 中已跟踪的文件（构建时会随 rootfs 打进镜像）：

```sh
cp /tmp/geoip/GeoLite2-City.mmdb \
   buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/
git add buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/GeoLite2-City.mmdb
git commit -m "chore: refresh GeoLite2-City.mmdb"
git push origin dev
```

4. 热部署到板子验证（可选）：

```sh
scp .../GeoLite2-City.mmdb root@<board>:/usr/share/
```

## 放置策略：rootfs（随 OTA）而非 config 分区

数据库放 rootfs、随 OTA 更新，**不**放 config 分区。理由：

- **新鲜度**：GeoLite2 每月更新；放 rootfs 则每次 OTA 都带构建时验证过的版本，
  镜像即唯一真源。放 config 分区会冻结在首次 seed 的版本，除非另写升级逻辑。
- **分区定位**：config 分区存用户/设备状态（WiFi profile、bitcoin.conf、ckpool
  数据、历史曲线），需跨 OTA 保留；mmdb 是厂商数据文件，不是用户状态。
- **避免版本漂移**：同一固件版本的所有设备 geo 数据一致；config 方案下不同
  首烧时间的设备会持有不同版本数据库。
- **零额外代码**：不需要版本比较/下载/原子替换逻辑，没有半写失败模式。
- **容量**：config 分区仅 1G，65MB 厂商数据占近 7% 不划算。

`/root/config/` 优先搜索路径保留为逃生舱：紧急热修数据库时 scp 到 config 分区
即可覆盖，无需重打 OTA；常规路径永远是 OTA 带的 `/usr/share/` 版本。

## 更新频率

建议每月跑一次 `geoipupdate` 并替换 overlay 中的文件（数据每月更新），
随下一次 OTA 发布生效。

## 降级行为

mmdb 缺失时：
- `rockbase-geoip` 以退出码 1 报错，statusd 的 self_geo 保持 null（无绿星）；
- `/api/geoip.cgi` 返回 `{"ok":false,"error":"geoip unavailable"}`，前端 peer map
  只显示坐标点、无城市名。
