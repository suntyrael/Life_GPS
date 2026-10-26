# Life_GPS BLE 协议基线（草案）

> 状态：v0.1 草案。最终正文由 [`protocol/lifegps_protocol.yaml`](../../protocol/lifegps_protocol.yaml) 生成；本文件暂作为人读入口，详细设计见 [Android_App_Plan.md](Android_App_Plan.md) §5。

## 已冻结的约定

| 项 | 约定 |
| :--- | :--- |
| 字节序 | 小端 |
| 目标 MTU | 247（Data 块最大 payload 228B） |
| 单条轨迹记录 | 32B，CRC-8（poly 0x07 / init 0x00） |
| 单条 Beacon 事件 | 16B，CRC-8 |
| 命令帧 | magic 0xA5 + 8B header + payload + CRC-16/CCITT-FALSE |
| 数据块 | magic 0x5A + 14B header + payload + CRC-16/CCITT-FALSE |
| 日包 | `LGD1`，48B header + 压缩块 + 8B footer（CRC-32） |
| 时间 | Life_GPS 2020 Epoch + 亚秒毫秒 |
| 坐标 | WGS-84，度 × 1e7 |
| OTA | MCUboot + SMP；内部 slot0 896KB + 外部 Flash 1MB slot1 |

## 下一步

1. 评审 `protocol/lifegps_protocol.yaml` 的字段/opcode/UUID。
2. 跑 `make proto-check` 校验结构体尺寸、opcode 唯一性、UUID 格式。
3. 生成 C 头文件、Kotlin 模型与完整的 Markdown 表格。
4. 双方用同一套 Golden Vectors 做单测，再开始固件/App 联调。

## 修改流程

- 只改 `protocol/lifegps_protocol.yaml`，不要手改生成物。
- 任何协议变更必须同时更新：YAML、Kotlin/C 生成物、Golden Vectors、固件单测、App 单测。
- 字段只追加，不改变已有字段的长度/含义；破坏性变更升主版本。
