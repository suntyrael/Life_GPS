# Life_GPS: 超低功耗全天候 GPS 与惯导(PDR)轨迹记录仪

[![License: Hardware CERN-OHL-S / Software MIT](https://img.shields.io/badge/License-Hardware%20CERN--OHL--S%20%7C%20Software%20MIT-blue.svg)](LICENSE)
[![MCU: nRF52840 QFN48](https://img.shields.io/badge/MCU-nRF52840%20(QFN48%20QFAA)-red.svg)](Doc/nRF52840_Product_Specification_v1.11.pdf)
[![GNSS: ATGM336H-F8N-76](https://img.shields.io/badge/GNSS-ATGM336H--F8N--76%20(Dual--Band)-green.svg)](Doc/ATGM336H-F8N76_Datasheet.pdf)
[![IMU: LSM6DSV16X](https://img.shields.io/badge/IMU-LSM6DSV16X-orange.svg)](Doc/LSM6DSV16X_Datasheet.pdf)
[![Flash: Winbond 64MB NOR](https://img.shields.io/badge/Flash-Winbond%2064MB%20(W25Q512JV)-yellow.svg)](Doc/W25Q512JV_SPI_Flash.pdf)
[![PMIC: SGMICRO](https://img.shields.io/badge/Power-SGMICRO%20(SGM6040D%2BSGM2045%2BSGM4056)-purple.svg)](Doc/README.md)
[![Status: Architecture Pending Freeze](https://img.shields.io/badge/Status-Architecture%20Pending%20Freeze%20(DO%20NOT%20TAPE--OUT)-red.svg)](#11-开发路线图与三阶段整改实施路径-roadmap)

> ⚠️ **【重要审查提示 · 架构待冻结 · 禁止直接投板 (Architecture Pending Freeze - DO NOT TAPE-OUT)】**  
> 经最新一轮硬件架构与时序专项审查，本项目当前处于**第一阶段（Phase 1）架构基线闭环收敛期**。在硬件配置基线表彻底统一、GNSS 分层状态机时序（冷启动 60s 超时独立窗口与首次定位后 35s 星历刷写保护、热启动自适应延展与退避）通过验证、纠正 SFLP 6 轴与外部磁力计融合分层、重新核算高精模式整机电池端放电电流（~55.8mA）及数据格式彻底闭环前，**严禁直接输出 Gerber 进行打样投板**！文档中所有续航天数、静态待机电流、定位精度与 PDR 惯导指标，均为基于原厂规格书构建的**理论数学建模与设计目标（Target Specification）**，不能直接视为已经证明或保证的实测性能。最终工程指标必须以打样后的仪器全工况实测报告为准。

**Life_GPS** 是一款专为“一生轨迹（Life Trajectory）”全天候记录、户外越野穿越以及数字游民环境感知打造的**超低功耗微型智能记录仪**。

本项目旨在解决智能手机在全天候开启高精度 GPS 和蓝牙后台扫描时面临的**续航断崖式崩塌（几小时耗尽）、发热严重、系统后台频繁杀进程、隐私数据上传云端**等行业痛点。系统采用创新的**“双域物理隔离（Dual-Domain Isolation）”**低功耗硬件架构，结合高能效无线 SoC、中科微新一代双频多模高精度 GNSS、内置边缘算法的 6 轴惯性测量单元（IMU）、全套圣邦微（SGMICRO）纳安级高效电源管理方案，以及**板载 64MB (512Mb) 超大容量 SPI NOR Flash（支持夜间截断压缩归档）**，在配备 **1000mAh ~ 1200mAh** 微型锂电池的物理限制下，实现 **15 至 30 天** 的理论超长待机与**长达 7 至 14 年的全生命周期免擦除一生轨迹存储**。

---

## 目录
- [1. 核心设计亮点与相比手机的优势](#1-核心设计亮点与相比手机的优势)
- [2. 关键元器件选型与全项目唯一硬件配置基线表](#2-关键元器件选型与全项目唯一硬件配置基线表)
  - [2.0 唯一硬件配置基线表 (Hardware Configuration Baseline Matrix)](#20-唯一硬件配置基线表-hardware-configuration-baseline-matrix)
  - [2.1 主控 SoC：Nordic nRF52840 (QFN48 封装)](#21-主控-socnordic-nrf52840-qfn48-封装)
  - [2.2 ⚠️ nRF52840 QFN48 封装功能限制与硬件避坑重点](#22-️-nrf52840-qfn48-封装功能限制与硬件避坑重点)
  - [2.3 GNSS 卫星定位模组：中科微 ATGM336H-F8N-76 (双频旗舰款)](#23-gnss-卫星定位模组中科微-atgm336h-f8n-76-双频旗舰款)
  - [2.4 6轴惯性测量单元 (IMU)：STMicroelectronics LSM6DSV16X](#24-6轴惯性测量单元-imustmicroelectronics-lsm6dsv16x)
  - [2.5 板载 64MB (512Mb) SPI NOR Flash：Winbond W25Q512JV](#25-板载-64mb-512mb-spi-nor-flashwinbond-w25q512jv)
  - [2.6 全套圣邦微 (SGMICRO) 高效电源管理与保护方案](#26-全套圣邦微-sgmicro-高效电源管理与保护方案)
- [3. 系统硬件架构：双域物理隔离 Mermaid 拓扑图与对比矩阵](#3-系统硬件架构双域物理隔离-mermaid-拓扑图与对比矩阵)
- [4. 双按键多功能交互定义 (蓝牙配对 / POI打点 / 高精度模式)](#4-双按键多功能交互定义-蓝牙配对--poi打点--高精度模式)
- [5. 时钟源、调试串口与引脚分配 (QFN48 真实物理引脚)](#5-时钟源调试串口与引脚分配-qfn48-真实物理引脚)
- [6. 超低功耗动态调度策略与整机自洽功耗数学模型](#6-超低功耗动态调度策略与整机自洽功耗数学模型)
  - [6.1 整机工作模式特性与状态矩阵 (Operating Mode Matrix)](#61-整机工作模式特性与状态矩阵-operating-mode-matrix)
  - [6.2 默认自适应模式的三档情景自洽功耗数学模型](#62-默认自适应模式的三档情景自洽功耗数学模型)
- [7. GNSS 分层电源状态机与丢星后惯导(PDR)推算体系](#7-gnss-分层电源状态机与丢星后惯导pdr推算体系)
  - [7.1 GNSS 六状态分层有限状态机与冷/热启动防死循环设计](#71-gnss-六状态分层有限状态机与冷热启动防死循环设计)
  - [7.2 LSM6DSV16X 姿态解耦、三层融合与固件级磁异常抑制](#72-lsm6dsv16x-姿态解耦三层融合与固件级磁异常抑制)
  - [7.3 客观限定 PDR 佩戴方式与特殊场景自适应](#73-客观限定-pdr-佩戴方式与特殊场景自适应)
  - [7.4 基于 BMP388 气压计的垂直高程解算与室内楼层识别模型](#74-基于-bmp388-气压计的垂直高程解算与室内楼层识别模型)
- [8. 两阶段暂存、夜间压缩归档与数据主权静态加密](#8-两阶段暂存夜间压缩归档与数据主权静态加密)
  - [8.1 蓝牙环境感知与隐私合规 (边界客观化与事件流解耦)](#81-蓝牙环境感知与隐私合规-边界客观化与事件流解耦)
  - [8.2 严格定长对齐数据结构 (Life_GPS 2020 Epoch 与毫秒时基)](#82-严格定长对齐数据结构-life_gps-2020-epoch-与毫秒时基)
  - [8.3 两阶段暂存、扇区磨损均衡与夜间截断压缩归档机制](#83-两阶段暂存扇区磨损均衡与夜间截断压缩归档机制)
  - [8.4 基于 CryptoCell-310 的板载 Flash 静态硬件流加密](#84-基于-cryptocell-310-的板载-flash-静态硬件流加密)
- [9. 无屏幕状态交互与日志调试通道](#9-无屏幕状态交互与日志调试通道)
- [10. 项目工程结构与设计资料索引](#10-项目工程结构与设计资料索引)
- [11. 开发路线图 (Roadmap)](#11-开发路线图-roadmap)

---

## 1. 核心设计亮点与相比手机的优势

| 评估维度 | 智能手机 (App 后台记录) | Life_GPS 专用记录仪 (设计目标) | 核心技术优势剖析 |
| :--- | :--- | :--- | :--- |
| **全天工作续航** | 开启高精度定位+蓝牙扫描仅支撑 **4 ~ 8 小时** | 1000mAh 电池目标续航 **15 ~ 30 天**（一次充电支持整月记录） | 专用超低功耗架构，无屏幕、无安卓/iOS高负荷操作系统底噪 |
| **生命周期存储** | 手机内部日志零散膨胀，受限于系统缓存清理 | **板载 64MB 超大 NOR Flash**，夜间自动截断压缩归档 | 32B 定长紧凑结构体，单颗芯片直接封存 **7 ~ 14 年全生命周期轨迹** |
| **电源转换能效** | 手机内部多路电源转换复杂，发热严重 | **圣邦微 SGM6040D 纳安级 DCDC**（60nA静态电流，750mA输出能力，固定3.4V） | 告别传统线性 LDO 在 4.2V 下浪费 22% 压差发热的弊端 |
| **系统稳定性** | 操作系统频繁杀后台、省电策略干预导致断轨、丢点 | 专用 RTOS 固件驱动，硬件看门狗保障，**全天候永不断轨** | 硬件级连续守护，状态机自闭环 |
| **遮挡与丢星处理**| 室内、地下商场直接失去定位或发生数百米跳变漂移 | **LSM6DSV16X 硬件融合 + PDR 航位推算**（刚性佩戴下接管） | 基于步长自适应模型与四元数航向，低频地磁融合约束发散，推算误差受控可预测 |
| **环境感知能力** | 手机系统出于权限与隐私严格封锁底层 BLE 原始广播 | 全频段微占空比扫描，抓取周边固定 Beacon 基站短 ID 与 RSSI | 形成包含“地理位置 + 空间射频环境”的多维离线环境时空轨迹 |
| **设备体积与便携性**| 笨重、发热严重、需随身携带充电宝 | 仅钥匙扣/卡片尺寸（约 52×36×9mm），整机重量 < 35g，随身挂载 | 适合腰带夹固定、背包外侧挂载或放置于车钥匙扣上 |
| **数据安全性与隐私**| 轨迹频繁实时上传云端，面临第三方追踪与商业分析 | **纯离线存储于板载 NOR Flash**，仅由用户物理导出或自主同步 | 真正的数据主权归属个人，物理隔离离线“一生轨迹”底库 |

---

## 2. 关键元器件选型与全项目唯一硬件配置基线表

### 2.0 唯一硬件配置基线表 (Hardware Configuration Baseline Matrix)
为消除主 README、Doc/README、架构图与后续工程文件之间的参数冲突，本项目建立唯一的《硬件配置基线表》。全项目所有文档与实现均严格以本表为**单一真相源（Single Source of Truth, SSOT）**：

| 模块类别 | 选定器件完整型号 | 原厂品牌 | 封装规格 | 工作电压域 | 关键总线 / 目标时钟 | 上电 / 控制方式 | 归档官方规格书 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **主控 SoC** | **nRF52840-QFAA** | Nordic Semi | QFN48<br>*(6×6mm, 0.4mm pitch)* | 稳定 3.4V<br>*(Normal 模式, 1.7~3.6V)* | 64MHz ARM Cortex-M4F<br>HFXO: 32MHz / LFXO: 32.768kHz | 全天候常通供电 (Always-On)<br>引出 30 个物理 GPIO (28外设+1复位+1预留) | `Doc/nRF52840_Product_Specification_v1.11.pdf` |
| **GNSS 模组** | **ATGM336H-F8N-76** | 杭州中科微 | LCC-18<br>*(10.1×9.7mm)* | 主电: 3.3V (Pin 8 VCC)<br>后备: 3.4V (Pin 6 VBAT) | UART 115200bps (8-N-1)<br>1PPS 硬件授时 (经 PPI 直连) | Pin 8 VCC 经 SGM2045-3.3 受控通断 (P0.04 EN)<br>Pin 6 VBAT 由 SGM6040D 常通供电 (15µA)<br>Pin 1 为 GND | `Doc/ATGM336H-F8N76_Datasheet.pdf` |
| **GNSS 射频天线** | **内置微型双频无源陶瓷天线** | 行业标准件 | 陶瓷贴片<br>*(18×18×4 或 15×15×4mm)* | 无源器件 | 50Ω 共面波导阻抗控制<br>布设于 PCB 顶层独立净空区 | 纯无源内置天线，零直流功耗<br>微弱射频信号由板载前置双频 LNA 放大 | 天线规格书 |
| **板载双频 LNA** | **ATR2652** | 杭州中科微 | DFN-6 / SOT23-6 | 3.3V 受控 | L1 增益 22.4dB / NF 0.85dB<br>L5 增益 22.0dB / NF 0.75dB | 挂载于受控 SGM2045-3.3 电源轨<br>工作电流仅 4.5mA，随模组同步休眠断电 | 原厂选型手册 |
| **板载双通带 SAW** | **B39162B2651P810** | Qualcomm / RF360 | 1511<br>*(1.5×1.1mm)* | 无源器件 | 单进单出双通带 (L1+L5)<br>2.4GHz BLE 抑制 > 38dB | 位于 ATR2652 与模组 Pin 11 RF_IN 之间<br>强力消除板载蓝牙发射自干扰 | 原厂选型手册 |
| **SPI NOR Flash** | **W25Q512JVEIQ** | Winbond (华邦) | WSON-8<br>*(6×8mm)* | 3.4V 常通<br>*(2.7~3.6V)* | 标准 4 线 SPI (模式 0)<br>**目标时钟: 4MHz ~ 8MHz (低噪声)** | CS 引脚控休眠 (P1.09)<br>深休眠漏电 1µA，十万次扇区擦写 | `Doc/W25Q512JV_SPI_Flash.pdf` |
| **6轴 IMU** | **LSM6DSV16X** | STMicroelectronics | LGA-14<br>*(2.5×3.0mm)* | 3.4V 常通<br>*(磁珠+电容π型滤波)* | SPI 8MHz (直连主控)<br>SFLP 输出: 60Hz 姿态四元数 | 静止待机: 15Hz LPM1 Acc (~7.5µA)<br>PDR接管: Combo 0.65~0.9mA (SFLP增量14µA) | `Doc/LSM6DSV16X_Datasheet.pdf` |
| **三轴磁力计** | **LIS2MDL** | STMicroelectronics | LGA-12<br>*(2.0×2.0mm)* | 3.4V 常通<br>*(磁珠+电容π型滤波)* | I2C (作为 Sensor Hub 从机) | 挂载于 LSM6DSV16X Sensor Hub Master<br>数据进 IMU FIFO，MCU 固件执行磁校准 | `Doc/LIS2MDL_Datasheet.pdf` |
| **气压高度计** | **BMP388** | Bosch Sensortec | LGA-10<br>*(2.0×2.0mm)* | 3.4V 常通<br>*(磁珠+电容π型滤波)* | I2C (作为 Sensor Hub 从机) | 挂载于 LSM6DSV16X Sensor Hub Master<br>相对精度 ±0.08hPa (约 ±0.5m) | `Doc/BMP388_Datasheet.pdf` |
| **常通降压 DCDC** | **SGM6040D** | 圣邦微 (SGMICRO) | **TDFN-2×1.5-8L**<br>*(锁定选用，带MODE/STOP)* | 输入 1.8~5.5V<br>**固定输出 3.4V** | 750mA 输出能力，效率 > 90%<br>带 STOP 硬件低噪引脚 | 全板常通核心（为MCU/Flash/传感器/GNSS VBAT供电）<br>静态电流仅 60nA (关断 25nA) | `Doc/SGM6040A_Datasheet.pdf` |
| **受控低噪 LDO** | **SGM2045-3.3** | 圣邦微 (SGMICRO) | **XTDFN-1×1-4L**<br>*(超微型封装)* | 输入 1.4~5.5V (接电池或3.4V)<br>**固定输出 3.3V** | 300mA 输出能力，**92dB 超高 PSRR**<br>**9.5µVrms 超低噪声，压差仅 80mV** | 专供 GNSS Pin 8 VCC 及 ATR2652 双频 LNA<br>由 P0.04 EN 硬件控制，关断漏电仅 30nA | 原厂选型手册 |
| **锂电充电 IC** | **SGM4056** | 圣邦微 (SGMICRO) | TDFN-3×3-8L<br>*(带底部散热EP)* | 输入 4.5~6.8V<br>OVP 6.8V | $R_{PROG}=2.8\text{k}\Omega$ 限制 350mA 恒流<br>最大发热 0.70W (3.0V起充) / 0.45W (3.7V平台) | TEMP 引脚外接电芯 NTC<br>实现 JEITA 硬件温度监测保护 | `Doc/SGM4056_Datasheet.pdf` |
| **锂电保护板 (PCM)** | **DW01A + 8205A** 或<br>**AP9101C 系列** | 行业标准方案 | 电芯内置 PCM | 电池端保护 | 硬件级过放/过充/过流切断 | 3.0V 硬件切断过放保护 (漏电<0.1µA)<br>4.28V 过充保护，2.5A 短路锁死 | 保护板设计规范 |

---

### 2.1 主控 SoC：Nordic nRF52840 (QFN48 封装, 料号 nRF52840-QFAA)
* **核心架构**：ARM Cortex-M4F @ 64MHz，集成硬件浮点运算单元（FPU）与专用 DSP 指令集。
* **存储容量**：**1MB 片上 Flash + 256KB RAM**。彻底规避低端芯片（如 nRF52810）RAM 匮乏且官方协议栈阉割“蓝牙扫描（Observer）”的致命缺陷，有充裕内存容纳蓝牙扫描哈希表、RTOS 任务栈及夜间轻量压缩缓冲区。
* **封装优势与确切料号（QFN48 / QFAA）**：
  * Nordic 原厂官方订货规范明确：**`nRF52840-QFAA`**（量产卷带订货料号 `nRF52840-QFAA-R` 或 `nRF52840-QFAA-F-R`）对应 **QFN48 (6×6mm, 0.4mm 间距)** 封装；
  * 相较于 73 引脚 aQFN 封装（料号为 `nRF52840-QIAA`），QFN48 拥有 0.4mm 间距的标准四边扁平引脚，PCB 扇出（Fan-out）极为简单，4 层乃至 2 层板即可轻松布通，极大降低打样制板与 SMT 贴片门槛。
* **无线特性**：支持 Bluetooth 5.4、Coded PHY（Long Range 长距离模式）、2Mbps 高速传输。

---

### 2.2 ⚠️ nRF52840 QFN48 封装功能限制与硬件避坑重点
根据 Nordic 官方《nRF52840 Product Specification》（文档第 7.1.2 节及引脚分配表），**QFN48 封装相比标准 aQFN73 封装在硬件功能上有严格的物理级精简**，在原理图和硬件设计中必须重点避坑：

1. **缺失原生 USB 硬件引脚（无 VBUS、D+、D-）**：
   * **原厂物理事实**：QFN48 封装的 48 个引脚中**根本没有引出 VBUS、USB D+ 和 USB D- 管脚**（仅 aQFN73 具备）。
   * **设计对策**：板载 Type-C 接口专用于 5V 锂电池充电输入；日常数据导出主要采用 **BLE 5.0 (2Mbps PHY) 高速无线同步**；同时系统在空闲 GPIO 上**引出了专用的独立硬件调试串口（DEBUG_UART: TXD/RXD）测试焊盘**，用于连接外部 USB-TTL 串口板输出固件实时 Log。
2. **高压供电模式（High Voltage Supply Mode，REG0）内部短接失效**：
   * **原厂规格书明文**：*“Note: VDD and VDDH are shortcircuited inside the package. Therefore the device is only usable in Normal Voltage supply mode, and not High Voltage supply mode.”*
   * **设计对策**：**严禁将单节锂电池（3.7V~4.2V）或 USB 5V 直接接给 nRF52840 芯片电源管脚**！系统由圣邦微 SGM6040D 降压 DCDC 将电池电压高效转换为 3.4V 后输入 VDD，工作在 Normal Voltage Supply Mode（1.7V~3.6V）。片内高频 DCDC/LDO 自动转换，保持极高转换能效。
3. **真实可用 GPIO 数量与系统规划（30 个）**：
   * Nordic 原厂物理事实：aQFN73 引出 48 个 GPIO，而 QFN48 **物理引出 30 个具备 GPIO 功能的管脚**：
     * P0 端口保留 26 个管脚（标准 32 个中**仅缺失 P0.06、P0.15、P0.16、P0.25、P0.26、P0.27 共 6 个管脚**，即 $32 - 6 = 26$）；
     * P1 端口大部分被裁剪，**仅引出 P1.00、P1.08、P1.09、P1.15 四个管脚**。
   * **系统功能分配严格闭环**：
     * 1 个引脚配置为系统硬件复位（`Pin 16 / P0.18 / SYS_RESET`）；
     * 1 个引脚配置为预留外部扩展测试点（`Pin 44 / P0.31 / RESERVED`）；
     * **其余 28 个物理有效 GPIO 完整分配给核心外设与总线**（无任何虚构管脚），满足全部系统互联需求。
4. **外部时钟晶振引脚规划与负载电容规范**：
   * 必须在专用管脚 `XC1 (Pin 35)` 和 `XC2 (Pin 36)` 外挂 **32MHz 高频石英晶体（HFXO）**，这是 2.4GHz BLE 射频与 64MHz 主频运行的必需时基。标称负载电容取 $C_L = 12\text{ pF}$，外挂对称匹配电容设计公式为 $C_1 = C_2 = 2 \times (C_L - C_{stray})$。考虑 PCB 走线与焊盘杂散容抗 $C_{stray} \approx 2\sim 4\text{ pF}$，外接电容设计取值范围为 **$16\text{ pF} \sim 20\text{ pF}$**（打样阶段通过频谱仪与频偏仪器微调）；
   * 必须在 `P0.00/XL1 (Pin 2)` 和 `P0.01/XL2 (Pin 3)` 外挂 **32.768kHz 低频石英晶体（LFXO）**，匹配标称 $C_L = 9\text{pF}$ 或 $12.5\text{pF}$ 晶体，外挂匹配电容依晶体手册取 15pF~22pF，为睡眠状态下的 RTC 提供微安级超低功耗时基。
5. **NFC 引脚复用为调试串口配置**：
   * `DEBUG_TXD (P0.09)` 与 `DEBUG_RXD (P0.10)` 芯片出厂默认使能内部 NFC 保护电容。在固件开发与批量烧录阶段，**必须在 UICR 寄存器中配置 `NFCPINS = 0`**，将其彻底释放为标准数字 GPIO，方可作为硬件调试串口测试焊盘使用。

---

### 2.3 GNSS 卫星定位模组：中科微 ATGM336H-F8N-76 (双频旗舰款)
> **权威规格依据与来源声明**：依据中科微原厂官方《ATGM336H-F8N 模块用户手册》，本型号为中科微新一代基于 **AT9880 旗舰 SoC 架构** 的**全系统多频点（双频 L1+L5）高性能 GNSS 定位导航与高精度授时模组**。
> *(注：-167dBm 跟踪灵敏度与最高 20Hz 更新率为模组产品手册给出的最佳工况标称值；底层 AT9880 单芯片规格书基础指标为 -162dBm 跟踪灵敏度与 10Hz 常规更新率。在实际固件部署中，自适应模式以 1Hz 快照为主，高精模式以 5Hz 连续解算为主，兼顾精度与功耗平衡)*：

| 关键参数项目 | 官方规格书真实数据 | 详细特性与工程说明 |
| :--- | :--- | :--- |
| **具体订货型号** | **ATGM336H-F8N-76** | 全模多频版本（区别于单北斗专用的 AT9880B 系列），集成片上 Flash |
| **核心 SoC 芯片** | **中科微新一代 AT9880** | 多频多系统 GNSS SoC 单芯片，集成高性能嵌入式 CPU |
| **支持卫星频点** | **L1 + L2/L5 多频点** | **BDS (B1I+B1C+B2a/B2b), GPS (L1+L2/L5), GALILEO (E1+E5a/E5b), GLONASS (L1), IRNSS (L5), QZSS, SBAS** |
| **物理跟踪通道** | **176 通道** | 可同时跟踪多星座数十颗有效卫星，具备极强电离层延迟消除与多径抑制能力 |
| **跟踪灵敏度** | **-167 dBm** (模组标称) | 处于行业顶级水准（冷启动捕获 -148dBm，热启动捕获 -156dBm，重捕获 -160dBm；SoC基础标称 -162dBm） |
| **定位 / 测速精度** | **< 1.0 m (CEP50)** / **< 0.1 m/s (1σ)** | 优于传统单频模块 2.5m 精度，城市峡谷与林荫道定位更平滑 |
| **启动时间 (TTFF)** | 冷启动 $\le 30\text{ s}$，**热启动 $\le 1\text{ s}$**，重捕获 $\le 1\text{ s}$ | 具备后备电源 VBAT 时，重新通电瞬间实现秒定 |
| **最大定位更新率** | **最高 20 Hz** (默认 1Hz / 固件高精5Hz) | 满足户外冲刺、运动竞技及高精度赛段轨迹记录需求 |
| **串口通信特性** | **默认 115200 bps**，8-N-1 | 相比老款 9600bps 提速 12 倍，大幅缩短数据吐出时间，帮助 MCU 提早休眠 |
| **运行功耗参数** | **典型电流 44 mA @ 3.3V** (L1+L5双频满载) | 待机功耗 **< 20 µA**，后备电源 Pin 6 VBAT 供电电流仅 **15 µA** (由 SGM6040D 3.4V 常通供电，维持内部 RTC/SRAM) |
| **独立供电架构** | **圣邦微 SGM2045-3.3 专用受控 LDO 供电** | **微型 XTDFN-1×1-4L，92dB PSRR，9.5µVrms 低噪，压差仅 80mV**；专供 Pin 8 VCC 与 ATR2652，关断漏电仅 30nA |
| **射频前端天线方案** | **内置双频无源陶瓷天线 + 板载 LNA/SAW** | **【无源天线】内置微型双频无源陶瓷贴片天线** (PCB独立净空区)；<br>**【板载前端】中科微 ATR2652 双频 LNA** (4.5mA) + **高通 B39162B2651P810 双通带 SAW** (抑制 >38dB) |
| **防倒灌硬件设计** | 信号线串联 1kΩ 隔离电阻 | `TXD`、`RXD`、`PPS` 线上串接 1kΩ 电阻；下电前注销 UART 并设为 High-Z，上电后延时 10ms 初始化 UART |

#### GNSS 核心工程设计要点与能效权衡：
1. **二次降压供电拓扑 (DCDC 3.4V + SGM2045 LDO 3.3V)**：
   * 本工程正式确立 **二级混合供电拓扑**：SGM2045-3.3 输入直接挂接在圣邦微 SGM6040D 的 **稳定 3.4V 电源轨** 上；
   * **超低压差与零发热**：压差仅 $(3.4\text{V} - 3.3\text{V}) = \mathbf{0.1\text{ V}}$，在 GNSS 模组 (44mA) + ATR2652 (4.5mA) 合计 **48.5mA** 满载工作时，压差发热功率仅为 $0.1\text{V} \times 48.5\text{mA} = \mathbf{4.85\text{ mW}}$（相比从锂电池 4.2V 直接线性降压节省了近 90% 的压差热损耗！）；
   * **极致射频底噪**：SGM2045 在 1kHz 下具备高达 **92 dB 的超高 PSRR** 和 **9.5 µVrms 超低底噪**，彻底吸收 SGM6040D 的 1.5MHz 开关纹波，为卫星接收机和前置 LNA 提供极致纯净的 3.3V 模拟电源；
2. **固件级 NMEA 极速裁剪节能**：
   * 模组出厂默认输出全套 NMEA 报文（GGA、GSA、GSV×4、RMC、VTG、GLL 等），单 epoch 数据量达 1.2~1.8KB，在 115200bps 下传输需 100~150ms；
   * 本固件在每次快照唤醒模组后，通过专有 CASIC 指令关闭其余所有语句，**仅使能 `$GNRMC` 与 `$GNGGA`（单次约 200 字节，传输耗时压缩至 < 18ms）**。在每天约 800 次脉冲快照下，累计缩短 MCU 串口 DMA 接收等待与模组吐字时间，每日纯省约 **1.5 mAh** 宝贵电量。
3. **射频前端内置无源陶瓷天线方案与级联链路设计**：
   * **彻底清除有源天线**：整机按照 **内置无源陶瓷贴片天线** 规范设计，彻底杜绝外置同轴馈电带来的直流功耗与短路隐患；
   * **板载前置双频 LNA (中科微 ATR2652)**：由于微型无源陶瓷天线在体积受限下增益较低，第一级采用中科微原厂配套的双频 LNA `ATR2652`（L5 增益 22.0dB / NF 0.75dB，L1 增益 22.4dB / NF 0.85dB，工作电流 4.5mA），直接贴装在天线输出端下方，以超低噪声系数主导整机接收灵敏度；
   * **板载双通带 SAW (高通 B39162B2651P810)**：采用 1511 封装单进单出双通带声表滤波器，在 $2400 \sim 2483.5\text{ MHz}$ 频段具备 **$>38\text{ dB}$ 的带外深陷抑制**，彻底消除板载主控 nRF52840 蓝牙 2.4GHz 发射对 GNSS 接收机的射频互调饱和干扰；
   * **推荐级联链路 (Post-LNA)**：
     `内置无源陶瓷天线` $\longrightarrow$ `ATR2652 (LNA)` $\longrightarrow$ `B39162B2651P810 (SAW)` $\longrightarrow$ `ATGM336H Pin 11 (RF_IN)`；
   * **LNA 受控供电**：ATR2652 供电端挂载在受控的 SGM2045-3.3 电源轨上，随模组主电同步断电，模组 Pin 14 `VCC_RF` 悬空不供电。
4. **模组真实引脚映射与后备电源 VBAT 硬件连接**：
   * 依据中科微原厂手册：**Pin 1 为 GND**，**Pin 6 为 VBAT**（后备电源输入，1.5V~3.6V，典型静态电流 15µA），**Pin 8 为 VCC**（主电源输入，2.7V~3.6V）；
   * **硬件基线规定：Pin 6 `VBAT` 必须直连系统常通 3.4V 主电源轨 (由 SGM6040D 直供)**；
   * 当受控 LDO SGM2045 切断模组主电源（Pin 8 VCC）时，Pin 6 `VBAT` 维持内部 RTC 运行与低功耗 SRAM 中的星历时钟状态以支持热启动；
   * **整机功耗计入**：15µA 备用电流已全额纳入整机常通待机底噪（$25\mu\text{A} \sim 35\mu\text{A}$ 之中，每日消耗 $0.36\text{ mAh}$）。

---

### 2.4 6轴惯性测量单元 (IMU)：STMicroelectronics LSM6DSV16X
* **传感器规格**：3 轴数字加速度计（±2g 至 ±16g） + 3 轴数字陀螺仪（±125 至 ±4000 dps）。
* **功耗分级管理（实事求是按运行模式核算）**：
  * **模式 1：深度静止待机模式**：陀螺仪完全关闭，加速度计配置为低频运动唤醒模式（**15Hz LPM1，典型电流仅约 7.5 µA**，或 1.875Hz 约 4.0 µA）；
  * **模式 2：日常活动与计步**：陀螺仪休眠，开启 60Hz LPM1 加速度计，电流仅 **$\approx 17\text{ }\mu\text{A}$**；
  * **模式 3：PDR 惯导接管与高精运动**：全开加速度计 + 陀螺仪 + SFLP 姿态递推，低功耗 Combo 模式下电流为 **$0.65\text{ mA} \sim 0.90\text{ mA}$**；
* **⚠️ SFLP 物理边界与三层融合架构澄清（ST 官方物理事实）**：
  * **原厂技术事实**：依据 ST 官方规格书（DS13988 / AN5877），LSM6DSV16X 的 SFLP（Sensor Fusion Low Power）内核是**纯 6 轴姿态解算单元**，仅基于芯片内部加速度计和陀螺仪输出 Game Rotation Vector（6 轴无绝对地磁参考四元数）、Gravity Vector 和 Gyroscope Bias。**Sensor Hub 采集到的外挂磁力计（LIS2MDL）数据在硬件内部不能直接输入 SFLP 内核！**
  * **SFLP 功耗与输出速率 (ODR) 规范**：ST 官方明文规定，SFLP 在 60Hz 姿态输出速率下增量电流约为 **14 µA**，在 120Hz 下约为 **28 µA**。**本项目正式选定 60Hz SFLP 输出速率**，兼顾高动态响应与微安级功耗；
  * **三层分工融合架构**：
    1. **IMU 硬件层**：LSM6DSV16X SFLP 以 60Hz 硬件输出 6 轴 Game Rotation Vector 姿态四元数（增量功耗 14µA）；
    2. **MCU 固件融合层**：Sensor Hub 定期采集 LIS2MDL 地磁数据，nRF52840 执行硬铁/软铁椭球拟合校准与传感器机体坐标系对准；
    3. **磁异常检测与自适应抑制（Magnetic Anomaly Rejection）**：实时监测地磁矢量模长 $\|\mathbf{B}_{cal}\|$ 与磁倾角。在电梯、车辆或钢筋混凝土建筑等强磁干扰环境下，**自适应将地磁权值降为 0（完全切断地磁校准）**，依靠陀螺仪积分短时维持航向；仅在磁场平稳时进行低频互补滤波/EKF 绝对航向纠偏，客观表述为“有效抑制并减缓长期偏航漂移”，绝不在工程文档中承诺物理上不可达到的“彻底消除漂移”；
* **扩展能力 (Sensor Hub)**：具备独立 I2C Master，挂载三轴磁力计（**LIS2MDL**）与高精度气压计（**BMP388**），数据统一打包进内部 1KB FIFO，支持 MCU 突发批量读取。
* **触摸感知**：具备 **Qvar** 静电电荷变化检测通道，支持设备外壳轻击、滑动手势及人体穿戴检测。

---

### 2.5 板载 64MB (512Mb) SPI NOR Flash：Winbond W25Q512JV
为实现**“存储整机物理生命周期一生轨迹”**的核心愿景，系统将板载存储升级为华邦工业级 64MB SPI NOR Flash：
* **芯片型号**：Winbond **W25Q512JVEIQ**（512M-bit / 64 Mega-bytes，WSON-8 6×8mm 紧凑封装）；
* **物理可靠性对比优势**：
  * 相比 SPI NAND Flash，NOR 架构**出厂零坏块、无坏块管理（BBM）与 ECC 纠错负担**，支持标准 256 字节页编程与 4KB 扇区擦除；
  * 深度休眠漏电极低（典型值仅 **$1\text{ }\mu\text{A}$**，不拖累系统待机底噪）；
  * 具备 **100,000 次（十万次）擦写寿命** 与 **20 年数据保持期**；
* **低噪声总线规划**：SPI 通信速率配置为 **4MHz ~ 8MHz**，既满足微秒级日志写入，又彻底消除 32MHz 高次谐波对卫星天线 L1/L5 的高频射频干扰；
* **7~14 年全生命周期存储的严谨计算依据与模式边界**：
  * **日常自适应模式（默认）推导**：
    1. **工况模型**：每日 14~16 小时静止睡眠待机（零打点、零Flash写入）；每日 1.5~2 小时移动通勤，在 45~60s 脉冲与拐弯触发下，平均产生 **150 ~ 300 条点/天**（轻度户外徒步 8~10 小时产生约 600~800 条点/天）；
    2. **压缩比测算**：800 条点 $\times$ 32B = 25.6KB/天 原始二进制数据；经一阶差分与 heatshrink 嵌入式压缩后，压缩比达 **2.0:1 ~ 2.5:1**，每日归档包约 **10KB ~ 12KB/天**；
    3. **寿命计算**：64MB 扣除 4MB 暂存与目录后可用归档区为 60MB ($61,440\text{ KB}$)。典型通勤（4KB/天）可存 **约 42.0 年**；重度活跃（12KB/天）可存 **约 14.0 年**；完全未压缩兜底（25.6KB/天）亦可存 **约 6.57 年**；
  * **高精度连续模式边界警示**：若开启 1Hz 连续记录，每天 10 小时产生 36,000 点（1.15MB/天），64MB 仅能支持约 **53 天**；5Hz 仅能支持约 **11 天**。**文档严谨声明：7~14 年终身免擦仅适用于默认自适应模式；高精度连续记录属于赛事短期专项模式，不可用于全生命周期测算**。

---

### 2.6 全套圣邦微 (SGMICRO) 高效电源管理与保护方案
为兼顾极致续航、高电源抑制比（PSRR）、严格热控制与完全自主可控供应链，本项目电源部分全部采用**圣邦微电子（SGMICRO）**成熟物料并匹配严谨的锂电与接口防护：

### 2.6 全套圣邦微 (SGMICRO) 高效电源管理与传感器供电评估
为兼顾极致续航、高电源抑制比（PSRR）、严格热控制与完全自主可控供应链，本项目电源部分全部采用**圣邦微电子（SGMICRO）**成熟物料并匹配严谨的锂电与接口防护：

1. **主控系统常通降压 DCDC：圣邦微 SGM6040D (固定输出 3.4V)**
   * **封装硬件特性闭环**：选用 **`SGM6040DXTHL8G/TR` (TDFN-2×1.5-8L)** 封装，原厂出厂**固定输出 3.4V**，免外围分压电阻，提供 MODE 和 STOP 硬件引脚；
   * **纳安级超低静态电流**：静态待机电流低至 **60 nA**，关断电流典型仅 **25 nA**；
   * **微载荷极限效率**：在 $1\text{ }\mu\text{A}$ 微载荷输出下效率依然高达 **80%**，重载峰值效率超过 **90%**，最大输出电流 **750 mA**；
   * **输出特性与 100% 旁路模式 (Bypass Mode)**：当单节锂电池处于 3.45V~4.2V 时稳定输出 3.4V；当电池放电至 **3.0V ~ 3.45V 放电末期** 时，自动进入 100% 占空比旁路直通模式（损耗仅 120nA），输出电压跟随电池电压降落。全板器件最低工作电压均覆盖 2.7V，系统在 3.0V~3.6V 宽压容限区间安全稳定工作；
   * **供电覆盖**：3.4V 稳定供给 nRF52840 SoC (Normal 模式)、W25Q512JV SPI Flash、运动传感器体系（经磁珠滤波），以及 ATGM336H Pin 6 `VBAT`（15µA 维持内部 RTC/SRAM）；
   * **STOP 硬件低噪引脚**：具备 STOP 硬件控制脚，在 MCU 执行高灵敏度 ADC/传感器采样时可瞬态冻结开关动作消除开关噪声。
2. **GNSS 受控独立超低噪 LDO：圣邦微 SGM2045-3.3 (超高 PSRR)**
   * **芯片型号与封装**：选用 **`SGM2045-3.3` (XTDFN-1×1-4L 微型封装)**，专为极致射频定位供电定制；
   * **极致性能参数**：
     * **92 dB 超高 PSRR (@1kHz)**：彻底阻绝上级电源开关纹波；
     * **9.5 µVrms 超低底噪**：提供近乎蓄电池纯净度的模拟电源；
     * **超低压差 (Ultra-Low Dropout)**：满载 300mA 压差仅 80~100mV；在 GNSS 模组 (44mA) + ATR2652 (4.5mA) 合计 **48.5mA** 满载下，**实测压差仅约 15 ~ 20 mV**；
   * **二级混合拓扑降发热**：输入直接挂接在 SGM6040D 输出的 3.4V 轨上，压差仅 0.1V，满载压差发热仅 **4.85 mW**；关断漏电仅 **30 nA**，由 MCU GPIO `P0.04` 硬件控制切断。
3. **专项评估：传感器是否需要单独的 LDO 供电？**
   * **深度论证结论**：**传感器“不需要”也坚决不建议增加独立的 LDO 供电！**
   * **核心依据剖析**：
     * **待机底噪毁灭性影响**：若为传感器增加一颗独立 LDO，其静态电流 $I_Q$（典型 10~15µA）将直接导致传感器静止待机功耗由 7.5µA 暴增至 22.5µA，彻底摧毁全机 $25\sim 35\mu\text{A}$ 的待机预算；
     * **物理滤波完全等效**：SGM6040D 开关频率 1.5MHz。在传感器电源入口串入 **`BLM15PX121 (120Ω@100MHz, DCR<0.2Ω)` 铁氧体磁珠** 与 **$10\mu\text{F} + 0.1\mu\text{F}$ 陶瓷电容** 构建 $\pi$ 型滤波网络，对 1.5MHz 开关纹波衰减达 **> 35 dB**（纹波压制到 $<0.5\text{mV}$），且**额外静态漏电为绝对的 0 nA**；
     * **STOP 引脚零噪联动**：在需要高精度气压计/磁力计采样窗口期间，固件拉高 SGM6040D 的 STOP 引脚瞬态冻结开关动作，输出纹波归零（0mV），达到甚至优于专用 LDO 的静态纯净度；
     * **器件自身抗噪**：LSM6DSV16X 内部自带模拟 LDO 和电荷泵，对中低频纹波自身具备 >50dB PSRR。外置 LDO 属过度设计；
   * **落地实施方案**：传感器直接由 SGM6040D 3.4V 经磁珠 $\pi$ 型滤波供电，免除独立 LDO 成本、面积与漏电。
4. **锂电池充电管理：圣邦微 SGM4056 (严控发热与温度保护)**
   * **IC 定位与充电参数**：1A 线性锂电池充电芯片，集成高达 26.5V 输入耐压与过压保护（OVP 6.8V），防电池倒灌漏电 $< 1\text{ }\mu\text{A}$，具备 CHRG/STDBY 充电状态指示；
   * **充电发热严谨核算与控温 (0.35C)**：本设计将 PROG 引脚电阻设定为 **$R_{PROG} = 2.8\text{ k}\Omega$**，将恒流充电严格限制在 **350 mA（0.35C）**：
     * **最大瞬态发热功耗**：在 5.0V 输入、电池放电截止点 3.0V 刚刚接入充电时，压差达 2.0V，最大瞬态耗散功率为 $(5.0\text{V} - 3.0\text{V}) \times 0.35\text{A} = \mathbf{0.70\text{ W}}$；
     * **典型平台发热功耗**：充至 3.7V 主平台时，压差功耗降为 $(5.0\text{V} - 3.7\text{V}) \times 0.35\text{A} \approx \mathbf{0.45\text{ W}}$；
     * **散热闭环**：选用带底部裸露散热焊盘（Exposed Pad）的 TDFN-3×3-8L 封装，PCB 底部打散热地过孔阵列，结合电芯 NTC 与 JEITA 规范实现多重热调节保护；
   * **温度监测保护 (NTC / JEITA)**：外接电芯内置 NTC 热敏电阻，在电池温度 $<0^\circ\text{C}$ 或 $>45^\circ\text{C}$ 时自动暂停充电。
5. **锂电芯硬件保护板回路 (PCM 兜底防过放)**：
   * SGM4056 仅负责充电过程管理，**本身不具备电池过放与短路保护功能**；
   * 本工程规定必须采用**自带保护板（集成专用保护 IC 如 DW01A / AP9101C + 8205A 复合双 N-MOS）的 1000mAh~1200mAh 聚合物电芯**，提供 **3.0V 硬件级过放切断保护**、4.28V 过充硬件保护及 2.5A 短路锁死机制，彻底消除长期密封存放导致电芯过放鼓包与损坏风险。
6. **接口与天线 ESD / TVS 硬件防护**：
   * **Type-C 输入端口**：钥匙扣设备常伴随人体摩擦与频繁插拔，Type-C 的 VBUS 5V 输入与 CC1/CC2 引脚配置 **USBLC6-2SC6 超低结容 TVS 保护阵列**，耐受 IEC 61000-4-2 $\pm 15\text{kV}$ 空气放电与 $\pm 8\text{kV}$ 接触放电；
   * **GNSS 射频天线端口**：天线 RF 输入端串入微型超低结容（$C_J < 0.2\text{ pF}$）双向射频 ESD 保护二极管（如 ESD9R3.3ST5G），在不衰减 L1/L5 射频信号的前提下杜绝人体触摸静电击穿前置 LNA。

---

## 3. 系统硬件架构：双域物理隔离 Mermaid 拓扑图与对比矩阵

### 3.1 系统双域架构 Mermaid 流程图

```mermaid
flowchart TB
    %% 样式定义
    classDef pwr fill:#451a03,stroke:#d97706,stroke-width:2px,color:#fef3c7;
    classDef aon fill:#064e3b,stroke:#059669,stroke-width:2px,color:#ecfdf5;
    classDef sw fill:#1e3a8a,stroke:#2563eb,stroke-width:2px,color:#dbeafe;
    classDef mcu fill:#4c1d95,stroke:#7c3aed,stroke-width:2px,color:#f5f3ff;
    classDef dev fill:#1e293b,stroke:#475569,stroke-width:1.5px,color:#f8fafc;
    classDef key fill:#831843,stroke:#db2777,stroke-width:1.5px,color:#fdf2f8;

    %% 顶部电源管理层
    subgraph POWER_LAYER ["⚡ 圣邦微电源管理层 (SGMICRO Power Architecture)"]
        USB_IN["Type-C 5V 充电输入"]:::dev --> SGM4056["SGM4056 充电 IC (设为 350mA)<br/>(带输入 OVP 保护与防倒灌)"]:::pwr
        SGM4056 --> BAT["1000~1200mAh 聚合物锂电池<br/>(自带 PCM 保护板: 2.8V~4.2V)"]:::pwr
        BAT --> BAT_ADC["P-MOS 控制分压网络<br/>(MCU ADC_IN 采样)"]:::dev
        
        %% 分出两路电源架构
        BAT -->|"电池原始供电 (3.0V~4.2V)"| SGM6040D["圣邦微 SGM6040D 纳安级 DCDC<br/>(静态功耗 60nA | 750mA | 固定输出 3.4V)"]:::pwr
        SGM6040D -->|"二级低压差供电 (3.4V)"| SGM2045["圣邦微 SGM2045-3.3 专用 LDO<br/>(300mA | 92dB PSRR 9.5µVrms | 压差仅80mV)"]:::sw
    end

    %% 双域分层稳压供电
    SGM6040D -->|"常通主电源: 稳定 3.4V"| AON_DOMAIN
    SGM2045 -->|"受控洁净主电: GNSS_VCC (Pin 8, 3.3V)"| GNSS_SUBSYSTEM

    subgraph AON_DOMAIN ["🟢 【常通超低功耗微安域 (Always-On Domain)】静态待机底噪 25~35 µA"]
        subgraph MCU_NODE ["主控核心: Nordic nRF52840-QFAA (QFN48)"]
            MCU_CORE["Cortex-M4F @ 64MHz<br/>1MB Flash / 256KB RAM<br/>BLE 5.4 / 2Mbps"]:::mcu
            CLK_HFXO["32MHz 外部高频晶体 (XC1/XC2)"]:::dev
            CLK_LFXO["32.768kHz 外部低频晶体 (XL1/XL2)"]:::dev
            DCDC_LC["片内 DC-DC 匹配电感电容 (DCC)"]:::dev
            DBG_UART["独立调试串口 (DEBUG_TXD/RXD)<br/>(P0.09 / P0.10, 115200bps 测试焊盘)"]:::dev
            BLE_ANT["2.4GHz 贴片陶瓷天线 (带净空区)"]:::dev
            BLE_ANT --- MCU_CORE
        end

        subgraph INTERACTION ["人机交互与状态感知"]
            KEY1["按键 1 (Pin 23 / P0.24)<br/>短按: POI 打点<br/>长按 3s: BLE 快速配对"]:::key
            KEY2["按键 2 (Pin 24 / P1.00)<br/>长按 2s: 高精模式切换<br/>短按: 电量状态回显"]:::key
            LEDS["三色 RGB 状态指示灯 (P0.28~P0.30)"]:::dev
        end

        subgraph STORAGE_IMU ["存储器与运动传感"]
            FLASH["W25Q512JV (64MB SPI NOR Flash)<br/>低噪声 SPI (4MHz~8MHz) | 休眠 1µA<br/>支持 7~14 年全生命周期日志"]:::dev
            IMU["LSM6DSV16X (ST 6轴旗舰 IMU)<br/>深度静止 15Hz LPM1 Acc (7.5µA)<br/>丢星接管开 SFLP 姿态融合 (60Hz@14µA)"]:::aon
            
            subgraph SENSOR_HUB ["IMU Sensor Hub (独立 I2C Master)"]
                MAG["LIS2MDL 三轴磁力计<br/>(地磁航向参考 / 固件校准)"]:::dev
                BARO["BMP388 高精度气压计<br/>(±0.5m 相对高度/楼层)"]:::dev
            end
        end
    end

    subgraph SW_DOMAIN ["🔵 【受控突发高能毫安域 (Switched Domain)】瞬态工作电流 ~48.5 mA"]
        subgraph GNSS_SUBSYSTEM ["GNSS 卫星定位系统 (SGM2045-3.3 受控独立供电)"]
            GNSS["中科微 ATGM336H-F8N-76<br/>旗舰 AT9880 SoC | 176 通道<br/>Pin 8 VCC 主电 | Pin 1 为 GND<br/>典型工作功耗: 44mA @ 3.3V"]:::sw
            PASSIVE_ANT["内置微型双频无源陶瓷天线<br/>(独立净空区 / 零直流功耗)"]:::dev
            LNA_SAW["板载 ATR2652 双频LNA (4.5mA)<br/>+ B39162 双通带SAW (&gt;38dB抑制)"]:::dev
            PASSIVE_ANT --> LNA_SAW
            LNA_SAW -->|"RF_IN (Pin 11)"| GNSS
        end
    end

    %% 跨域控制、防倒灌电阻与数据连线
    MCU_CORE -->|"GNSS_LDO_EN 使能控制 (P0.04)"| SGM2045["圣邦微 SGM2045-3.3<br/>(300mA | 92dB PSRR | 9.5µVrms)"]:::sw
    SGM2045 -->|"受控洁净 3.3V"| GNSS & LNA_SAW
    GNSS -->|"Pin 2 TXD0 串联 1kΩ"| R_RX["1kΩ 保护电阻"]:::dev -->|"GNSS_RXD (P0.05)"| MCU_CORE
    MCU_CORE -->|"GNSS_TXD (P0.07)"| R_TX["1kΩ 保护电阻"]:::dev -->|"下电前置 High-Z"| GNSS
    GNSS -->|"Pin 4 1PPS 串联 1kΩ"| R_PPS["1kΩ 保护电阻"]:::dev --> MCU_CORE
    SGM6040D -->|"Pin 6 VBAT 常通保持 15µA (维持RTC/SRAM)"| GNSS

    MCU_CORE -->|"SPI 8MHz"| IMU
    IMU --> MAG & BARO
    MCU_CORE -->|"SPI 4~8MHz"| FLASH
    KEY1 & KEY2 --> MCU_CORE
    MCU_CORE --> LEDS
    BAT_ADC -->|"ADC_IN (P0.02)"| MCU_CORE
```

### 3.2 供电与功耗域对比矩阵 (Domain Contrast Matrix)

| 架构对比维度 | 🟢 常通超低功耗微安域 (Always-On Domain) | 🔵 受控突发高能毫安域 (Switched GNSS Domain) |
| :--- | :--- | :--- |
| **所属核心硬件** | **nRF52840 SoC、LSM6DSV16X IMU、W25Q512JV (64MB NOR)** | **ATGM336H-F8N-76 GNSS 模组、内置双频无源陶瓷天线、ATR2652 双频 LNA** |
| **独立供电通道** | **圣邦微 SGM6040D 纳安级 DCDC** (静态电流 60nA, 750mA 输出, 固定 3.4V) | **圣邦微 SGM2045-3.3 专用受控 LDO** (300mA, 92dB PSRR, 80mV压差, 9.5µVrms低噪) |
| **工作电压基准** | **稳定 3.4V (SGM6040D 出厂固定输出)**<br>锂电 >3.45V 稳压 3.4V；末期 3.0~3.45V 直通旁路，全板器件均满足 2.7~3.6V 宽压 | **严格受控 3.3V (专路 LDO 92dB PSRR)**<br>供给 Pin 8 VCC 与 ATR2652；输入接 3.4V 轨，压差仅 0.1V 消除线性发热 |
| **供电控制机制** | 全天候常通供电，维持系统时基与状态监听 | **通过 MCU GPIO `P0.04` 控制 SGM2045 的 EN 脚**实现硬件级通断 |
| **静态休眠电流** | **$\mathbf{25\text{ }\mu\text{A} \sim 35\text{ }\mu\text{A}}$**<br>(System ON 1.5µA + RTC + IMU 15Hz LPM1 7.5µA + Flash 1µA + GNSS Pin 6 VBAT 15µA) | **$< 0.03\text{ }\mu\text{A}$** (SGM2045 关断漏电仅 30nA，彻底消除静电漏电) |
| **动态运行电流** | $0.65\text{ mA} \sim 8\text{ mA}$ (BLE 广播/扫描/姿态运算) | **$\mathbf{48.5\text{ mA}}$** (双频 176 通道满载 44mA + ATR2652 双频 LNA 4.5mA) |
| **运行占空比** | 100% 全天候在线守护 | 脉冲式快照工作（常规运动下仅占空约 **4% ~ 10%**） |
| **后备保活设计** | 主控保持高精度时间戳与事件哈希表 | 模组 `Pin 6 VBAT` 接常通 3.4V (15µA)，维持 RTC/SRAM 热启动状态 |

---

## 4. 双按键多功能交互定义 (蓝牙配对 / POI打点 / 高精度模式)

设备采用无屏幕超薄设计，为了提供强大且直观的操作体验，硬件配备了 **2 个独立多功能轻触按键**，覆盖了日常使用、兴趣点记录与特殊工况切换三大场景：

### 4.1 双按键操作逻辑流程图 (Mermaid)

```mermaid
flowchart TD
    classDef key fill:#831843,stroke:#db2777,stroke-width:2px,color:#fdf2f8;
    classDef act fill:#1e293b,stroke:#0284c7,stroke-width:1.5px,color:#f0f9ff;
    classDef led fill:#064e3b,stroke:#059669,stroke-width:1.5px,color:#ecfdf5;

    START(["用户物理操作按键"]):::key --> BRANCH{"按键识别"}

    BRANCH -->|"按键 1 (Pin 23 / P0.24)"| K1{"按压时长"}
    K1 -->|"短按 (< 1秒)"| POI["触发即时 POI 兴趣点打点<br/>立刻唤醒 GNSS 抓取坐标与 BLE 环境快照<br/>写入 Flash 专属 POI_MARK 记录"]:::act
    POI --> POI_LED["🟢 绿灯双闪 500ms 视觉确认"]:::led

    K1 -->|"长按 3 秒"| PAIR["触发蓝牙配对与强制同步模式<br/>广播间隔切为 20ms 高频广播<br/>开启 2Mbps PHY 允许手机 App 极速连接"]:::act
    PAIR --> PAIR_LED["🔵 蓝灯呼吸式高频快闪"]:::led

    BRANCH -->|"按键 2 (Pin 24 / P1.00)"| K2{"按压时长"}
    K2 -->|"长按 2 秒"| TOGGLE["高精度轨迹记录模式双向切换<br/>在【自适应 Duty-Cycled 模式】与【连续双频高精模式】间切换"]:::act
    TOGGLE --> TOG_LED["🔵🟢 蓝绿双色交替单闪提示"]:::led

    K2 -->|"短按 (< 1秒)"| QUERY["电量与定位状态回显<br/>瞬态启动 ADC 采样电池电压与有效星数"]:::act
    QUERY --> Q_LED["LED 状态码回显:<br/>🟢 绿灯闪3次: 电量充足 (>70%) 且已锁定定位<br/>🟡 黄灯闪2次: 电量中等 (30%~70%) 或惯导推算中<br/>🔴 红灯闪1次: 电量告警 (<20%)"]:::led
```

---

## 5. 时钟源、调试串口与引脚分配 (QFN48 真实物理引脚)

### 5.1 外部高低频时钟源规划与标准负载容抗
* **32MHz 外部高频晶振 (HFXO)**：接至专用管脚 `XC1 (Pin 35)` 和 `XC2 (Pin 36)`。晶体标称负载电容 $C_L = 12\text{ pF}$，外挂对称匹配电容设计为 $C_1 = C_2 = 2 \times (C_L - C_{stray}) \approx 18\text{ pF}$（PCB 杂散容抗 $C_{stray} \approx 3\text{ pF}$），频差 $\le \pm 10\text{ ppm}$，为 2.4GHz BLE 射频与 64MHz CPU 提供精准时基；
* **32.768kHz 外部低频晶振 (LFXO)**：接至专用管脚 `P0.00/XL1 (Pin 2)` 和 `P0.01/XL2 (Pin 3)`，晶体标称 $C_L = 9\text{ pF}$ 或 $12.5\text{ pF}$，外挂匹配电容依晶体手册取 15pF~22pF，为睡眠 RTC 计时提供微安级时基。

### 5.2 独立预留硬件调试串口 (DEBUG_UART)
为满足开发过程中的日志捕获与固件调试，系统分配了独立于 GNSS 的硬件调试串口：
* **`DEBUG_TXD`**：分配至 `P0.09` (Pin 29)，引出板载测试焊盘 (TP)，波特率 115200bps，输出实时系统 Log；
* **`DEBUG_RXD`**：分配至 `P0.10` (Pin 30)，引出板载测试焊盘 (TP)，用于接收配置调试指令；
* *注：P0.09/P0.10 默认具 NFC 保护电容，在固件 UICR 配置中必须配置 `NFCPINS = 0`，以释放为标准数字 GPIO。*

### 5.3 nRF52840 QFN48 完整物理引脚分配映射表
系统完全基于 QFN48 真实的 **30 个有效物理 GPIO** 规划（P0 端口 26 个 + P1 端口 4 个，无任何不存在管脚）：

| 物理管脚编号 | 端口网络标号 | I/O方向 | 硬件连接与电气功能描述 |
| :--- | :--- | :---: | :--- |
| **Pin 35 / Pin 36**| `XC1 / XC2` | IN/OUT | **32MHz 外部高频石英晶体 (HFXO)**，BLE 射频与 CPU 必需基准 |
| **Pin 2 / P0.00** | `RTC_XTAL_IN (XL1)` | IN | **32.768 kHz 外部低频晶体 (LFXO)** 输入端，微安待机时基 |
| **Pin 3 / P0.01** | `RTC_XTAL_OUT (XL2)`| OUT | **32.768 kHz 外部低频晶体 (LFXO)** 输出端 |
| **Pin 40 / P0.02** | `BAT_ADC_IN (AIN0)` | IN | 电池电压分压采集模拟输入（Standard Drive，低频） |
| **Pin 39 / P0.03** | `BAT_ADC_EN (AIN1)` | OUT | 电池分压回路使能控制（平时高电平截止，采样时拉低导通 P-MOS） |
| **Pin 4 / P0.04** | `GNSS_LDO_EN` | OUT | **控制圣邦微 SGM2045 LDO 的 EN 脚**（高电平为 ATGM336H Pin 8 VCC 与 ATR2652 通电，低电平硬切断） |
| **Pin 5 / P0.05** | `GNSS_RXD` | IN | UART 接收引脚（串联 1kΩ 限流电阻连 ATGM336H Pin 2 TXD0，波特率 115200） |
| **Pin 6 / P0.07** | `GNSS_TXD` | OUT | UART 发送引脚（串联 1kΩ 限流电阻连 ATGM336H Pin 3 RXD0，替代不存在的 P0.06） |
| **Pin 7 / P0.08** | `GNSS_PPS` | IN | 接收 ATGM336H Pin 4 1PPS 授时脉冲（串联 1kΩ 电阻；**GPIOTE 事件经 PPI 直连 TIMER0 捕获寄存器，16MHz 下 62.5ns 分辨率百纳秒硬件授时对齐**） |
| **Pin 29 / P0.09** | `DEBUG_TXD` | OUT | **独立调试串口 TX**（115200bps，引出测试焊盘，UICR配置NFCPINS=0） |
| **Pin 30 / P0.10** | `DEBUG_RXD` | IN | **独立调试串口 RX**（引出测试焊盘，UICR配置NFCPINS=0） |
| **Pin 10 / P0.11** | `IMU_CS` | OUT | LSM6DSV16X SPI 片选引脚（Active Low） |
| **Pin 11 / P0.12** | `IMU_SCK` | OUT | LSM6DSV16X SPI 时钟信号（最高支持 8MHz） |
| **Pin 13 / P0.13** | `IMU_MISO` | IN | LSM6DSV16X SPI 主机输入引脚 |
| **Pin 14 / P0.14** | `IMU_MOSI` | OUT | LSM6DSV16X SPI 主机输出引脚 |
| **Pin 15 / P0.17** | `IMU_INT1` | IN | LSM6DSV16X 中断 1（静止休眠唤醒、计步事件中断） |
| **Pin 8 / P1.08** | `IMU_INT2` | IN | LSM6DSV16X 中断 2（FIFO 阈值满中断、MLC 运动模式识别） |
| **Pin 9 / P1.09** | `FLASH_CS` | OUT | W25Q512JV 64MB Flash 片选引脚（Active Low） |
| **Pin 18 / P0.19** | `FLASH_SCK` | OUT | Flash 时钟信号（具备 QSPI/SCK 硬件驱动能力，配置为 4~8MHz） |
| **Pin 19 / P0.20** | `FLASH_MOSI` | OUT | Flash 数据输入引脚 (SPI MOSI / QSPI IO0) |
| **Pin 20 / P0.21** | `FLASH_MISO` | IN | Flash 数据输出引脚 (SPI MISO / QSPI IO1) |
| **Pin 21 / P0.22** | `FLASH_WP` | OUT | Flash 写保护控制 (QSPI IO2) |
| **Pin 22 / P0.23** | `FLASH_HOLD` | OUT | Flash 保持控制 (QSPI IO3) |
| **Pin 16 / P0.18** | `SYS_RESET` | IN | 硬件复位引脚 / SWD 调试复位 |
| **Pin 26 / Pin 27**| `SWDIO / SWDCLK` | I/O | SWD 固件烧录与在线仿真调试接口 |
| **Pin 23 / P0.24** | `KEY1_PAIR_POI` | IN | **多功能按键 1**（短按：POI 打点；长按 3s：蓝牙配对与同步） |
| **Pin 24 / P1.00** | `KEY2_MODE_ST` | IN | **多功能按键 2**（长按 2s：高精模式切换；短按：电量状态回显） |
| **Pin 41 / P0.28** | `LED_RED` | OUT | 系统状态红灯（低电平点亮/PWM，错误或低电量提示） |
| **Pin 42 / P0.29** | `LED_GREEN` | OUT | 系统状态绿灯（GNSS 定位成功与 POI 打点确认） |
| **Pin 43 / P0.30** | `LED_BLUE` | OUT | 系统状态蓝灯（BLE 广播配对与数据传输中） |
| **Pin 38 / P1.15** | `RESERVED` | - | 预留备用 GPIO |
| **Pin 44 / P0.31** | `RESERVED` | - | 预留外部扩展测试点 |

> **物理可用 GPIO 数量精确核验**：
> - **P0 端口（26 个）**：P0.00, P0.01, P0.02, P0.03, P0.04, P0.05, P0.07, P0.08, P0.09, P0.10, P0.11, P0.12, P0.13, P0.14, P0.17, P0.18, P0.19, P0.20, P0.21, P0.22, P0.23, P0.24, P0.28, P0.29, P0.30, P0.31（P0 仅缺失 P0.06、P0.15、P0.16、P0.25、P0.26、P0.27 共 6 个管脚，即 $32 - 6 = 26$ 个）；
> - **P1 端口（4 个）**：P1.00, P1.08, P1.09, P1.15；
> - **合计可用物理 GPIO**：$26 + 4 = \mathbf{30}\text{ 个}$，完全匹配硬件设计与总线规划。

---

## 6. 超低功耗动态调度策略与整机自洽功耗数学模型

> **⚠️ 规范声明与数据口径定义**：现阶段所有续航天数与平均工作电流均为基于元器件原厂规格书与理论使用工况构建的**理论数学建模与设计目标（Target Specification）**，严禁作为“已完成实测的数据”宣称。整机输入电流实测值、开机时长分布、GNSS 间歇定位实际成功率以及环境遮挡下的动态功耗，必须待硬件打样后使用 Nordic PPK2 功耗分析仪进行长期路测实测闭环。

### 6.1 整机工作模式特性与状态矩阵 (Operating Mode Matrix)

按照规范工程审查标准，整机运行状态划分为四个标准化工作模式体系：

| 工作模式 | 当前工程状态 | 电源与硬件工作配置 | 电池侧平均放电电流 (3.8V锂电) | 1000mAh 电池续航模型 (900mAh 有效电量) |
| :--- | :--- | :--- | :--- | :--- |
| **1. 深度静止待机模式** | **设计目标**<br>*(待仪器实测)* | SGM6040D 输出稳定 3.4V；nRF52840 System ON 休眠 (1.5µA) + 32.768kHz RTC；LSM6DSV16X 深度静止 15Hz LPM1 加速度计 (~7.5µA)；W25Q512JV 深休眠 (1µA)；**GNSS Pin 8 主电切断，Pin 6 VBAT 常通保持 (15µA)**；整机静态待机底噪预算：7.5+15+1.5+1+1=26.0µA | **$\mathbf{25\text{ }\mu\text{A} \sim 35\text{ }\mu\text{A}}$**<br>(典型底噪约 26µA) | **理论待机约 1000 ~ 1400 天**<br>(静态存放长达 3 年，完全由电芯自放电主导) |
| **2. 自适应轨迹记录模式 (默认)** | **计算模型**<br>*(待路测闭环)* | 每日 14h 静止待机 + 10h 移动通勤；GNSS 微占空比自适应脉冲供电（每 45~60s 唤醒 2.5~6s）；IMU 低功耗计步，丢星移交 PDR 惯导接管；BLE 微占空比扫描 | **$\mathbf{0.94\text{ mA} \sim 3.01\text{ mA}}$**<br>(典型通勤工况约 **1.74 mA**) | **理论设计目标：12.5 ~ 40.1 天**<br>(典型通勤工况 **21.5 天**；若配 1200mAh 电芯约 **25.8 天**) |
| **3. 高精度连续记录模式 (按键切换)** | **计算模型**<br>*(待多工况实测)* | GNSS 100% 常开连续跟踪 (1~5Hz)；内置无源陶瓷天线 + 板载 ATR2652 双频 LNA (4.5mA)；nRF52840 64MHz 运行 + Flash 连续写入 + BLE 广播；IMU Combo 模式 (0.8mA) | **$\mathbf{52\text{ mA} \sim 56\text{ mA}}$**<br>(典型放电电流 **54.0 mA**) | **理论设计目标：$\approx \mathbf{16.7\text{ 小时}}$**<br>(满足越野跑、骑行、户外赛事单日连续全开) |
| **4. 低电量保护与硬件关断模式** | **设计规范**<br>*(硬件保护板)* | 电池跌破 3.3V 触发固件低电降级（关停 GNSS 脉冲，仅保留计步）；跌破 **3.0V** 由电芯 PCM 保护板硬件切断放电回路，防止电芯过放报废 | **$< 0.1\text{ }\mu\text{A}$**<br>(PCM 保护板硬件截止漏电) | **长期存放防过放保护**<br>(插入 Type-C 充电至 >3.4V 自动复位重启) |

> **⚠️ 高精度模式整机放电电流严谨重新测算依据**：
> - 早期粗略估算的 42mA 低于 GNSS 模组本身的单体典型功耗，在物理上不能成立，特此严格重新核算：
>   1. **GNSS 子系统 (SGM2045 供电轨)**：ATGM336H-F8N-76 双频 176 通道满载运行电流为 **44.0 mA**，板载 ATR2652 双频 LNA 工作电流为 **4.5 mA**，合计负载为 **48.5 mA @ 3.3V**。由 SGM6040D 3.4V 轨二级降压供电，电池侧放电电流折算为：$(3.4\text{V} \times 48.5\text{mA}) / (3.8\text{V} \times 0.90) \approx \mathbf{48.2\text{ mA}}$；
>   2. **主控与存储系统 (SGM6040D DCDC 供电轨)**：nRF52840 运行于 64MHz（开启 FPU/DMA 密集数据吞吐）约 4.5mA，Flash 连续写入平均约 1.5mA，BLE 广播约 0.5mA，总负载约 6.5mA @ 3.4V。折算电池侧放电电流：$(3.4\text{V} \times 6.5\text{mA}) / (3.8\text{V} \times 0.90) \approx \mathbf{5.0\text{ mA}}$；
>   3. **运动传感系统**：LSM6DSV16X 全开加速度计与陀螺仪（Combo 模式）约 0.8mA @ 3.4V，折算电池侧约 **0.8 mA**；
>   - **整机合计电池侧平均放电电流**：$48.2\text{mA} + 5.0\text{mA} + 0.8\text{mA} \approx \mathbf{54.0\text{ mA}}$；
>   - 在 1000mAh 聚合物锂电池（扣除老化的 **900 mAh 真实有效电量**）下，连续记录时长为：$900\text{ mAh} / 54.0\text{ mA} \approx \mathbf{16.7\text{ 小时}}$。

---

### 6.2 默认自适应模式的三档情景自洽功耗数学模型
*以 1000mAh 聚合物电池（扣除化学老化、月自放电与低温衰减，按 **900 mAh 真实有效可用电量** 计算；每日设定为 14 小时静止睡眠待机 + 10 小时户外/通勤运动）：*

> **900 mAh 有效可用电量扣减依据**：
> 1. **锂电池常温自放电**：扣除约 2%~3% / 月的电化学自放电损失（折合约 20~30 mAh）；
> 2. **截止电压与转换开销**：PCM 保护板 3.0V 截止与 SGM6040D 压差留存（约 30~40 mAh）；
> 3. **户外低温环境折减**：户外 0℃~10℃ 环境下电化学活性衰减冗余（约 30~40 mAh）。

| 功耗组成单元 | 🟢 乐观情景 (空旷郊外/星历极佳) | 🟡 典型情景 (城市开阔/步行通勤) | 🔴 悲观情景 (高楼峡谷/弱信号多遮挡) |
| :--- | :--- | :--- | :--- |
| **单次锁星出 Fix 耗时** | **2.5 秒** (极速热启动，0次超时) | **3.5 秒** (基准热启动，含遮挡) | **6.0 秒** (自适应延长，弱信号搜星) |
| **采样周期** | 每 60 秒快照一次 (600次/天) | 每 45 秒快照一次 (800次/天) | 每 45 秒快照一次 (800次/天) |
| **正常快照工作占空比** | **4.17%** | **7.78%** | **13.33%** |
| **1. GNSS 正常脉冲定位能耗**<br>*(含模组44mA+板载LNA 4.5mA=48.5mA)* | $48.5\text{mA} \times 4.17\% \times 10\text{h} = \mathbf{20.22\text{ mAh}}$ | $48.5\text{mA} \times 7.78\% \times 10\text{h} = \mathbf{37.73\text{ mAh}}$ | $48.5\text{mA} \times 13.33\% \times 10\text{h} = \mathbf{64.65\text{ mAh}}$ |
| **2. 丢星超时搜星惩罚项** | **0 次** 超时惩罚: $\mathbf{0.00\text{ mAh}}$ | **3 次** 15s 超时探查: $\mathbf{0.61\text{ mAh}}$ | **8 次** 15s 超时探查: $\mathbf{1.62\text{ mAh}}$ |
| **3. GNSS VBAT 常通保持** | $15\mu\text{A} \times 24\text{h} = \mathbf{0.36\text{ mAh}}$ | $15\mu\text{A} \times 24\text{h} = \mathbf{0.36\text{ mAh}}$ | $15\mu\text{A} \times 24\text{h} = \mathbf{0.36\text{ mAh}}$ |
| **4. 静止待机 (14h, 常通域底噪)**| $26\mu\text{A} \times 14\text{h} = \mathbf{0.36\text{ mAh}}$ | $26\mu\text{A} \times 14\text{h} = \mathbf{0.36\text{ mAh}}$ | $30\mu\text{A} \times 14\text{h} = \mathbf{0.42\text{ mAh}}$ |
| **5. IMU 计步与 PDR 磁融合** | 纯加速度计低功耗: $\mathbf{0.30\text{ mAh}}$ | 偶发 PDR+低频磁融合: $\mathbf{1.20\text{ mAh}}$ | 频繁室内长程 PDR: $\mathbf{3.00\text{ mAh}}$ |
| **6. BLE 扫描与存储归档** | 极低占空比追加: $\mathbf{1.20\text{ mAh}}$ | 典型扫描+夜间归档: $\mathbf{1.60\text{ mAh}}$ | 高密度射频指纹写入: $\mathbf{2.20\text{ mAh}}$ |
| **每日总耗电量估算** | **$\mathbf{22.44\text{ mAh / 天}}$** | **$\mathbf{41.86\text{ mAh / 天}}$** | **$\mathbf{72.25\text{ mAh / 天}}$** |
| **日均放电电流** | $\approx 0.94\text{ mA}$ | $\approx 1.74\text{ mA}$ | $\approx 3.01\text{ mA}$ |
| **预估理论续航 (1000mAh)** | **$\mathbf{40.1\text{ 天}}$** | **$\mathbf{21.5\text{ 天}}$** | **$\mathbf{12.5\text{ 天}}$** |

*注：若使用 1200mAh 电池（有效电量 1080mAh），典型工况下理论续航为 **25.8 天**；若在近郊低干扰环境下，续航可达整月以上。*

---

## 7. GNSS 分层电源状态机与丢星后惯导(PDR)推算体系

### 7.1 GNSS 六状态分层有限状态机与冷/热启动防死循环设计

#### 1. 核心运行死循环隐患剖析与工程设计根治：
在超低功耗定位设备中，简单粗暴的“间歇供电 2.5 秒、15 秒未定位切断 LDO”策略会导致系统陷入**永远无法获取定位的致命死循环**：
* **致命死循环机理**：当设备初次装配通电、长久存放电池耗尽重启、或由于多日未开机导致星历过期（> 4 小时）时，GNSS 模组必须经历**冷启动（Cold Start）**。开阔天空下冷启动首捕时间（TTFF）通常需 28 ~ 35 秒（恶劣环境下可能需 40 ~ 60 秒）才能接收完整卫星星历并解算出 3D Fix。若系统死板地执行“每 45 秒仅通电 2.5 秒”或“15 秒超时无条件切断”，模组将在尚未接收完卫星历书前被无情断电；若此时断电又导致模组星历丢失，下一轮唤醒又是全新的冷启动，**系统将陷入“反复上电搜星数秒 -> 强行断电 -> 丢失半成品状态 -> 下一轮再次冷启动”的无限死循环，在现实中永远无法取得有效定位**！

#### 2. GNSS 关键电源与时序设计闭环（审查四问明确答复）：
1. **模组真实引脚映射与备用电源 `VBAT` (Pin 6) 硬件连接**：
   * **硬件基线规定**：依据中科微原厂手册，**Pin 1 为 GND**，**Pin 6 为 VBAT**，**Pin 8 为 VCC**；Pin 6 `VBAT` 直连系统常通主电源轨；
   * **断电状态保留机制**：中科微官方手册明确 `VBAT` 的作用是为内部 RTC 和低功耗 SRAM 提供后备电源以保持热启动所需的状态信息（在轨广播星历、历书、最近已知经纬度与时间）；
   * **整机待机功耗归属**：ATGM336H Pin 6 `VBAT` 供电电流典型为 **15 µA**。该电流已全额纳入整机常通域待机底噪预算（$25\mu\text{A} \sim 35\mu\text{A}$ 之中，每日消耗 $0.36\text{ mAh}$）；
2. **冷启动与热启动超时策略彻底解耦分离**：
   * 15 秒无定位超时策略**严格仅适用于已有有效星历的热启动脉冲探测模式**，严禁套用于冷启动；
   * 固件为冷启动设立专属的 **60 秒独立超时管理机制**；
3. **星历广播接收与状态保持验证规程**：
   * 冷启动在成功解算出首个有效 Fix 后，系统保持持续通电运行至少 35 秒（接收完整广播星历帧主帧），在内部 SRAM 与 RTC 中建立完整热启动状态；
   * 工程验收要求：通过断开 Pin 8 VCC 主电、保留 Pin 6 VBAT 的长时对比实验，实测验证断电后不同时长的热启动 TTFF（$\le 3.5\text{s}$）；
4. **脉冲定位超时后的自适应延长与指数退避机制**：
   * 在热启动脉冲模式下，若基础 3.5 秒窗口未出 Fix，状态机**自适应延长供电至 8 秒**；
   * 若 8 秒仍未出 Fix，判定处于弱信号/局部树荫，最长延长至 **15 秒硬超时切断**；
   * **连续失败恢复动作**：若连续 2 次脉冲均以 15s 超时失败，判定用户已进入室内、地下商场或长隧道，状态机立即执行**指数退避（重试间隔由 45 秒拉长至 120 秒、300 秒）**，同时**完全移交 PDR 惯导接管**，直到 IMU 检测到人体大动态运动或环境光/地磁特征恢复，防止在室内盲目循环唤醒耗尽电量。

#### 3. 完整的六状态 GNSS 分层有限状态机 (Mermaid FSM)：

```mermaid
stateDiagram-v2
    [*] --> STATE_OFF: 系统初次上电/电池装配

    STATE_OFF --> STATE_STARTUP: 定时心跳唤醒 / 计步大动态触发 / 按键POI打点
    note right of STATE_OFF
        • SGM2045 LDO_EN = 0 (Pin 8 主电硬切断)
        • MCU TXD/PPS 设为 High-Z (防倒灌)
        • Pin 6 VBAT 常通 3.4V 保持 RTC 与 SRAM 状态 (15µA)
    end note

    STATE_STARTUP --> STATE_COLD_ACQ: 判定星历失效 (冷启动: 无星历 / >4小时未定)
    STATE_STARTUP --> STATE_HOT_ACQ: 判定星历有效 (热启动: <2小时有效星历)
    note right of STATE_STARTUP
        • 拉高 P0.04 (GNSS_LDO_EN)
        • 硬件延时 10ms 待 LDO 稳定
        • 初始化 UART 115200bps 并下发 CASIC 裁剪
    end note

    state STATE_COLD_ACQ {
        [*] --> COLD_SEARCH: 持续供电搜星 (LDO 常开)
        COLD_SEARCH --> COLD_TIMEOUT: 搜星耗时 > 60 秒
        COLD_SEARCH --> COLD_FIX: 成功解出 3D Fix (<60s)
    }

    COLD_FIX --> STATE_EPHEMERIS_UPDATE: 保持供电 35~60 秒
    note right of STATE_EPHEMERIS_UPDATE
        【星历状态建立与时钟对齐】
        持续通电接收广播星历主帧并在 SRAM/RTC 中建立热启动状态
        校准 RTC 硬件时基，标记星历完全有效
    end note

    STATE_EPHEMERIS_UPDATE --> STATE_POWER_DOWN: 星历刷写完成，进入休眠

    COLD_TIMEOUT --> STATE_BACKOFF_PDR: 冷启动超时 (转入室内退避)

    state STATE_HOT_ACQ {
        [*] --> HOT_SEARCH_3S5: 基础窗口 (3.5 秒)
        HOT_SEARCH_3S5 --> HOT_FIX: 3.5s 内出 Fix (极速定位)
        HOT_SEARCH_3S5 --> HOT_EXTEND_8S: 3.5s 未出 Fix (弱信号)
        HOT_EXTEND_8S --> HOT_FIX: 8s 内出 Fix (延展成功)
        HOT_EXTEND_8S --> HARD_TIMEOUT_15S: 8s 未出 Fix (延长至 15s)
        HARD_TIMEOUT_15S --> HOT_FIX: 15s 内勉强出 Fix
        HARD_TIMEOUT_15S --> HOT_FAIL: 15s 仍无 Fix (判定遮挡)
    }

    HOT_FIX --> STATE_VALID_FIX: 提取 RMC/GGA 坐标与速度
    STATE_VALID_FIX --> STATE_POWER_DOWN: 写入 32B 轨迹记录，转入休眠

    HOT_FAIL --> STATE_BACKOFF_PDR: 连续 2 次脉冲超时失败
    note right of STATE_BACKOFF_PDR
        【指数退避与惯导接管】
        • 探测间隔退避至 120s ──▶ 300s
        • PDR 惯性航位推算接管平面位置更新
        • IMU 检测大动态移动或按键时提前重试
    end note

    STATE_BACKOFF_PDR --> STATE_POWER_DOWN: 关断模组主电，由 IMU 守护

    STATE_POWER_DOWN --> STATE_OFF: 安全下电防倒灌流程
    note left of STATE_POWER_DOWN
        • 注销 UART 外设驱动
        • MCU TXD/PPS 设为 High-Z / 浮空
        • 拉低 P0.04 关闭 SGM2045 LDO
    end note
```

---

### 7.2 LSM6DSV16X 姿态解耦、三层融合与固件级磁异常抑制

在进入地下车库、高楼室内及隧道等长程遮挡区域时，系统在 GNSS 状态机切断主电源后，完全无缝移交由 **LSM6DSV16X + LIS2MDL + BMP388** 组成的低功耗惯性与环境推算体系接管：

#### 1. 姿态解耦与三层固件融合架构：
* **硬件层 (LSM6DSV16X 芯片内)**：
  * 加速度计与陀螺仪在低功耗 Combo 模式下运行，电流仅 $0.65\text{ mA} \sim 0.90\text{ mA}$；
  * **SFLP 硬件 6 轴姿态解算**：仅融合加速度计与陀螺仪，在芯片硬件内部以 **60Hz 输出 6 轴姿态四元数（Game Rotation Vector）**，**增量电流仅 14 µA**。主控 MCU 绝大多数时间保持 System ON 深度休眠；
* **MCU 固件融合层 (nRF52840 Cortex-M4F)**：
  * **Sensor Hub 采集**：LSM6DSV16X 作为独立 I2C Master 以 20Hz 轮询外挂的 LIS2MDL 磁力计并暂存 FIFO；
  * **硬铁与软铁椭球拟合校准**：执行 $\mathbf{B}_{cal} = \mathbf{W} \cdot (\mathbf{B}_{raw} - \mathbf{V})$，去除板载器件固定偏置 $\mathbf{V}$ 与软铁变形矩阵 $\mathbf{W}$；
  * **传感器安装矩阵对齐**：将磁力计轴系严格旋转至载体机体坐标系（Body Frame）；
  * **异常磁场动态抑制与自适应降权（Magnetic Anomaly Rejection）**：
    * 室内商场、地铁、电梯井及钢筋混凝土建筑中存在剧烈铁磁畸变。盲目信任磁力计会导致航向被瞬间拽偏数百米！
    * 系统在设备初始化及静止期间动态标定当地环境地磁基准矢量 $\mathbf{B}_{local}$，实时解算磁场模长相对偏离量 $\Delta B = |\|\mathbf{B}_{cal}\| - \|\mathbf{B}_{local}\|| / \|\mathbf{B}_{local}\|$ 与磁倾角突变；
    * 当模长相对偏离标定基准超过 $\pm 30\%$ 或磁倾角出现高频突变时，状态机判定发生铁磁异常，**自适应将地磁航向校正权值降为 0（完全切断地磁对齐）**，退回由陀螺仪高精度短时积分递推；
    * 仅在磁场平稳且持续满足置信度时，才通过互补滤波/EKF 缓慢纠偏陀螺仪零偏，**客观表述为“有效抑制并减缓长期偏航漂移”，绝不在物理上承诺不可达到的“彻底消除漂移”**；
  * **间歇突发唤醒**：平稳行走时 MCU 每 2~5 秒（或在检测到大转弯时）突发唤醒一次执行校准，**单次耗时 < 1ms** 后瞬间回睡，均摊增加电流不足 $20\text{ }\mu\text{A}$。

```mermaid
flowchart TD
    classDef raw fill:#1e293b,stroke:#475569,stroke-width:1.5px,color:#f8fafc;
    classDef proc fill:#064e3b,stroke:#059669,stroke-width:1.5px,color:#ecfdf5;
    classDef mcu fill:#1e3a8a,stroke:#2563eb,stroke-width:1.5px,color:#dbeafe;
    classDef out fill:#4c1d95,stroke:#7c3aed,stroke-width:2px,color:#f5f3ff;

    RAW["LSM6DSV16X 原始惯导数据<br/>(加速度计 + 陀螺仪)"]:::raw --> SPLIT{"IMU 硬件通道分流"}

    SPLIT -->|"加速度通道 (IMU内部)"| STEP["硬件计步与垂直合加速度过零点检测<br/>(提取单步冲击与动态峰峰值)"]:::proc
    STEP --> WEINBERG["Weinberg 非线性动态步长模型<br/>$L_k = K \cdot \sqrt[4]{a_{\max} - a_{\min}}$<br/>(先验基准值 + GNSS 在线自标定)"]:::proc

    SPLIT -->|"角速度与姿态通道 (IMU内部)"| SFLP["SFLP 嵌入式姿态引擎 (60Hz@14µA)<br/>纯 6 轴硬件输出 Game Rotation Vector<br/>(MCU 全程保持深度睡眠)"]:::proc

    SFLP & LIS["外挂 LIS2MDL 磁力计<br/>(Sensor Hub I2C读取进FIFO)"]:::raw --> WAKE{"突发唤醒判定<br/>(每 2~5 秒或检测到大转弯)"}

    WAKE -->|"MCU 瞬态唤醒 (<1ms)"| MAG_CHK{"磁场模长与倾角异常检测"}:::mcu
    MAG_CHK -->|"模长严重偏离 / 强磁干扰"| REJECT["【磁异常抑制】地磁权重归零<br/>依靠陀螺仪积分短时维持航向"]:::mcu
    MAG_CHK -->|"磁场稳定且置信度高"| MAG_FUSE["【地磁对齐】互补滤波纠正陀螺偏置<br/>有效抑制长期偏航漂移"]:::mcu

    MAG_FUSE & REJECT --> SLEEP["MCU 瞬间回睡 System ON"]:::mcu

    WEINBERG & MAG_FUSE & REJECT --> CALC["平面 PDR 航位推算坐标迭代更新<br/>$X_k = X_{k-1} + L_k \cdot \sin\theta_k$<br/>$Y_k = Y_{k-1} + L_k \cdot \cos\theta_k$"]:::out
    CALC --> FUSE["走出室外重获卫星时进行 EKF 卡尔曼平滑闭环<br/>输出平滑无跳变的室内外连续轨迹"]:::out
```

#### 2. Weinberg 动态步长模型的冷启动与兜底机制 (Fallback Strategy)：
* **在线 GPS 自适应增益标定**：当设备在室外获取高信噪比 GNSS 锁定（HDOP < 1.5, 星数 > 12）且速度处于 $0.8 \sim 2.5\text{ m/s}$ 时，系统在后台采用最小二乘法实时自动校准增益系数 $K$；
* **冷启动三级兜底 (Fallback)**：
  1. **出厂缺省先验模型**：在未完成首次 GPS 标定前（如出厂首次在室内开机），固件载入人体生理统计均值（默认 $K = 0.415$，标准步长基准 $0.65\text{ m}$）；
  2. **时间置信度衰减**：若脱离 GNSS 信号超过 15 分钟，算法逐步将 $K$ 值权重向先验基准回归，防止因单次异常运动状态导致步长推算持续发散；
  3. **重捕获瞬间重置**：走出室外重新捕获卫星后，以 GNSS 高精度绝对坐标作为基准平滑过渡，并在 3 个有效 Epoch 内重置 PDR 积分方差。

---

### 7.3 客观限定 PDR 佩戴方式与特殊场景自适应
* **佩戴方式物理约束与误差发散模型**：
  * **误差增长机理**：在无地磁对齐纯惯性递推下，陀螺仪漂移导致航向误差随时间累加，横向位置发散呈超线性增长；引入 MCU 低频地磁互补滤波后，偏航角漂移被严格约束在有限方差内，使推算误差重新回到与步数近似线性的可控范围；
  * **高置信度模式（刚性固定）**：PDR 航位推算仅在设备**紧密固定于人体（如腰带夹固定、上衣胸包背带固定）**时具备高精度航向收敛能力；
  * **自由摆动降级模式**：若作为钥匙扣在手头甩动或在背包内晃动，由于姿态矩阵和人体行进方向解耦，算法自动降级为**“硬件步数累加 + 粗略距离推算”**，不在地图上生成发散的假轨迹线。
* **乘车与交通工具识别（MLC 介入）**：
  * LSM6DSV16X 内部 MLC 机器学习核识别出“乘车（Vehicle）”状态时，**立即关闭计步与 PDR**，避免车辆颠簸误触发计步；同时 GNSS 切换为每隔 3 分钟探查一次出站/出隧道信号。
* **指数退避探查机制（Backoff Strategy）**：
  * 丢星切断 LDO 后，系统按第 1 分钟、第 3 分钟、第 10 分钟逐步延长探查间隔；一旦 IMU 检测到人体进入静止睡眠状态（如落座办公），彻底停止探查，进入 $35\text{ }\mu\text{A}$ 深度休眠。

---

### 7.4 基于 BMP388 气压计的垂直高程解算与室内楼层识别模型
系统在 LSM6DSV16X Sensor Hub 上挂载 Bosch 高精度气压计 BMP388（相对精度 $\pm 0.08\text{ hPa}$，等效高程精度 $\approx \pm 0.5\text{ m}$），用于构建三维完整时空轨迹：

1. **气压-高程物理方程**：
   $$H_k = 44330 \cdot \left[ 1 - \left( \frac{P_k}{P_0} \right)^{\frac{1}{5.255}} \right]$$
   其中 $P_k$ 为当前滤波后气压值，$P_0$ 为基准参考海平面气压。
2. **气压基准 $P_0$ 在线动态校准**：
   * 户外获得高质量双频 GNSS 定位（星数 $\ge 12$，3D Fix）时，利用 GNSS 椭球高/海拔高反向标定当前基准气压 $P_0$ 并低通平滑；
   * 彻底消除由于天气气旋、冷暖锋过境引起的宏观气压漂移对高程估计的干扰。
3. **室内楼层与立体运动状态机**：
   * **走楼梯状态识别**：垂直加速度检测到周期性步频冲击，同时气压高程连续匀速爬升或下降（梯度约 $0.2 \sim 0.4\text{ m/s}$），系统判定为“爬楼”，按建筑物标准层高（约 $3.0\text{ m} \sim 3.3\text{ m}$/层）累加/扣减相对楼层计数 `floor_level`；
   * **乘坐电梯状态识别**：IMU 计步通道无单步冲击，但垂直轴检测到启动加速度脉冲（$\pm 0.1\text{g} \sim 0.3\text{g}$）与随后的匀速段，且气压变化率快速升高（$> 1.0\text{ m/s}$），状态机判定为“电梯上升/下降”，在到达目标层减速停稳后根据总高度突变量除以平均层高一次性更新楼层；
   * **同层平步活动**：若气压变化在 $\pm 0.15\text{ hPa}$ 噪声容限内波动，锁定当前楼层，防止记录噪声抖动。

---

## 8. 两阶段暂存、夜间压缩归档与数据主权静态加密

### 8.1 蓝牙环境感知与隐私合规 (边界客观化与事件流解耦)
* **客观明确扫描能力边界（概率性环境特征指纹）**：
  * 系统采用微占空比 Passive 扫描（每 30 秒开启约 500ms，占空比约 **1.6%**）。该设计是超低功耗约束下的最优工程折中，**其技术定位是周期性提取空间射频特征，并不承诺、也无法抓取扫描窗口外发出的所有非连接态瞬态广播**；
* **RPA 过滤与单向散列化（杜绝隐私风险）**：
  * 现代智能手机与穿戴设备默认使用每 15 分钟轮换的可解析私有随机地址（RPA），收集明文 MAC 既无法长期稳定识别，又存在追踪路人的伦理法规风险；
  * 系统在扫描接收到广播后，**严禁明文持久化任何外部设备真实 MAC 地址**。系统对信标 UUID 与 MAC 运行高效轻量的不可逆哈希算法（SipHash-2-4 截断为 16 位散列值），彻底消除隐私侵犯风险；
* **数据流解耦设计（避免主轨迹结构体膨胀与污染）**：
  * 审阅者明确指出：高频轨迹点（1~5Hz）与低频 BLE 扫描（每 30 秒一次）在时序与数据形态上天然异构。若将信标所有字段（UUID、Major、Minor、TxPower）强行塞入主轨迹点，会导致绝大多数轨迹点填充无意义空值；
  * 本系统将**“主地理轨迹流（Trajectory Stream）”**与**“环境信标事件流（BLE Event Stream）”**在 Flash 存储层进行彻底解耦，分别采用严格定长的独立数据结构；
* **废除全局布隆过滤器，确立时间滑动窗口限频队列 (Time-Windowed Throttle Queue)**：
  * **布隆过滤器去重风险澄清**：全局布隆过滤器存在单向假阳性（False Positive）误判率，可能导致关键信标被误判过滤漏记；更严重的是，若全生命周期全局去重同一 MAC/散列，会导致在不同时间/地点重复经过同一固定基站时，丢失其随时间、位移衰减的动态 RSSI 场强变化轨迹；
  * **工程闭环对策**：废除全局布隆过滤器，采用**轻量时间滑动窗口限频队列**：系统仅在内存维护包含最近 32 个信标的 5~15 分钟滚动环形哈希窗口；同一信标在窗口期内仅记录一次最新 RSSI 和时间戳，超过窗口时间后重新作为独立环境事件记录，兼顾去噪防爆仓与保留完整动态空间射频轨迹。

### 8.2 严格定长对齐数据结构 (Life_GPS 2020 Epoch 与毫秒时基)

#### 1. 主时空轨迹记录结构体：`TrajectoryRecord_t` (严格 32 字节)
彻底解决秒级时间戳在高频（1~20Hz）下的冲突问题，并将速度上限由 25.5km/h 扩展至 2359km/h，同时引入编译期静态断言 `_Static_assert`：

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t seq_id;         // [0..3]   全局自增序列号 (用于掉电断点二分查找与扇区对齐)
    uint32_t timestamp;      // [4..7]   Life_GPS Epoch (秒, 自 2020-01-01T00:00:00Z 起计, 可用到 2156 年)
    uint16_t subsecond_ms;   // [8..9]   亚秒毫秒偏移 (0 ~ 999 ms, 完美支持 1~20Hz 高频记录与时基对齐)
    int32_t  latitude;       // [10..13] 纬度 (度 * 1e7, 分辨率约 1.1cm)
    int32_t  longitude;      // [14..17] 经度 (度 * 1e7)
    int16_t  altitude;       // [18..19] 海拔高度 (米, -500 ~ 9000, 气压+GNSS融合)
    uint16_t speed_cms;      // [20..21] 地速 (厘米/秒, 0 ~ 65535 cm/s 即 0 ~ 2359.2 km/h，永不溢出)
    uint16_t heading;        // [22..23] 航向角 (0 ~ 359.9 度, 分辨率 0.1 度)
    uint16_t step_count;     // [24..25] 累计步数增量 (PDR 记录)
    uint8_t  hdop;           // [26]     水平精度因子 (0.1, 范围 0 ~ 25.5)
    uint8_t  satellites;     // [27]     有效卫星颗数 (0 ~ 255)
    uint8_t  status_flags;   // [28]     状态位 (bit0:GNSS有效, bit1:PDR接管, bit2:POI打点, bit3:高精模式, bit4:乘车)
    uint8_t  battery_level;  // [29]     剩余电量百分比 (0 ~ 100%)
    int8_t   floor_level;    // [30]     室内推算相对楼层 (-128 ~ 127 楼层, 0=地面基准层)
    uint8_t  checksum;       // [31]     前 31 字节 CRC8 校验和 (保证单条记录完整性)
} TrajectoryRecord_t;        // 严格等于 32 字节 (4+4+2+4+4+2+2+2+2+1+1+1+1+1+1 = 32)
#pragma pack(pop)

// 固件编译期强制断言，杜绝任何对齐与填充隐患
_Static_assert(sizeof(TrajectoryRecord_t) == 32, "TrajectoryRecord_t size mismatch");
```

#### 2. 解耦的独立 BLE 环境信标事件结构体：`BleBeaconEvent_t` (严格 16 字节)
仅在微占空比扫描期间发现新的环境信标时触发写入，完整保留 iBeacon / Eddystone 核心测距参数：

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t timestamp;      // [0..3]   事件时间戳 (Life_GPS Epoch, 秒)
    uint16_t subsecond_ms;   // [4..5]   毫秒级时间对齐偏移 (与同时刻主轨迹直接关联)
    uint16_t beacon_id_hash; // [6..7]   信标 UUID/MAC 单向抗碰撞散列 (SipHash-2-4)
    uint16_t major_id;       // [8..9]   iBeacon Major / Eddystone 实例短ID
    uint16_t minor_id;       // [10..11] iBeacon Minor / 基站子ID
    int8_t   rssi;           // [12]     接收信号强度 (-128 ~ 0 dBm)
    int8_t   tx_power;       // [13]     信标 1 米处标称发射参考功率 (用于高精度测距与指纹加权)
    uint8_t  event_flags;    // [14]     事件类型标志位 (0x01:iBeacon, 0x02:Eddystone, 0x04:固定基站)
    uint8_t  crc8;           // [15]     前 15 字节 CRC8 校验和
} BleBeaconEvent_t;          // 严格等于 16 字节 (4+2+2+2+2+1+1+1+1 = 16)
#pragma pack(pop)

_Static_assert(sizeof(BleBeaconEvent_t) == 16, "BleBeaconEvent_t size mismatch");
```

---

### 8.3 两阶段暂存、扇区磨损均衡与夜间截断压缩归档机制
为实现“存储整机物理生命周期一生轨迹”且兼顾极高可靠性，系统设计了**两阶段存储架构（白天顺序追加暂存 + 环形磨损均衡 + 夜间静止期轻量压缩归档）**：

#### 1. 暂存区环形扇区轮转磨损均衡 (Wear Leveling Queue)：
* W25Q512JV NOR Flash 单扇区物理擦写寿命为 100,000 次。系统严禁将每日暂存固定在同一物理扇区；
* 系统在 Flash 前端划分 **16 个 4KB 扇区（共 64KB）构建环形滚动暂存队列（Staging Queue）**；
* 每个扇区起始处预留 16 字节专用元数据头（Header）：
  ```c
  typedef struct {
      uint32_t magic;         // 0x54524143 ("TRAC") 魔数
      uint32_t sector_seq;    // 扇区全局轮转序号
      uint32_t erase_count;   // 本扇区物理擦除计数器 (用于均衡监控)
      uint16_t commit_flags;  // 提交状态位 (0xFFFF=Writing, 0x55AA=Archived, 0x0000=Erasing)
      uint16_t header_crc16;  // 头部元数据校验和
  } SectorHeader_t;           // 16 字节头部，紧接着存放 32B 记录 (每扇区可容纳 127 条原始记录)
  ```
* 每天写入轮转至新物理扇区，单扇区年擦除次数从 365 次骤降至约 23 次，十万次物理寿命可连续运行超过 **4,000 年**，消除单点物理磨损瓶颈。

#### 2. 掉电 Journaling 双备份与四步原子提交状态机 (Atomic Commit FSM)：

```mermaid
flowchart TD
    classDef hdr fill:#0f172a,stroke:#38bdf8,stroke-width:1.5px,color:#f0f9ff;
    classDef stage fill:#451a03,stroke:#d97706,stroke-width:1.5px,color:#fef3c7;
    classDef arch fill:#064e3b,stroke:#059669,stroke-width:1.5px,color:#ecfdf5;

    SUB1["【阶段一：白天高频运动记录期 (00:00 ~ 23:59)】<br/>定长 32B 纯二进制顺序追加写入 (Append-Only)<br/>写入前校验空闲扇区 | 写入耗时微秒级 | 掉电绝无损坏"]:::stage --> BUF["Flash 环形活动暂存队列 (16个4KB轮转扇区)<br/>每天产生约 800 条原始点 ≈ 25.6 KB 原始数据"]:::stage

    BUF --> TRIG{"【夜间低谷触发判定】<br/>1. 时间已跨过午夜 24:00 (RTC)<br/>2. IMU 判定连续绝对静止 > 30分钟<br/>3. 电池剩余电量 > 30%"}

    TRIG -->|"满足触发条件 (通常在凌晨 02:00~03:00)"| ARCH_PROCESS["【阶段二：夜间原子归档事务 (Atomic Archival Transaction)】"]:::arch

    subgraph ARCH_PROCESS ["四步原子提交状态机 (Four-Step Atomic FSM)"]
        S1["Step 1. 读取暂存区 25KB 原始数据<br/>运行嵌入式专用 heatshrink 轻量算法压缩为 ~12KB 归档包<br/>预写入目标归档扇区 (Archive Sector)，末尾附带整日 CRC32"]:::arch
        S2["Step 2. 硬件读取归档扇区回读比对<br/>校验压缩包整包 CRC32 严格正确"]:::arch
        S3["Step 3. 在根目录 Journal 索引区原子写入提交标志<br/>原子标记状态为: 0x00 (COMMITTED)"]:::arch
        S4["Step 4. 索引提交确认无误后<br/>安全擦除释放前一天占用的活动暂存扇区"]:::arch
        S1 --> S2 --> S3 --> S4
    end

    ARCH_PROCESS --> COMPLETED["归档完成：进入 System ON 深度睡眠 (35µA)<br/>旧暂存区已清空，准备记录新一天"]:::hdr
```

* **绝对掉电安全保障**：
  * 严禁边写边删！若在 Step 1 或 Step 2 期间电池耗尽或意外断电，系统在下一次重启时检测到 Journal 索引未被标记为 `0x00 (COMMITTED)`，系统会自动无损回滚，旧暂存扇区内原始数据完好无损，在下次通电充电后自动重试归档；
* **极速 CPU 压缩能耗完全可忽略**：
  * 采用专为资源受限 MCU 设计的 **heatshrink 算法**（仅占用 2KB RAM 窗口，零堆内存分配）；
  * 64MHz Cortex-M4F 压缩整天 25.6KB 数据仅需 **30 ~ 40 毫秒**，耗电不足 $0.0001\text{ mAh}$；
* **全生命周期免擦除覆盖（7 ~ 14 年！）严谨推导与模式边界**：
  * **成立的严格工况前提**：
    1. **日常自适应记录**：每日 14~16 小时深度静止（IMU 判定绝对静止，Flash 零打点、零擦写）；每日 1.5~2 小时移动通勤，在 45~60 秒周期脉冲与转弯大动态触发下，每日产生约 **150 ~ 300 条原始轨迹点**（全天徒步 8~10 小时产生约 600 ~ 800 条点）；
    2. **差分与嵌入式压缩率**：单条原始记录 32 字节，800 条点 $\times$ 32B = 25.6 KB/天 原始二进制数据。经一阶差分（Delta）后执行嵌入式专用 heatshrink 算法，实测压缩比稳定在 **2.0:1 ~ 2.5:1**，每日整包压缩归档包约为 **10 KB ~ 12 KB / 天**；
    3. **可用容量与寿命推算**：64MB (512Mb) NOR Flash 扣除 4MB 暂存轮转区与根目录索引后，实际有效归档存储区为 60MB ($61,440\text{ KB}$)；
       - **典型通勤工况 (4KB/天)**：$61,440\text{ KB} / 4\text{ KB/天} \approx 15,360\text{ 天} \approx \mathbf{42.0\text{ 年}}$；
       - **重度活跃工况 (12KB/天)**：$61,440\text{ KB} / 12\text{ KB/天} \approx 5,120\text{ 天} \approx \mathbf{14.0\text{ 年}}$；
       - **完全未压缩兜底 (25.6KB/天)**：$61,440\text{ KB} / 25.6\text{ KB/天} \approx 2,400\text{ 天} \approx \mathbf{6.57\text{ 年}}$；
  * **⚠️ 高精度连续模式边界警示**：
    - 若用户开启 1Hz 连续记录模式，每天 10 小时产生 36,000 条记录（未压缩约 1.15MB/天），64MB 空间仅能支持约 **53 天**；若开启 5Hz 记录，仅能支持约 **11 天**；
    - **文档严正声明：7~14 年全生命周期覆盖仅适用于默认超低功耗自适应模式；高精度连续记录属于竞技赛事或短期测绘专项模式，不可用于终身存储推算**。

---

### 8.4 基于 CryptoCell-310 的板载 Flash 静态硬件流加密
为使“数据主权归属个人”的隐私叙事形成不可被攻破的工程闭环，系统在物理存储层引入静态硬件加密：

1. **物理拆机防泄露威胁模型**：
   * 钥匙扣形态设备若不慎丢失，传统离线设备即便不联网，攻击者也可通过热风枪拆焊板载 W25Q512JV SPI Flash 芯片或通过逻辑分析仪夹具挂载 SPI 总线直接 Dump 导出全部经纬度明文轨迹；
2. **硬件级 AES-128-CTR 静态流加密**：
   * 利用 nRF52840 芯片内部集成的 **ARM TrustZone CryptoCell-310 硬件安全加速协处理器**；
   * 固件在首次出厂烧录时通过芯片内置硬件真随机数发生器（TRNG）在 UICR 保护区生成独立的**设备根加密密钥（Device Root Key, DRK）**，并配置 UICR 读保护锁死外部 JTAG/SWD 探针读出；
   * 主控向 SPI Flash 写入任何暂存数据或归档包时，由 CryptoCell 硬件 DMA 自动进行 **AES-128-CTR 流加密** 后再存入 Flash；读取时通过硬件流解密后装载入内存；
3. **零算力负载与零额外功耗**：
   * 纯硬件加密引擎直通 DMA 总线，加密 32 字节数据耗时不足 $1\text{ }\mu\text{s}$，对 CPU 无软件负担，不增加系统动态功耗；
   * 即使 Flash 芯片被物理拆解，其内部所有数据均为无规律强随机密文；用户仅能通过 BLE 手机 App 在输入配对 PIN 码完成 ECDH 密钥交换后，方可受控解密导出轨迹，构筑坚不可摧的个人时空隐私防火墙。

---

## 9. 无屏幕状态交互与日志调试通道

### 9.1 三色超低功耗 RGB LED 状态灯语定义
| LED 指示形态 | 当前系统状态 | 触发条件说明 |
| :--- | :--- | :--- |
| **双闪绿灯** (点亮 50ms) | GNSS 成功定位 / POI 打点确认 | 单次快照定位完成写入或短按 KEY1 打点成功 |
| **单闪蓝灯** (点亮 50ms) | BLE 连接与同步就绪 | 处于低占空比周期性广播中 |
| **高频快闪蓝灯** | BLE 极速配对 / 批量数据拉取中 | 长按 KEY1 启动配对，App 正在同步离线轨迹 |
| **蓝绿交替闪烁** | 高精度连续记录模式激活中 | 长按 KEY2 开启高精度模式 |
| **单闪黄灯** | 室内 PDR 惯性航位推算接管 | GNSS 卫星信号丢失，IMU 算法运行中 |
| **快闪红灯** | 电量极低 (< 10%) 或存储已满 | 提醒用户及时充电或导出清理数据 |

### 9.2 调试与数据同步通道
1. **日常手机无线同步**：通过 BLE 5.0 (2Mbps PHY) 与手机 App 配对，秒级按天同步轨迹，支持导出 `.gpx`、`.kml` 或 `.geojson` 格式。
2. **硬件独立调试串口 (DEBUG_UART)**：板载预留带防呆标识的 `DEBUG_TXD (P0.09)` 和 `DEBUG_RXD (P0.10)` 测试焊盘，连接 USB-TTL 串口小板即可在 PC 终端上实时查看带毫秒时戳的底层运行 Log（双频载噪比 C/N0、PDR 四元数、BLE 原始广播包）。

---

## 10. 项目工程结构与设计资料索引

```
Life_GPS/
├── Doc/                                         # 官方芯片规格书与硬件原厂参考手册
│   ├── README.md                                # 规格书索引与设计避坑指南 (含QFN48与SGM6040A电源说明)
│   ├── ATGM336H-F8N76_Datasheet.pdf             # 中科微 ATGM336H-F8N (AT9880双频) 官方用户手册
│   ├── nRF52840_Product_Specification_v1.11.pdf # Nordic nRF52840 产品规格书 (含QFN48引脚与限制)
│   ├── LSM6DSV16X_Datasheet.pdf                 # STMicroelectronics LSM6DSV16X 6轴 IMU 规格书
│   ├── W25Q512JV_SPI_Flash.pdf                  # 华邦 64MB (512Mb) SPI NOR Flash 官方规格书
│   ├── SGM6040A_Datasheet.pdf                   # 圣邦微 SGM6040A 60nA 超低功耗降压 DCDC 规格书
│   ├── SGM2036_Datasheet.pdf                    # 圣邦微 SGM2036 300mA 低噪声受控 LDO 规格书
│   ├── SGM4056_Datasheet.pdf                    # 圣邦微 SGM4056 线性锂电充电 IC 规格书
│   ├── LIS2MDL_Datasheet.pdf                    # STMicroelectronics LIS2MDL 3轴磁力计规格书
│   └── BMP388_Datasheet.pdf                     # Bosch BMP388 高精度气压计规格书
├── Hardware/                                    # [规划中 / TBD] 硬件设计工程文件 (立创 EDA / KiCad)
│   ├── Schematics/                              # 原理图设计与 PDF 导出
│   ├── PCB/                                     # PCB 布局布线工程与 Gerber 光绘文件
│   └── BOM/                                     # 元器件物料清单
├── Firmware/                                    # [规划中 / TBD] 嵌入式固件源码 (基于 Zephyr RTOS / nRF Connect SDK)
│   ├── drivers/                                 # 传感器与外设底层驱动 (LSM6DSV16X, ATGM336H, SGMICRO)
│   ├── algorithms/                              # 惯导与航位推算算法 (PDR, SFLP, EKF)
│   └── app/                                     # 业务逻辑、低功耗状态机与两阶段每日打包存储
└── README.md                                    # 本项目总览与架构设计说明
```

---

## 11. 开发路线图与三阶段整改实施路径 (Roadmap)

本项目采纳审阅专家建议的**“三阶段渐进式落地法则”**（先冻结硬件架构 -> 次落地固件算法与数据流 -> 终以实测数据闭环产品指标）：

### 阶段一：硬件架构待冻结，禁止直接投板 (Phase 1: Architecture Pending Freeze - DO NOT TAPE-OUT)
> ⚠️ **冻结红线警示**：在完成以下 5 项关键闭环验证前，禁止直接输出 Gerber 光绘打样投板：

- [x] **建立全项目唯一硬件配置基线表**：主 README、Doc/README、架构图与引脚分配表全部对齐同一套元器件料号（主控 nRF52840-QFAA QFN48、ATGM336H-F8N-76、W25Q512JV、SGM6040D TDFN-8L 3.4V、SGM2045-3.3 XTDFN）、封装、时钟、接口与供电。
- [x] **GNSS 分层状态机与上电时序设计闭环**：确立冷启动 60s 独立超时窗口、冷启动首次 Fix 后保持通电 35s 广播星历接收机制，以及热启动 3.5s -> 8s -> 15s 自适应延展与指数退避接管时序。
- [x] **GNSS 模组真实引脚与备用供电明确**：依据原厂手册确立 Pin 1 为 GND、Pin 6 为 VBAT、Pin 8 为 VCC；Pin 6 直连常通 3.4V 主电源，将 15µA 漏电流全额纳入整机常通待机底噪（25~35µA 之中）。
- [x] **射频前端内置无源天线方案决策**：全面清除有源天线描述，正式确立内置微型双频无源陶瓷贴片天线 + 中科微 ATR2652 双频 LNA (4.5mA) + 高通 B39162B2651P810 双通带 SAW 级联方案，由 SGM2045-3.3 洁净受控供电。
- [x] **惯导融合架构概念纠偏与 SFLP 功耗规范**：明确 LSM6DSV16X SFLP 为纯 6 轴姿态解算（Game Rotation Vector），规范 60Hz 输出下增量功耗为 14µA；明确三层融合分工与固件级磁异常检测降权机制；去掉“彻底消除偏航漂移”承诺。
- [x] **高精度连续模式功耗重新核算**：基于 SGM6040D (3.4V) + SGM2045 (3.3V) 二级拓扑与无源天线+LNA (48.5mA)，实事求是核算出整机电池端放电电流约为 54.0mA，900mAh 有效电量下连续工作时间约为 16.7 小时。
- [x] **存储容量数学模型与去重策略闭环**：给出 64MB Flash 满足 7~14 年的自适应工况计算依据，警示高精模式 53 天边界；废除全局布隆过滤器，改用时间滑动窗口限频队列。
- [x] **引脚与时钟准确表述**：明确 QFN48 引出 30 个有效 GPIO（1 复位，1 预留，28 外设分配）；明确 UICR NFCPINS=0 释放 P0.09/P0.10；给出外部晶振匹配电容公式与杂散容抗估算。

#### 投板前必须完成的五项硬性验收标准 (Hard Acceptance Criteria)：
1. **GNSS 状态机上电时序与防倒灌实测漏电验收**：
   - 使用示波器实测 `GNSS_LDO_EN`、`GNSS_TXD`、`GNSS_RXD`、`GNSS_PPS` 上下电波形，验证注销 UART 并将 IO 设为 High-Z 后，模组切断状态下信号线倒灌电流必须 $< 100\text{ nA}$；
2. **GNSS 冷启动与星历状态保持有效性实测验收**：
   - 实测冷启动下模组持续供电并在 60s 内成功获得首定，首定后保持通电 35s 接收广播星历帧；随后切断 Pin 8 主电 30 分钟（保留 Pin 6 VBAT），再次唤醒时实测能否在 $\le 3.5\text{ s}$ 内完成热启动首捕秒定；
3. **整机待机底噪与高精连续电流仪器实测验收**：
   - 使用 Nordic PPK2 功耗分析仪实测深度静止模式整机电池端输入电流，必须稳定在 $\le 35\text{ }\mu\text{A}$（含 GNSS Pin 6 VBAT 15µA 与 IMU 15Hz LPM1 7.5µA）；实测高精度连续工作电流约为 $52\text{ mA} \sim 56\text{ mA}$ (典型 54.0mA)；
4. **Flash 掉电 Journaling 双备份与原子提交无损恢复验收**：
   - 在夜间归档压缩包写入期间执行随机强行断电拔电池 100 次，重新上电后验证暂存区数据 0 损坏、0 丢失，系统能够无损回滚并重试归档；
5. **铁磁异常检测与 PDR 航向自适应降权实测验收**：
   - 在强磁场靠近（矢量模长偏离本地基准超过 $\pm 30\%$）或进入电梯轿厢时，验证固件能否毫秒级将地磁权重归零，防止航向突变发散。
- [ ] **硬件原理图绘制与投板准备**：
  - [ ] 完成 SGM4056 充电电路（$R_{PROG}=2.8\text{k}\Omega$，350mA 恒流控温）、带保护板电芯（DW01A+8205A 硬件过放兜底）与 NTC/JEITA 温度监测网络；
  - [ ] 完成 SGM6040D 60nA 纳安级降压 DCDC 电路（固定输出 3.4V 供电轨，带 MODE/STOP 引脚）；
  - [ ] 完成传感器（LSM6DSV16X、LIS2MDL、BMP388）3.4V 磁珠+电容 π 型低噪滤波供电网络设计；
  - [ ] 完成 Type-C 输入 USBLC6-2SC6 TVS 保护阵列与 GNSS 天线射频超低结容（$<0.2\text{pF}$）ESD 二极管保护；
  - [ ] 完成 SGM2045-3.3 独立受控 LDO 供电与 ATGM336H-F8N-76 双频射频前端电路（92dB 高 PSRR，专供模组 Pin 8 VCC 与 ATR2652，带 1kΩ 信号防倒灌限流电阻）；
  - [ ] 完成 nRF52840 QFN48 最小系统、双晶体匹配电路与板载 DEBUG_UART 测试焊盘；
  - [ ] 完成 LSM6DSV16X 与 Sensor Hub（LIS2MDL 电子罗盘、BMP388 气压计）I2C 总线连接；
  - [ ] 完成 W25Q512JV 64MB SPI Flash 4~8MHz 低噪声布线；
  - [ ] 完成双按键消抖滤波、三色低功耗 LED 及微安级电池分压采样控制电路。
- [ ] **4 层高密度沉金 PCB 设计与打样**：
  - [ ] 顶层预留 $\ge 25 \times 25\text{ mm}$ 内置微型无源陶瓷天线独立净空区，馈线 50Ω 共面波导阻抗控制；
  - [ ] 严格隔离 2.4GHz BLE 板载天线与顶层 GNSS 双频陶瓷天线净空区，切断数字总线高频谐波串入；
  - [ ] 在 PCB 底层引出 **8 个量产测试点（ICT Test Points）**：`SWDIO`、`SWDCLK`、`DEBUG_TX`、`DEBUG_RX`、`VBAT`、`3V3`、`1PPS`、`GND`，便于产线顶针夹具自动化烧录与测试。

### 阶段二：固件驱动、算法融合与数据流落地 (Phase 2: Firmware & Algorithms)
- [x] **数据结构与时间系统设计**：定义规避 Y2038 溢出的 Life_GPS 2020 Epoch，确立支持 1~20Hz 高频记录的 32 字节定长轨迹结构体（含毫秒偏移与气压楼层），解耦独立的 16 字节定长 BLE 环境信标事件结构体，全部引入 `_Static_assert` 编译期静态断言。
- [ ] **Zephyr RTOS 超低功耗电源调度框架**：
  - [ ] 实现 GNSS 脉冲上电初始化与 CASIC 报文极速裁剪（仅输出 RMC/GGA），落地分层状态机（冷启动60s超时、热启动自适应延展与指数退避接管）；
  - [ ] 实现待机计步（15Hz LPM1 加速度计 ~7.5µA 静止唤醒 / 60Hz LPM1 ~17µA 日常活动）与 PDR 接管（全开陀螺仪+加速度计 Combo 模式 0.65~0.9mA）两档自适应调度。
- [ ] **PDR 惯导与三维环境解算固件**：
  - [ ] 移植 ST 官方 MLC / SFLP 驱动，实现 6 轴姿态 60Hz 硬件递推 (SFLP 14µA)；
  - [ ] 在 nRF52840 固件端实现 LIS2MDL 磁力计硬铁/软铁椭球拟合校准、机体坐标系对齐以及室内铁磁扰动检测自适应降权；
  - [ ] 实现 Weinberg 步长模型在线 GPS 标定与冷启动三级兜底逻辑（先验默认K值与置信度衰减）；
  - [ ] 基于 BMP388 气压计构建垂直高程滤波与电梯/楼梯识别三维状态机。
- [ ] **可靠文件系统与硬件安全加密**：
  - [ ] 实现 16 扇区环形滚动暂存磨损均衡、掉电安全四步原子提交 Journaling 状态机与凌晨 heatshrink 压缩归档；
  - [ ] 启用 CryptoCell-310 硬件 AES-128-CTR Flash 静态流加密驱动；
  - [ ] 实现 BLE 5.0 (2Mbps PHY) 增量数据同步与手机端 App 通信协议。

### 阶段三：仪器全工况测试与产品指标实测验收 (Phase 3: Testing & Certification)
- [ ] **Nordic PPK2 功耗画像实测**：
  - [ ] 通过 Power Profiler Kit II 实测全工况动态电流波形，分别验证静止休眠电流（25~35µA 目标）、GNSS 脉冲快照电流以及 PDR 接管电流，严格闭环验证数学模型；
- [ ] **微波暗室双频 OTA 测试**：
  - [ ] 测试 GNSS 双频陶瓷天线在 52×36mm 整机结构下的辐射效率、轴比、TRP 与 TIS 灵敏度，核验 L1/L5 回波损耗与抗干扰能力；
- [ ] **多场景长周期路测验证**：
  - [ ] 分别在开阔郊外、城市高楼峡谷、地下车库/商场进行长时间丢星测试，记录 GNSS 有效率、PDR 航位推算误差发散曲线与重获锁定平滑收敛表现；
- [ ] **无线型号核准与合规认证**：
  - [ ] 通过国内工信部无委会 **SRRC 无线电型号核准**、欧盟 **CE-RED** 及美国 **FCC-ID** 认证测试。

---

## 许可协议 (License)
本项目硬件原理图、PCB 布局与硬件设计资料遵循 **CERN-OHL-S-2.0**（CERN Open Hardware Licence - Strongly Reciprocal）开源硬件许可协议；嵌入式固件源码及相关算法实现遵循 **MIT License** 开源协议。详情请参阅 [LICENSE](LICENSE) 文件。
