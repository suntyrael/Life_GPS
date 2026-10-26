# Schematic

## 当前文件

| 文件 | 说明 |
| :--- | :--- |
| `Life_GPS_SCH_V0.1.SVG` | V0.1 正式投板原理图（嘉立创 EDA 导出） |

## 约定

- 源工程（立创 EDA / KiCad）在 M1 阶段归档到本目录或 `Hardware/` 子目录；导出的 SVG/PDF 一并保留。
- 网表、BOM、坐标文件必须来自同一版本；文件名带版本号与日期。
- 原理图变更后同步更新：
  - 根 `README.md` §2 硬件配置基线表、§5 引脚表；
  - `Firmware/boards/lifegps/` 的 DTS/pinctrl；
  - `Doc/Hardware/README.md` 的选型说明。
- 双模组兼容位（MAX-M10S / LC97H）与双 BOM（直通 / LNA+SAW）必须在原理图注释和 BOM 中同时标注。

## 查看

SVG 可直接用浏览器打开；需要标注/对比时用 Inkscape 或 EDA 工具。
