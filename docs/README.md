# Rockbase 文档索引

本目录保存 Rockbase K230 项目的稳定文档、调试规则和卡死问题交接资料。

## 优先阅读

- [当前交接文档](rockbase-current-handoff.md)：当前状态、已验证结论、下一步要做什么。
- [项目基础信息](rockbase-project-overview.md)：板子身份、defconfig、镜像规则、源码路径、操作规则。
- [调试方法手册](rockbase-debug-playbook.md)：卡死后如何抓现场、如何捞 pstore、如何核对 SSH/镜像/构建。
- [Freeze 调试里程碑](rockbase-freeze-milestones.md)：重要卡死记录、阶段性结论和修复/诊断演进。

## 既有参考文档

- [设计文档目录](design/)：LVGL、Web/API 等专题架构设计。
- [Block Seed 镜像流程](rockbase-block-seed-image-workflow.md)：带 seed 镜像构建流程。默认不要使用。

## 本地临时记录

- `output/freeze-log.md` 被 git 忽略，适合放临时现场流水。
- 重要结论应及时整理进 [Freeze 调试里程碑](rockbase-freeze-milestones.md)，避免只留在本地 ignored 文件里。
