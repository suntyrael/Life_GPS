# boards/lifegps

自定义 nRF52840 板级定义（脚手架时生成）：

| 文件 | 作用 |
| :--- | :--- |
| `board.yml` | 板名、SoC、架构 |
| `Kconfig.lifegps` | 板级选项 |
| `lifegps_defconfig` | 默认配置 |
| `lifegps.dts` | Flash/SPI/I2C/GPIO/按键/LED/传感器 |
| `lifegps-pinctrl.dtsi` | 引脚复用 |
| `board.c` / `board.h` | 初始化钩子（外部 Flash DPD 释放等） |

## 必须对齐的硬件基线

- MCU：nRF52840-QFAA，QFN48
- GNSS：MAX-M10S（兼容 LC97H）
- IMU：LSM6DSVTR + LIS2MDL + BMP388
- Flash：W25Q512NW，SPI 8MHz
- 按键：SW7 P0.08 / SW1 P1.08
- LED：三色
- 调试串口：P0.24 / P1.00，115200

引脚以根 README §5 与原理图 `Hardware/Schematic/Life_GPS_SCH_V0.1.SVG` 为准。
