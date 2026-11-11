# Hardware

> 状态：**Main_V0.2 原理图基线（2026-11-10，主控 nRF54L15-QFAA）已冻结，4 层 PCB Layout 进行中**。  
> 硬件配置以根 [README](../README.md) §2 基线表为 SSOT；**引脚以 [`Doc/Hardware/Pinmap_Main_V0.2.md`](../Doc/Hardware/Pinmap_Main_V0.2.md) 为唯一真相源**。

## 目录

| 目录 | 内容 |
| :--- | :--- |
| `Schematic/` | 原理图导出、EDA 源文件/网表（后续） |
| `PCB/` | 布局布线、Stackup、Gerber 源 |
| `BOM/` | 物料清单、双 BOM 选项、替代料 |
| `Production/` | Gerber、钻孔、坐标文件、装配图、产测文件 |

## 相关文档

- [硬件设计资料索引](../Doc/Hardware/README.md)（器件规格书、选型、避坑）
- [系统架构图](../Doc/Hardware/Architecture/hardware_architecture.svg)
- [Stackup 侧视图](../Doc/Hardware/Stackup/hardware_stackup_sideview.svg)
- [V0.1 原理图（历史，nRF52840）](Schematic/Life_GPS_SCH_V0.1.SVG)
- **V0.2 原理图**（嘉立创 EDA 工程，导出件待归档）
- **V0.2 原理图（现行）**：[`Schematic/Life_GPS_V0.2_SCH.pdf`](Schematic/Life_GPS_V0.2_SCH.pdf)
- **V0.2 网表（已同步）**：`../Netlist_Main_V0.2_2026-11-11.tel`

## 规则

- **不提交** Gerber zip、STEP、装配 PDF 等大二进制到普通 Git；放 `Production/` 或 Release 资产，必要时 Git LFS。
- 原理图/PCB 变更必须同步：根 README 基线表、引脚表、`Firmware/boards/lifegps/`、`Doc/Hardware/README.md`。
- 任何 BOM 变更（LNA/SAW、MAX-M10S / LC79H 兼容位 R13/R27/R28）必须在 `BOM/README.md` 记录装配选项与互斥关系。
- 原理图里所有器件 `Value` 不得留空（当前 `{Value}` 状态会导致 BOM/固件无法追溯）。
- 生产文件命名：`lifegps-hw-v<版本>-<类型>-<日期>`，附 `SHA256SUMS`。
