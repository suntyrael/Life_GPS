# Life_GPS 设计资料与器件规格书索引 (Doc/)

本目录归档了 **Life_GPS** 超低功耗轨迹与惯导记录仪项目核心硬件元器件的原厂官方数据手册（Datasheet）及规格书。在进行原理图绘制、PCB 布局（特别是射频天线与阻抗控制）和固件驱动开发时，请务必以本目录下的原厂规范为准。

> **版权免责声明**：本目录下的所有 PDF 规格书版权归各原厂（Nordic Semiconductor、中科微电子、意法半导体、圣邦微电子、博世、华邦等）所有，仅供开源工程硬件研发与技术评估参考。

---

## 1. 核心芯片规格书清单

| 文件名 | 器件型号 | 厂商 | 类别 / 封装 | 核心作用与原厂真实关键参数 |
| :--- | :--- | :--- | :--- | :--- |
| **`ATGM336H-F8N76_Datasheet.pdf`** | **ATGM336H-F8N-76** | 杭州中科微电子 | 全系统多频 GNSS<br>*(LCC-18, 10.1×9.7mm)* | **基于新一代 AT9880 旗舰 SoC**，支持 L1+L2/L5 多频，支持 BDS-2/BDS-3/GPS/GLONASS/Galileo/IRNSS/QZSS，**176 通道**，跟踪灵敏度 **-167dBm**，定位精度 **< 1.0m (CEP50)**，更新率最高 **20Hz**，典型功耗 **44mA@3.3V**，默认波特率 **115200bps**。内部集成 LNA，Pin 14 输出 `VCC_RF` 支持直接驱动双频有源陶瓷天线 |
| **`nRF52840_Product_Specification_v1.11.pdf`** | **nRF52840** | Nordic Semiconductor | 主控 SoC<br>*(QFN48)* | 64MHz Cortex-M4F (带FPU)，1MB Flash / 256KB RAM，BLE 5.4 / Long Range，系统控制与 PDR 算法运行。**（注意：QFN48 内部 VDD 与 VDDH 短接，仅支持 Normal 供电模式，无原生 USB VBUS/D+/D- 引脚）** |
| **`LSM6DSV16X_Datasheet.pdf`** | **LSM6DSV16X** | STMicroelectronics | 6轴 IMU<br>*(LGA-14, 2.5×3.0mm)* | 3轴加速度计+3轴陀螺仪，三核边缘智能（MLC + FSM + SFLP 15µA 姿态融合内核），带 Qvar 静电检测与 Sensor Hub（挂载磁力计）。日常计步仅开低功耗加速度计(~30µA) |
| **`SGM6040A_Datasheet.pdf`** | **SGM6040A** | 圣邦微电子 (SGMICRO) | 纳安级降压 DCDC<br>*(TDFN-8L / WLCSP-6B)* | **静态电流低至 60nA**（关断电流 25nA），750mA 输出，输入 1.8V~5.5V，支持 100% 占空比旁路模式，用于将锂电池电压高效转换为 3.3V 为 nRF52840 及系统常通域供电 |
| **`SGM2036_Datasheet.pdf`** | **SGM2036-3.3** | 圣邦微电子 (SGMICRO) | 低噪声受控 LDO<br>*(SOT-23-5)* | 300mA 输出能力，超低压降，高 PSRR，低噪声，**带高电平 EN 使能脚（关断漏电流仅 10nA）**，由 MCU GPIO 控制独立为 GNSS 模组供电与硬切断 |
| **`SGM4056_Datasheet.pdf`** | **SGM4056** | 圣邦微电子 (SGMICRO) | 线性锂电充电 IC<br>*(TDFN / SOIC-8-EP)* | 1A 线性充电芯片，设置 300~400mA 恒流充电，支持过压保护 (OVP 6.8V/10.5V)，防倒灌 (<1µA 关断漏电) |
| **`W25Q512JV_SPI_Flash.pdf`** | **W25Q512JVEIQ** | Winbond (华邦) | 64MB SPI NOR Flash<br>*(WSON-8, 6×8mm)* | **512M-bit (64 Mega-bytes) 超大容量 NOR Flash**，运行于 4MHz~8MHz 低噪声速率，深休眠电流 1µA，10 万次擦写寿命，20 年数据保持。配合夜间截断压缩归档机制，**可容纳长达 7 ~ 14 年的全生命周期一生轨迹** |
| **`LIS2MDL_Datasheet.pdf`** | LIS2MDL | STMicroelectronics | 3轴磁力计<br>*(LGA-12, 2.0×2.0mm)* | 超低功耗电子罗盘，挂载于 LSM6DSV16X Sensor Hub（I2C Master），辅助 PDR 航向角绝对校准，消除零速偏航漂移 |
| **`BMP388_Datasheet.pdf`** | BMP388 | Bosch Sensortec | 气压高度计<br>*(LGA-10, 2.0×2.0mm)* | 高精度低功耗气压传感器，相对精度 ±0.08 hPa（约 ±0.5米），用于辅助高度解算、爬坡与室内楼层识别 |

