---
name: rockbase-web
description: 'Develop and debug Rockbase web interface and API. Covers CGI, status API, frontend architecture. Use when: web, 网页, 前端, API, CGI, status.cgi, rockbase-webd, 页面, frontend, http, 监控页面.'
---

# Rockbase Web/API 开发

完整设计见 `docs/design/rockbase-web-api-architecture.md`。

---

## 产品定位

- Bitcoin 剪枝验证节点 + 本地 Stratum/solo mining
- Web 页面和 LCD 页面用于状态监控
- 面向用户文案：`剪枝验证节点 + 独立挖矿服务`
- 不要在界面写"Full Node"（实际运行 `pruned=true`）

---

## 关键路径

```
buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/
├── index.html
├── api/
│   └── status.cgi        # 当前 CGI 入口
└── ...
```

---

## 当前问题

当前 `status.cgi` 的主要瓶颈：

- 每次 HTTP 请求即时采样，创建多个 shell 进程
- 频繁调用 `grep`、`awk`、`sed`、`df`、`du`、`ip`、`iw`、`ps`
- 可能调用 `bitcoin-cli getblockchaininfo`、`getnetworkinfo`、`getblockhash`、`getblockheader`

导致的后果：

- 请求延迟抖动
- CPU 瞬时峰值
- 页面偶发卡顿或空白

---

## 目标架构

```
静态前端资源 + 常驻 rockbase-webd + 后台定时采样 + 内存缓存 + JSON API
```

### 建议演进方向

1. 后台采样去耦 HTTP 请求
2. 内存缓存最近一次采样结果
3. API 直接从缓存返回，不触发重采样
4. 前端静态资源与数据 API 分离

---

## 热部署

Web 文件热部署到板子后，在线验证通过再固化到 `buildroot-overlay/` 下。详见 `rockbase-hotfix` skill。
