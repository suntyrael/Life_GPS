# Golden Vectors

存放固件与 App 共同读取的字节级测试向量。

## 计划覆盖

| 文件 | 内容 |
| :--- | :--- |
| `trajectory_basic.bin` | 32B 轨迹记录，普通值 |
| `trajectory_delta_wrap.bin` | 跨 0x00/0xFF 的 delta 回绕 |
| `beacon_basic.bin` | 16B Beacon 事件 |
| `command_get_status.bin` | 命令帧 + CRC16 |
| `data_chunk_trajectory.bin` | Data 块 + 7 条记录 |
| `day_package_basic.bin` | 完整日包（header + 压缩块 + footer） |
| `manifest.json` | 每个向量的期望解码结果/CRC |

## 规则

- 二进制文件小（KB 级），直接签入 Git。
- C（native_sim）与 Kotlin（JVM 单测）读同一文件，断言同一结果。
- 每次协议变更必须新增/更新向量；CI 双端跑。
