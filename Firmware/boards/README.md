# boards/lifegps

自定义 **nRF54L15** 板级定义（脚手架时生成）：

| 文件 | 作用 |
| :--- | :--- |
| `board.yml` | 板名、SoC、架构 |
| `Kconfig.lifegps` | 板级选项 |
| `lifegps_defconfig` | 默认配置 |
| `lifegps.dts` | Flash/SPI/I2C/GPIO/按键/LED/传感器 |
| `lifegps-pinctrl.dtsi` | 引脚复用 |
| `board.c` / `board.h` | 初始化钩子（外部 Flash DPD 释放等） |

## 必须对齐的硬件基线

- MCU：**nRF54L15-QFAA**，QFN48（49 脚含 EP）
- GNSS：**默认 u-blox MAX-M10S-00B**（驱动默认 UBX）；Quectel LC79H 仅作可选兼容位，默认不装配
- IMU：LSM6DSVTR + LIS2MDL + BMP388
- Flash：W25Q512NW，SPI 8MHz
- 按键：SW7 `P1.06`（KEY1_POI，**内部上拉**）/ SW1 `P1.08`（KEY2_MODE，**内部上拉**）
- 充电：`CHG_EN`=`P0.03`（**低有效**）、`CHG_STAT`=`P0.04`（**内部上拉**）
- Flash SPI（`SPIM00`）：SCK `P2.01`、MOSI `P2.02`、MISO `P2.04`、CS `P2.05`（**内部上拉**）
- IMU SPI（`SPIM21` 跨域，需 Constant Latency）：SCK `P2.06`、MOSI `P2.08`、MISO `P2.09`、CS `P2.10`；INT1 `P0.02`
- LED：`P1.10` 红 / `P1.12` 蓝 / `P1.13` 绿（DTC143ZETL 带阻三极管直驱）
- 电池采样：`P1.11` = SAADC **AIN4**（非 AIN0），须做 735kΩ 输入阻抗修正
- 调试串口：`P0.00` / `P0.01`（`UARTE30`），115200

引脚以 **`Doc/Hardware/Pinmap_Main_V0.2.md`**（SSOT）与 V0.2 原理图为唯一依据；V0.1 SVG 仅作历史参考。
