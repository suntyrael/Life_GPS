> 当前实现：第一阶段是 **单模块** `:app`，代码在 `app/src/main/java/com/lifegps/app/`。本文件描述 M2 的拆分目标，尚未生成 `core:*` Gradle 模块。

# core 模块

`core` 只放可复用、可测试的能力；`feature` 不直接碰 BLE。

| 模块 | 职责 | 依赖 |
| :--- | :--- | :--- |
| `protocol` | 帧、CRC、32B/16B 记录、日包、命令模型；**纯 JVM** | 无 Android |
| `ble` | `Transport` 接口；BLE / USB Serial / Fake / Replay 实现 | Android BLE 库 |
| `sync` | 同步状态机、日包/暂存双通道、断点、重试、指标 | protocol + ble + data |
| `data` | Room、日包文件、AES-GCM、导出、备份 | Android |
| `model` | 时间、坐标、status_flags、统计、下采样 | 纯 Kotlin |
| `designsystem` | 主题、颜色、通用 Compose 组件 | Compose |

## 关键边界

- `protocol` 不依赖 Android，能在 JVM 单测和未来 CLI 中复用。
- `ble` 只负责“字节进、字节出”，不解析业务。
- `sync` 是唯一推进同步游标、写日包、提交 Room 的组件。
- `feature` 只调用用例，不直接操作 GATT。
