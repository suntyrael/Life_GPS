# protocol（协议 SSOT）

本目录是 Life_GPS 固件与 Android 之间的**唯一协议来源**。

```text
protocol/
├── lifegps_protocol.yaml   # UUID/帧/记录/日包/命令/遥测
├── vectors/                # 字节级 Golden Vectors（C/Kotlin 共用）
└── README.md
```

## 为什么单独放

- 固件（C）与 App（Kotlin）读同一份定义，避免枚举/结构体漂移。
- 生成物签入仓库，CI 用 `make proto-check` 检查是否最新。
- 协议变更可以独立评审、独立 tag，不和业务代码混在一起。

## 当前校验

```bash
python3 -m pip install -r tools/protocol-gen/requirements.txt
python3 tools/protocol-gen/validate.py
```

校验：UUID、32B/16B/帧/日包尺寸、字段偏移、opcode/stream/telemetry 唯一性。

## 变更流程

1. 改 `lifegps_protocol.yaml`。
2. 跑 `make proto-check`。
3. 生成 C/Kotlin/Markdown（生成器待实现）。
4. 更新 `protocol/vectors/` 并让 C/Kotlin 单测同时通过。
5. 提交：YAML + 生成物 + 向量 + 双方单测。

## 兼容规则

- 字段只追加；不改变已有字段长度/含义。
- 删除/改义 = 主版本升级。
- 未知 opcode 返回 `UNSUPPORTED`，客户端隐藏功能而不是崩溃。

## 相关文档

- [BLE 协议草案](../Doc/Software/BLE_Protocol.md)
- [完整设计](../Doc/Software/Android_App_Plan.md) §5
- [固件分区](../Firmware/docs/PARTITIONS.md)
