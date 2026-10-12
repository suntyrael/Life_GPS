# Life_GPS 设计资料与器件规格书索引 (Doc/)

本目录归档了 **Life_GPS** 超低功耗轨迹与惯导记录仪项目核心硬件元器件的原厂官方数据手册（Datasheet）及规格书。在进行原理图绘制、PCB 布局（特别是射频天线与阻抗控制）和固件驱动开发时，请务必以本目录下的原厂规范为准。

---

## 1. 核心芯片规格书清单

| 文件名 | 器件型号 | 厂商 | 类别 / 封装 | 核心作用与原厂真实关键参数 |
| :--- | :--- | :--- | :--- | :--- |
| **`ATGM336H-F8N76_Datasheet.pdf`** | **ATGM336H-F8N76** | 杭州中科微电子 | 全系统多频 GNSS<br>*(LCC-18, 10.1×9.7mm)* | **基于新一代 AT9880 旗舰 SoC**，支持 L1+L2/L5 多频，支持 BDS-2/BDS-3/GPS/GLONASS/Galileo/IRNSS/QZSS，**176 通道**，跟踪灵敏度 **-167dBm**，定位精度 **< 1.0m (CEP50)**，更新率最高 **20Hz**，典型功耗 **44mA@3.3V**，默认波特率 **115200bps** |
| **`nRF52840_Product_Specification_v1.11.pdf`** | **nRF52840** | Nordic Semiconductor | 主控 SoC<br>*(QFN48)* | 64MHz Cortex-M4F (带FPU)，1MB Flash / 256KB RAM，BLE 5.4 / Long Range，系统控制与 PDR 算法运行。**（注意：QFN48 内部 VDD 与 VDDH 短接，仅支持 Normal 供电模式，无原生 USB VBUS/D+/D- 引脚）** |
| **`LSM6DSV16X_Datasheet.pdf`** | **LSM6DSV16X** | STMicroelectronics | 6轴 IMU<br>*(LGA-14, 2.5×3.0mm)* | 3轴加速度计+3轴陀螺仪，三核边缘智能（MLC + FSM + SFLP 15µA 姿态融合），带 Qvar 静电检测与 Sensor Hub（挂载磁力计） |
| **`SGM6040A_Datasheet.pdf`** | **SGM6040A** | 圣邦微电子 (SGMICRO) | 纳安级降压 DCDC<br>*(TDFN-8L / WLCSP-6B)* | **静态电流低至 60nA**（关断电流 25nA），750mA 输出，输入 1.8V~5.5V，支持 100% 占空比旁路模式，用于将锂电池电压高效转换为 3.0V/3.3V 为 nRF52840 及系统常通域供电 |
| **`SGM2036_Datasheet.pdf`** | **SGM2036-3.3** | 圣邦微电子 (SGMICRO) | 低噪声受控 LDO<br>*(SOT-23-5)* | 300mA 输出能力，超低压降，高 PSRR，低噪声，**带高电平 EN 使能脚（关断漏电流仅 10nA）**，由 MCU GPIO 控制独立为 GNSS 模组供电与硬切断 |
| **`SGM4056_Datasheet.pdf`** | **SGM4056** | 圣邦微电子 (SGMICRO) | 线性锂电充电 IC<br>*(TDFN / SOIC-8-EP)* | 1A 线性充电芯片，支持过压保护 (OVP 6.8V/10.5V)，防倒灌 (<1µA 关断漏电)，带充电状态指示引脚 |
| **`LIS2MDL_Datasheet.pdf`** | LIS2MDL | STMicroelectronics | 3轴磁力计<br>*(LGA-12, 2.0×2.0mm)* | 超低功耗电子罗盘，挂载于 LSM6DSV16X Sensor Hub（I2C Master），辅助 PDR 航向角绝对校准，消除零速偏航漂移 |
| **`BMP388_Datasheet.pdf`** | BMP388 | Bosch Sensortec | 气压高度计<br>*(LGA-10, 2.0×2.0mm)* | 高精度低功耗气压传感器，相对精度 ±0.08 hPa（约 ±0.5米），用于辅助高度解算、爬坡与室内楼层识别 |
| **`MAX2659_GPS_LNA.pdf`** | MAX2659 | Analog Devices / Maxim | 低噪声放大器 (LNA)<br>*(DFN-6, 1.5×1.0mm)* | 增益 20.5dB，超低噪声系数 0.8dB，用于补偿小型无源陶瓷贴片天线微弱射频信号链路衰减 |
| **`W25Q128JV_SPI_Flash.pdf`** | W25Q128JV | Winbond | SPI NOR Flash<br>*(SOIC-8, 16MB)* | 128M-bit 外部存储器，支持标准/双通道/四通道 QSPI，深休眠电流 1µA，用于海量轨迹点与蓝牙扫描事件黑匣子存储 |

---

## 2. 硬件开发关键注意事项

### 2.1 全圣邦微电源架构设计要点
* **MCU 供电降压 DCDC（SGM6040A）**：
  * **极限静态功耗**：静态待机电流仅为 **60 nA**（0.06 µA），关断电流 25nA；在 1µA 微载荷下转换效率依然高达 80%，峰值转换效率高达 90%+。
  * **电压配置**：通过单颗电阻连接 VSET 引脚至 GND 即可精确选择 16 种预设输出电压（推荐配置为 3.0V 或 3.3V），直接接入 nRF52840 的 VDD（以及 Flash、IMU）。
  * **STOP 引脚**：具备 STOP 硬件静音引脚，在 MCU 执行高灵敏度 ADC 采样时可瞬态暂停开关动作消除纹波。
* **GPS 独立供电 LDO（SGM2036-3.3）**：
  * GNSS 模组瞬态工作电流约 44mA，射频敏感。SGM2036 提供高达 300mA 洁净低纹波电源；
  * MCU 通过 GPIO `P0.04 (GNSS_LDO_EN)` 控制 SGM2036 的 EN 引脚。当 GPS 处于休眠期时，拉低 EN 引脚，SGM2036 进入关断状态，**关断漏电仅 10nA**，彻底切断 GPS 电源；
  * GNSS 模组的 `VBAT (Pin 6)` 连接常通 3.3V（电流仅 15µA），确保 RTC 和 SRAM 星历长效保持，每次 LDO 上电均能实现 **< 1 秒极速热启动**。

### 2.2 nRF52840 QFN48 封装硬件限制与时钟/调试规划
* **外部晶振设计**：
  * **32MHz 高频晶振（HFXO）**：必须外接至专用管脚 `XC1 (Pin 35)` 和 `XC2 (Pin 36)`，匹配 12pF 电容，频差 $\le \pm 10\text{ ppm}$，这是 BLE 射频收发必须的时钟源；
  * **32.768kHz 低频晶振（LFXO）**：连接至 `P0.00/XL1 (Pin 2)` 和 `P0.01/XL2 (Pin 3)`，为微安级待机 RTC 计时提供基准。
* **独立调试串口（DEBUG UART）**：
  * 分配 `P0.09` (Pin 29) 作为 `DEBUG_TXD`，`P0.10` (Pin 30) 作为 `DEBUG_RXD`，波特率 115200bps，引出至板载调试测试焊盘；
  * 这两个引脚在芯片上电时默认具备 NFC 保护电容，在固件 UICR 配置中必须设置 `NFCPINS = 0`，以禁用 NFC 并释放为标准 GPIO。
* **无原生 USB 引脚**：
  * QFN48 封装未引出 VBUS、D+、D- 引脚，Type-C 仅作为 5V 充电接口，数据同步依托 BLE 5.0 (2Mbps PHY) 高速无线通道或调试串口。