---

## 2. 硬件开发关键避坑指南

### 2.1 64MB NOR Flash 存储架构设计要点
* **容量与寿命**：W25Q512JVEIQ 为标准 64MB (512Mb) SPI NOR 架构，相比 NAND Flash 无坏块管理（BBM）和 ECC 负担，支持标准页编程（256 字节）与扇区擦除（4KB）。单扇区寿命 100,000 次，每天擦除 1 次可支持超过 270 年。
* **低噪声 SPI 频率**：配置为 4MHz ~ 8MHz 时钟，杜绝高次谐波串入 GNSS L1/L5 频段。
* **两阶段存储与夜间归档**：白天在活动暂存区顺序追加 32 字节原始二进制点；夜间（凌晨 02:00~03:00 深度静止期）截断、运行轻量 heatshrink 算法压缩归档、原子提交后擦除暂存区，实现整个设备物理生命周期（7~14年）的持久免擦覆盖。

### 2.2 射频前端与双频有源天线架构
* **废弃单频 L1 LNA（如 MAX2659）**：MAX2659 仅针对 1575.42MHz 设计，在 L5（1176.45MHz）频段严重衰减失配，严禁用于本双频系统。
* **推荐天线方案**：采用微型**双频有源陶瓷贴片天线**，射频馈线直连模组 Pin 11 `RF_IN`。天线内部低噪放由模组 Pin 14 `VCC_RF`（3.3V，受控输出且带短路保护）供电。天线随模组自动通断电，无额外板级静态功耗。

### 2.3 GNSS 掉电时的“倒灌（Back-powering）”防护
* **偷电风险**：当 SGM2036 LDO 切断时，若 MCU 的 UART TXD 或 PPS 引脚维持高电平，电流将通过模组内部 ESD 钳位二极管反向倒灌向未受电的 VDD，导致模组处于半通电异常态并严重漏电。
* **硬件防护**：MCU 与模组连接的 `TXD`、`RXD`、`PPS` 信号线上**全串联 1kΩ 限流保护电阻**。
* **固件时序**：在拉低 `GNSS_LDO_EN` 前，固件必须先行注销 UART，并将所有对应 GPIO 设为浮空输入（High-Z）或低电平输出；上电后延时 10ms 待 LDO 稳定再初始化 UART。

### 2.4 电平统一与 nRF52840 QFN48 限制
* **电压域统一**：全板统一锁定 **3.3V 逻辑电平**。SGM6040A DCDC 与 SGM2036 LDO 均配置为 3.3V 输出，杜绝电平超限风险。
* **供电模式**：QFN48 内部 VDD 与 VDDH 短接，仅支持 Normal 模式，严禁直接输入单节锂电池 4.2V。
* **无原生 USB**：QFN48 未引出 VBUS、D+、D-，数据同步依托 BLE 5.0 (2Mbps PHY) 高速无线通道或调试串口。
* **调试串口**：独立引出 `DEBUG_TXD (P0.09)` 和 `DEBUG_RXD (P0.10)` 测试焊盘，UICR 配置 `NFCPINS = 0`。
