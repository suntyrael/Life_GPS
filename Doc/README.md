# Life_GPS 文档中心

本目录只放**文档**，工程文件分别放在 `Hardware/`、`Firmware/`、`App/`、`protocol/`。

## 快速导航

| 目录 | 内容 | 入口 |
| :--- | :--- | :--- |
| `Doc/Hardware/` | 芯片规格书、硬件设计索引、Stackup、系统架构图 | [Hardware/README.md](Hardware/README.md) |
| `Doc/Software/` | Android 规划、零基础开发指南、BLE 协议基线 | [Software/README.md](Software/README.md) |
| `Hardware/` | 原理图、PCB、BOM、生产文件 | [../Hardware/README.md](../Hardware/README.md) |
| `Firmware/` | Zephyr / nRF Connect SDK 固件、分区表 | [../Firmware/README.md](../Firmware/README.md) |
| `App/` | Android 调试工具与产品终端 | [../App/README.md](../App/README.md) |
| `protocol/` | 协议 SSOT（YAML + Golden Vectors） | [../protocol/README.md](../protocol/README.md) |

## 建议阅读顺序

1. 仓库根 [README](../README.md)：硬件基线、系统架构、Roadmap。
2. [Doc/Hardware/README.md](Hardware/README.md)：器件选型、电源域、引脚与避坑。
3. [Doc/Software/Android_Dev_Guide.md](Software/Android_Dev_Guide.md)：没有 Android 经验时的环境与学习路线。
4. [Doc/Software/Android_App_Plan.md](Software/Android_App_Plan.md)：调试工具 → 产品终端的完整规划。
5. [protocol/README.md](../protocol/README.md)：BLE 协议契约与校验方式。

## 文档约定

- 移动文档后必须更新相对链接；CI 的 `docs.yml` 会做链接检查。
- 规格书统一放 `Doc/Hardware/Datasheets/`；不要把 PDF 散落在根目录。
- 软件文档放 `Doc/Software/`；协议正文由 `protocol/lifegps_protocol.yaml` 生成，避免手改漂移。
- 构建产物、Gerber 大包、APK/HEX 不进 Git，放 Release 资产；大文件按需用 Git LFS。
