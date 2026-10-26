# LIFE GPS App 文档

| 文档 | 内容 |
| :--- | :--- |
| [GETTING_STARTED.md](GETTING_STARTED.md) | 环境安装、第一次运行、真机 BLE 调试 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 模块分层、Transport、同步引擎、存储 |
| [../../Doc/Software/Android_Dev_Guide.md](../../Doc/Software/Android_Dev_Guide.md) | 零 Android 经验的完整学习路线 |
| [../../Doc/Software/Android_App_Plan.md](../../Doc/Software/Android_App_Plan.md) | 产品/调试工具完整规划 |

## 当前状态

- Gradle 工程已生成（单模块 `:app`，Compose，`devDebug`）。
- 已包含硬件调试首页、串口/BLE 日志、传感器、GPS、硬件自检、Flash、命令终端（假数据）。
- 下一步：真实 BLE / USB 串口接入；稳定后再拆 `core:*` / `feature:*`。
- 编译统一走 GitHub Actions `app.yml`，本地不要求 Android SDK/Gradle。
