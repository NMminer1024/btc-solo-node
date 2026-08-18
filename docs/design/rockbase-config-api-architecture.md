# Rockbase 配置/API 架构记录

## 文档目的

本文档定义 Rockbase 的统一配置交互方案，覆盖 Web 端和设备屏幕端共用的配置/动作接口。

目标是把后续新增的 Wi-Fi、ckpool、bitcoind、软开关机、LCD 偏好、LED、恢复出厂等能力，纳入同一套可扩展框架，而不是继续增加零散 CGI 和脚本入口。

## 设计目标

- Web 和 LCD 使用同一套后端 API。
- 配置读写、服务动作、状态查询分离。
- 新增模块时只加 adapter，不重做框架。
- 慢操作全部异步化，避免阻塞 UI。
- 所有可见配置都可校验、可回滚、可审计。

## 推荐后端

建议将当前计划中的 `rockbase-webd` 扩展为统一入口：

- 对外提供 HTTP API。
- 对内管理配置 schema、操作队列、适配器调用。
- 读取 `rockbase-statusd` 生成的状态缓存。

职责边界建议如下：

- `rockbase-statusd`：只负责采样和状态缓存。
- `rockbase-webd`：只负责配置读写、动作执行、接口输出。
- Web 前端和 LCD UI：只消费同一套 API。

## 配置模型

建议使用一个主配置文件作为持久化源：

```text
/etc/rockbase/config.json
```

并保留运行态状态文件：

```text
/run/rockbase/config-state.json
```

主配置按模块组织，例如：

```json
{
  "version": 1,
  "wifi": {},
  "ckpool": {},
  "bitcoin": {},
  "display": {},
  "system": {}
}
```

各模块 adapter 负责把统一配置渲染到现有落地文件：

- `wifi` -> `/etc/rockbase-wifi.conf`
- `ckpool` -> `/root/ckpool/ckpool.conf`
- `bitcoin` -> `/root/bitcoind/bitcoin.conf`
- `display` -> `/etc/rockbase-lcdui.conf`

## API 约定

建议统一使用版本化资源接口：

```text
GET    /api/v1/config
GET    /api/v1/config/schema
GET    /api/v1/config/<module>
PATCH  /api/v1/config/<module>

POST   /api/v1/actions/<module>/<action>
GET    /api/v1/operations/<id>
GET    /api/v1/capabilities
```

补充的只读状态接口仍可独立保留：

```text
GET /api/v1/status
GET /api/v1/summary
GET /api/v1/logs/<service>
```

其中：

- `schema` 用于前端和 LCD 自动生成表单。
- `capabilities` 用于区分当前设备是否支持 Wi-Fi、LCD、USB Wi-Fi、关机 GPIO 等。
- `operations` 用于跟踪 apply/restart/reboot/shutdown 这类慢操作。

## 模块接口

每个配置模块建议实现统一 adapter 能力：

- `get_config`
- `validate`
- `save`
- `render`
- `apply`
- `status`
- `rollback`

这样新增模块时，不需要增加新的框架分支，只要补一个 adapter。

## 操作模型

所有可能断网、重启服务、关机、重启设备的动作都应异步执行。

推荐流程：

```text
validate -> save -> render -> apply -> verify -> rollback
```

原则：

- 保存失败直接返回错误。
- 应用失败必须可回滚到上一个稳定配置。
- 操作接口先返回 `operation_id`，前端轮询结果。
- Wi-Fi 这类高风险动作必须保留“失败恢复旧配置”的路径。

## 前端和 LCD 分工

Web 端适合做完整配置交互：

- Wi-Fi 扫描、手动 SSID、密码、保存。
- ckpool / Stratum 参数。
- bitcoind 安全参数。
- 系统重启、关机、服务重启。
- 高级项展开/折叠。

LCD 端适合做轻量交互：

- 当前状态展示。
- 常用动作确认。
- Wi-Fi 引导页、Web 地址提示、设备信息。

不建议在 LCD 上做复杂密码输入；LCD 更适合调用同一套动作接口，而不是独立维护一套配置逻辑。

## 扩展策略

未来新增模块时，遵循同一模式：

```text
config/<module>
actions/<module>/<action>
```

例子：

- `display`：亮度、旋转、待机时间。
- `led`：状态灯模式、测试模式。
- `update`：升级包检查、应用升级。
- `backup`：导出配置、恢复配置。
- `network`：静态 IP、AP 模式、切换网络。

只要模块遵守 schema + adapter + operation 约定，前端和 LCD 都可以直接复用。

## 迁移顺序

建议按下面顺序落地：

1. 建立统一 `config.json` schema。
2. 先迁移 Wi-Fi 写入路径。
3. 接入 ckpool 配置和服务动作。
4. 接入 bitcoind 配置和服务动作。
5. 接入关机/重启/恢复默认等系统动作。
6. 让 Web 和 LCD 完整切换到同一套 API。

## 兼容性原则

- 现有 `status` 缓存链路可以继续保留。
- 现有 `web/cgi` 入口可作为过渡兼容层。
- 新增功能优先走统一 API，不再新增独立脚本入口。

