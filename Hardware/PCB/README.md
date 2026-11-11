# PCB

## 目标

- 4 层高密度沉金板，整机约 52×36×9mm。
- 板角为 **GNSS 无源天线 `U7`** 与 **BLE Chip 天线 `L3`** 各自的全层净空区；两者间距 ≥15~20mm。
- 50Ω 共面波导馈线；GNSS 馈电端预留 0402 π 型调谐位（DNP）与低结容 RF ESD。
- 底层预留产测点：`SWDIO`、`SWDCLK`、`DEBUG_TX`、`DEBUG_RX`、`VBAT`、`1V9`、`GND`（`1PPS` 已裁撤）。
- **nRF54L15 射频接地强约束**：`Pin32 VSS_PA` 仅顶层连 `Pin49`；ANT 第一级并容地只接 `Pin32`；第二级并容地与除底层外所有地层隔离。

## 相关文件

- [Stackup 侧视图](../../Doc/Hardware/Stackup/hardware_stackup_sideview.svg)
- [V0.1 原理图](../Schematic/Life_GPS_SCH_V0.1.SVG)
- [系统架构图](../../Doc/Hardware/Architecture/hardware_architecture.svg)

## 检查清单

- [ ] 天线净空区无铜、无走线、无器件（含底层）
- [ ] BLE 与 GNSS 天线间距/隔离满足仿真与实测
- [ ] VSS_PA / 中心焊盘 / 两级匹配电容接地按 Datasheet Figure 180 注记实现
- [ ] GNSS 前端 DNP 分支（至 U5/Q1）残桩 ≤5mm 或串 DNP 0Ω
- [ ] SPI Flash 8MHz 走线短、包地、远离射频
- [ ] 1.9V/1.8V 电源域分区（建议把 R5/R6/R7 的 0Ω 换成磁珠实现 π 型滤波）
- [ ] 1.9V 轨总有效输出电容 ≤40µF（SGM6040A 推荐上限），或实测上电涌流合格
- [ ] 电池分压 1M/330k 高阻节点做护环、远离 DC-DC 与射频、可清洗工艺
- [ ] 调试串口 100Ω 串阻
- [ ] 产测点可被顶针夹具接触
- [ ] DRC/ERC 通过，Gerber 与 BOM 版本一致
