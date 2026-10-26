# tools

| 目录 | 作用 | 状态 |
| :--- | :--- | :--- |
| `protocol-gen/` | 校验/生成协议 C、Kotlin、Markdown | 校验已实现，生成器待做 |
| `ble-sim/` | Python BLE 外设模拟器，让手机连“假设备” | 规划 |
| `ci/` | 链接检查、固件体积/分区门禁、发布脚本 | 链接检查已实现 |

## 使用

```bash
make proto-check
make docs-check
```

工具本身尽量只用 Python 标准库 + PyYAML，避免给固件/Android 工具链增加依赖。
