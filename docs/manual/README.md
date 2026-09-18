# Rockbase 产品说明书（材料目录）

本目录存放 **Rockbase 消费者使用说明书** 的原文与配套材料。

## 目录结构

```
docs/manual/
├── README.md                      # 本文件：目录说明与材料清单
├── rockbase-product-manual.md     # 说明书原文（中文）
└── images/                        # 说明书引用的图片
    ├── device-front.png           # 设备正面图
    ├── device-back-ports.png      # 设备背面 / 接口图（标注各接口位置）
    ├── lcd-boot.png               # 开机后 LCD 首屏
    ├── web-wifi.png               # 网页 WiFi 配置页
    ├── wired-connection.png       # 有线连接示意图
    ├── lcd-bitcoin.png            # LCD Bitcoin 页
    ├── lcd-stratum.png            # LCD Stratum 页
    ├── lcd-system.png             # LCD System 页
    ├── lcd-network.png            # LCD Network 页
    ├── web-dashboard.png          # 网页 Dashboard 总览页
    ├── web-miners.png             # 网页 Miners 页
    ├── web-log.png                # 网页 Log 页
    ├── web-miners-connected.png   # 网页 Miners 页（已接入矿机）
    └── web-ota.png                # 网页升级入口 / 升级中界面
```

## 使用说明

- 说明书原文：`rockbase-product-manual.md`，面向最终消费者，只讲"怎么操作"。
- 图片：统一放在 `images/` 下，文件名与原文中的引用一一对应。
  把对应图片按文件名放入 `images/` 后，原文中的图片即可正常显示。
- 图片格式：建议 PNG（截图）或 JPG（实拍），单张尽量控制在 1MB 以内。

## 待办材料清单

- [ ] 设备正面 / 背面实拍图（`device-front.png`、`device-back-ports.png`）
- [ ] LCD 四页实拍或截图（`lcd-bitcoin/stratum/system/network.png`）
- [ ] 网页各视图截图（`web-dashboard/miners/log/wifi/ota.png`）
- [ ] 开机首屏、有线连接示意图
- [ ] 说明书原文中所有 `〔待补充〕` 条目（默认地址、Stratum 接入参数、电源规格、尺寸重量、包装清单等）
