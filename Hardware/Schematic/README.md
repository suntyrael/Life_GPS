# Schematic

## 当前文件

| 文件 | 说明 |
| :--- | :--- |
| `Life_GPS_SCH_V0.1.SVG` | V0.1 正式投板原理图（**历史版本，主控 nRF52840**） |
| `Main_V0.2`（嘉立创 EDA 工程） | **V0.2 现行原理图**（主控 nRF54L15-QFAA，2026-11-10 更新）；导出 SVG/PDF 待归档 |
| `Life_GPS_V0.2_SCH.pdf` | **V0.2 原理图导出（现行，主控 nRF54L15-QFAA）** |
| `../../Netlist_Main_V0.2_2026-11-11.tel` | **V0.2 网表（2026-11-11 导出，已与原理图同步）** |

## 约定

- 源工程（立创 EDA / KiCad）在 M1 阶段归档到本目录或 `Hardware/` 子目录；导出的 SVG/PDF 一并保留。
- 网表、BOM、坐标文件必须来自同一版本；文件名带版本号与日期。
- 原理图变更后同步更新：
  - 根 `README.md` §2 硬件配置基线表、§5 引脚章节；
  - **`Doc/Hardware/Pinmap_Main_V0.2.md`（引脚唯一真相源）**；
  - `Firmware/boards/lifegps/` 的 DTS/pinctrl；
  - `Doc/Hardware/README.md` 的选型说明。
- 兼容位标注：**GNSS 默认 `MAX-M10S-00B`（唯一采购料）**，`LC79H` 仅备份（涉及 R13/R27/R28 + 待增补的 Pin15 跳线）；射频为双 BOM（直通 / LNA+SAW）。两者都必须在原理图注释和 BOM 中同时标注，且默认装配不得包含备份料。
- 射频网络接地约束（VSS_PA 只连中心焊盘、第二级并容只接底层地）无法用网表表达，必须在原理图注释 + PCB 规则中落实。

## 查看

SVG 可直接用浏览器打开；需要标注/对比时用 Inkscape 或 EDA 工具。
