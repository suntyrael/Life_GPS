# protocol-gen

协议工具链：把 `protocol/lifegps_protocol.yaml` 变成固件/App 可用的代码与文档。

## 当前已实现

```bash
python3 tools/protocol-gen/validate.py
```

校验内容：

- UUID 格式与唯一性
- 轨迹 32B / Beacon 16B / 帧头 / 日包头尺寸
- 字段偏移连续、类型尺寸正确
- stream / opcode / telemetry id 唯一
- 命令方向合法

## 计划生成物

| 输出 | 路径 | 消费方 |
| :--- | :--- | :--- |
| C 头文件 | `Firmware/include/generated/lifegps_protocol.h` | 固件；含 `_Static_assert` |
| Kotlin 模型/常量 | `App/core/protocol/src/main/kotlin/.../GeneratedProtocol.kt` | App |
| 协议 Markdown | `Doc/Software/BLE_Protocol.md` | 人读 |

## 规则

- 生成物**签入仓库**，CI 用 `make proto-check` 检查漂移。
- 先改 YAML，再生成；禁止手改生成物。
- 字段只追加；破坏性变更升主版本。
- Golden Vectors 放 `protocol/vectors/`，C 与 Kotlin 读同一份文件。

## 本地准备

```bash
python3 -m pip install -r tools/protocol-gen/requirements.txt
python3 tools/protocol-gen/validate.py
```
