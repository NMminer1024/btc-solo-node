# Rockbase 文档索引

本目录保存 Rockbase K230 项目的稳定文档、调试规则和卡死问题交接资料。

## 优先阅读

- [当前交接文档](rockbase-current-handoff.md)：当前状态、已验证结论、下一步要做什么。
- [项目基础信息](rockbase-project-overview.md)：板子身份、defconfig、镜像规则、源码路径、操作规则。
- [调试方法手册](rockbase-debug-playbook.md)：卡死后如何抓现场、如何捞 pstore、如何核对 SSH/镜像/构建。
- [Freeze 调试里程碑](rockbase-freeze-milestones.md)：重要卡死记录、阶段性结论和修复/诊断演进。

## 既有参考文档

- [设计文档目录](design/)：LVGL、Web/API 等专题架构设计。
- [配置/API 架构记录](design/rockbase-config-api-architecture.md)：Web/LCD 共用配置模型、schema 和操作接口。
- [Block Seed 镜像流程](rockbase-block-seed-image-workflow.md)：带 seed 镜像构建流程。默认不要使用。
- [网络接口合并方案](rockbase-net-unified-image-plan.md)：USB 优先 + SDIO 回退单镜像方案，已评审未实施，供后续 agent 接手。

## Seed 更新一页式清单（交接专用）

适用场景：每周从长期运行的 seed 设备更新仓库本地 `seed/root-block`。

唯一推荐流程：仅使用 `tools/sync-rockbase-seed-from-device.sh`。

执行步骤：

1. 在 seed 设备上先优雅停止 `bitcoind`。
2. 在 WSL 仓库根目录运行：

```bash
bash tools/sync-rockbase-seed-from-device.sh
```

3. 按提示输入：远端 IP/主机名、用户名、密码。
4. SSH 成功后输入远端区块数据目录。
5. 确认清空本地 `seed/root-block`，等待 rsync 完成。
6. 观察脚本末尾输出，确认本地 manifest 校验通过且 `du -sh` 正常。

完成判据：

- 本地目录 `seed/root-block` 下存在 `blocks/`、`blocks/index/`、`chainstate/`。
- 脚本未报错退出，最后打印 `Rockbase block seed 已更新`。

禁用项（避免干扰）：

- 不再使用任何“SD 卡挂载/拷贝 seed”旧流程。
- 不再查找或恢复已删除的 SD 卡辅助脚本。

## 本地临时记录

- `output/freeze-log.md` 被 git 忽略，适合放临时现场流水。
- 重要结论应及时整理进 [Freeze 调试里程碑](rockbase-freeze-milestones.md)，避免只留在本地 ignored 文件里。
