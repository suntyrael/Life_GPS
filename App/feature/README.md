> 当前实现：第一阶段是 **单模块** `:app`，所有页面在 `app/src/main/java/com/lifegps/app/debug/`。本文件描述 M2 的拆分目标，尚未生成 `feature:*` Gradle 模块。

# feature 模块

每个 feature 是一个独立 UI 能力，共享 `core`，不互相依赖。

| 模块 | 内容 | 优先级 |
| :--- | :--- | :--- |
| `debug` | GATT 浏览器、命令终端、实时遥测、Flash/Journal、磁校准、Bring-up | P0（dev） |
| `devices` | 设备列表、状态、设置、配对 | P0 |
| `sync` | 同步进度、断点、诊断 | P0 |
| `map` | 轨迹、PDR 分段、POI、海拔剖面、离线区域 | P1 |
| `timeline` | 日历、热力图、统计 | P2 |
| `settings` | 导出、备份、OTA、隐私、关于 | P1 |

## 约定

- feature 之间不直接引用；共享逻辑下沉到 `core`。
- `dev` flavor 才编译 `feature/debug`；`prod` 不包含破坏性调试入口。
- 每个 feature 至少有一个 `README.md` 说明入口、状态与测试方式（脚手架时补）。
