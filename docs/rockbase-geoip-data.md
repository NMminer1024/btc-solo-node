# GeoLite2-City.mmdb 数据文件管理

`rockbase-geoip` 工具依赖 MaxMind GeoLite2-City 数据库
（`/usr/share/GeoLite2-City.mmdb`，约 65MB）。

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

## 更新频率

建议每月跑一次 `geoipupdate` 并替换 overlay 中的文件（数据每月更新）。

## 降级行为

mmdb 缺失时：
- `rockbase-geoip` 以退出码 1 报错，statusd 的 self_geo 保持 null（无绿星）；
- `/api/geoip.cgi` 返回 `{"ok":false,"error":"geoip unavailable"}`，前端 peer map
  只显示坐标点、无城市名。
