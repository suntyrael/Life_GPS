# 软件文档索引

| 文档 | 内容 | 状态 |
| :--- | :--- | :--- |
| [Android_App_Plan.md](Android_App_Plan.md) | Android 调试工具/产品终端规划、BLE 协议、同步、存储、CI | v0.1 |
| [Android_Dev_Guide.md](Android_Dev_Guide.md) | 零 Android 经验的环境搭建、学习路线、第一个练手任务 | v0.1 |
| [BLE_Protocol.md](BLE_Protocol.md) | 协议基线（最终由 `protocol/lifegps_protocol.yaml` 生成） | 草案 |
| [../../Firmware/README.md](../../Firmware/README.md) | 固件工程、Zephyr 构建、分区表 | 规划 |
| [../../protocol/README.md](../../protocol/README.md) | 协议 SSOT、校验器、Golden Vectors | v0.1 |

## 软件工作流

1. **协议**：先改 `protocol/lifegps_protocol.yaml`，跑 `make proto-check`。
2. **固件**：按 `Firmware/docs/PARTITIONS.md` 实现 Flash/OTA；用 `native_sim` 跑协议与算法单测。
3. **App**：先做 `dev` flavor 调试工具，再做 `prod` 产品 UI；用 Fake Transport / Python BLE Sim 解耦硬件。
4. **发布**：tag `app-v*` / `fw-v*`，GitHub Actions 产出签名 APK/AAB、固件 hex/bin/DFU、校验和。

> 当前仓库处于“文档 + 骨架”阶段：Android Gradle 骨架已生成（单模块 `:app`，硬件调试页面 + 假数据），Zephyr 工程待生成。所有编译在 GitHub Actions 完成；先按 [Android_Dev_Guide.md](Android_Dev_Guide.md) 了解流程，再下载 CI APK。
