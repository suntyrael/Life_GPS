# PCB

## 目标

- 4 层高密度沉金板，整机约 52×36×9mm。
- 板角为 GNSS Chip 天线独立全层净空区；2.4GHz BLE 天线与 GNSS 天线严格隔离。
- 50Ω 共面波导馈线 + 0402 π 型匹配位。
- 底层预留 8 个产测点：`SWDIO`、`SWCLK`、`DEBUG_TX`、`DEBUG_RX`、`VBAT`、`3V3`、`1PPS`、`GND`。

## 相关文件

- [Stackup 侧视图](../../Doc/Hardware/Stackup/hardware_stackup_sideview.svg)
- [V0.1 原理图](../Schematic/Life_GPS_SCH_V0.1.SVG)
- [系统架构图](../../Doc/Hardware/Architecture/hardware_architecture.svg)

## 检查清单

- [ ] 天线净空区无铜、无走线、无器件（含底层）
- [ ] BLE 与 GNSS 天线间距/隔离满足仿真与实测
- [ ] SPI Flash 8MHz 走线短、包地、远离射频
- [ ] 1.9V/1.8V 电源域分区与磁珠滤波
- [ ] 电池分压 1M/330k 高阻节点防漏电/防污染
- [ ] 调试串口 100Ω 串阻
- [ ] 产测点可被顶针夹具接触
- [ ] DRC/ERC 通过，Gerber 与 BOM 版本一致
