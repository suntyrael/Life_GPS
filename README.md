# Life_GPS: 超低功耗全天候 GPS 与惯导(PDR)轨迹记录仪

[![License: Hardware CERN-OHL-P / Software MIT](https://img.shields.io/badge/License-Hardware%20CERN--OHL--P%20%7C%20Software%20MIT-blue.svg)](LICENSE)
[![MCU: nRF52840 QFN48](https://img.shields.io/badge/MCU-nRF52840%20(QFN48)-red.svg)](Doc/nRF52840_Product_Specification_v1.11.pdf)
[![GNSS: ATGM336H-F8N-76](https://img.shields.io/badge/GNSS-ATGM336H--F8N--76%20(Dual--Band)-green.svg)](Doc/ATGM336H-F8N76_Datasheet.pdf)
[![IMU: LSM6DSV16X](https://img.shields.io/badge/IMU-LSM6DSV16X-orange.svg)](Doc/LSM6DSV16X_Datasheet.pdf)
[![PMIC: SGMICRO](https://img.shields.io/badge/Power-SGMICRO%20(SGM6040A%2BSGM2036%2BSGM4056)-purple.svg)](Doc/README.md)
[![Status: Pre-Schematic Architecture](https://img.shields.io/badge/Phase-Pre--Schematic%20Architecture-yellow.svg)](#11-开发路线图-roadmap)

> **⚠️ 工程阶段提示**：本项目目前处于**系统架构设计与硬件方案论证阶段（Pre-Schematic Architecture Phase）**。本文档中所有续航时间、静态功耗与定位精度数据均为基于官方元器件规格书的**理论数学建模与设计目标（Target Spec）**，最终指标以未来打样工程样机的实测测试报告为准。

**Life_GPS** 是一款专为“一生轨迹（Life Trajectory）”全天候记录、户外越野穿越以及数字游民环境感知打造的**超低功耗微型智能记录仪**。

本项目旨在解决智能手机在全天候开启高精度 GPS 和蓝牙后台扫描时面临的**续航断崖式崩塌（几小时耗尽）、发热严重、系统后台频繁杀进程、隐私数据上传云端**等行业痛点。系统采用创新的**“双域物理隔离（Dual-Domain Isolation）”**低功耗硬件架构，结合高能效无线 SoC、中科微新一代双频多模高精度 GNSS、内置边缘算法的 6 轴惯性测量单元（IMU），以及**全套圣邦微（SGMICRO）纳安级高效电源管理方案（SGM6040A + SGM2036 + SGM4056）**，在配备 **1000mAh ~ 1200mAh** 微型锂电池的物理限制下，实现 **15 至 30 天** 的理论超长待机与持续记录。

---

## 目录
- [1. 核心设计亮点与相比手机的优势](#1-核心设计亮点与相比手机的优势)
- [2. 关键元器件选型与原厂真实性能参数](#2-关键元器件选型与原厂真实性能参数)
  - [2.1 主控 SoC：Nordic nRF52840 (QFN48 封装)](#21-主控-socnordic-nrf52840-qfn48-封装)
  - [2.2 ⚠️ nRF52840 QFN48 封装功能限制与硬件避坑重点](#22-️-nrf52840-qfn48-封装功能限制与硬件避坑重点)
  - [2.3 GNSS 卫星定位模组：中科微 ATGM336H-F8N-76 (双频旗舰款)](#23-gnss-卫星定位模组中科微-atgm336h-f8n-76-双频旗舰款)
  - [2.4 6轴惯性测量单元 (IMU)：STMicroelectronics LSM6DSV16X](#24-6轴惯性测量单元-imustmicroelectronics-lsm6dsv16x)
  - [2.5 全套圣邦微 (SGMICRO) 高效电源管理芯片](#25-全套圣邦微-sgmicro-高效电源管理芯片)
- [3. 系统硬件架构：双域物理隔离 Mermaid 拓扑图与对比矩阵](#3-系统硬件架构双域物理隔离-mermaid-拓扑图与对比矩阵)
- [4. 双按键多功能交互定义 (蓝牙配对 / POI打点 / 高精度模式)](#4-双按键多功能交互定义-蓝牙配对--poi打点--高精度模式)
- [5. 时钟源、调试串口与引脚分配 (QFN48 真实物理引脚)](#5-时钟源调试串口与引脚分配-qfn48-真实物理引脚)
- [6. 超低功耗动态调度策略与三档自洽数学预算](#6-超低功耗动态调度策略与三档自洽数学预算)
- [7. GPS 丢失后的惯导与 PDR 行人航位推算方案](#7-gps-丢失后的惯导与-pdr-行人航位推算方案)
- [8. 蓝牙环境扫描与每日打包日志存储架构 (Daily Chunking)](#8-蓝牙环境扫描与每日打包日志存储架构-daily-chunking)
- [9. 无屏幕状态交互与日志调试通道](#9-无屏幕状态交互与日志调试通道)
- [10. 项目工程结构与设计资料索引](#10-项目工程结构与设计资料索引)
- [11. 开发路线图 (Roadmap)](#11-开发路线图-roadmap)

---

## 1. 核心设计亮点与相比手机的优势

| 评估维度 | 智能手机 (App 后台记录) | Life_GPS 专用记录仪 (设计目标) | 核心技术优势剖析 |
| :--- | :--- | :--- | :--- |
| **全天工作续航** | 开启高精度定位+蓝牙扫描仅支撑 **4 ~ 8 小时** | 1000mAh 电池目标续航 **15 ~ 30 天**（一次充电支持整月记录） | 专用超低功耗架构，无屏幕、无安卓/iOS高负荷操作系统底噪 |
| **电源转换能效** | 手机内部多路电源转换复杂，发热严重 | **圣邦微 SGM6040A 纳安级 DCDC**（60nA静态电流，750mA输出能力） | 告别传统线性 LDO 在 4.2V 下浪费 22% 压差发热的弊端 |
| **系统稳定性** | 操作系统频繁杀后台、省电策略干预导致断轨、丢点 | 专用 RTOS 固件驱动，硬件看门狗保障，**全天候永不断轨** | 硬件级连续守护，状态机自闭环 |
| **遮挡与丢星处理**| 室内、地下商场直接失去定位或发生数百米跳变漂移 | **LSM6DSV16X 硬件融合 + PDR 航位推算**（刚性佩戴下接管） | 基于步长自适应模型与四元数航向，室内航位推算误差呈步数线性累加 |
| **环境感知能力** | 手机系统出于权限与隐私严格封锁底层 BLE 原始广播 | 全频段微占空比扫描，抓取周边固定 Beacon 基站短 ID 与 RSSI | 形成包含“地理位置 + 空间射频环境”的多维离线环境时空轨迹 |
| **设备体积与便携性**| 笨重、发热严重、需随身携带充电宝 | 仅钥匙扣/卡片尺寸（约 52×36×9mm），整机重量 < 35g，随身挂载 | 适合腰带夹固定、背包外侧挂载或放置于车钥匙扣上 |
| **数据安全性与隐私**| 轨迹频繁实时上传云端，面临第三方追踪与商业分析 | **纯离线存储于板载 NOR Flash**，仅由用户物理导出或自主同步 | 真正的数据主权归属个人，物理隔离离线“一生轨迹”底库 |

---

## 2. 关键元器件选型与原厂真实性能参数

### 2.1 主控 SoC：Nordic nRF52840 (QFN48 封装)
* **核心架构**：ARM Cortex-M4F @ 64MHz，集成硬件浮点运算单元（FPU）与专用 DSP 指令集。
* **存储容量**：**1MB 片上 Flash + 256KB RAM**。彻底规避低端芯片（如 nRF52810）RAM 匮乏且官方协议栈阉割“蓝牙扫描（Observer）”的致命缺陷，有充裕内存容纳蓝牙扫描哈希表、RTOS 任务栈及惯导融合滤波。
* **封装优势（QFN48）**：相较于 73 引脚 aQFN 封装，QFN48 拥有 0.4mm 间距的标准四边扁平引脚，PCB 扇出（Fan-out）极为简单，4 层乃至 2 层板即可轻松布通，极大降低打样制板与 SMT 贴片门槛。
* **无线特性**：支持 Bluetooth 5.4、Coded PHY（Long Range 长距离模式）、2Mbps 高速传输。

---

### 2.2 ⚠️ nRF52840 QFN48 封装功能限制与硬件避坑重点
根据 Nordic 官方《nRF52840 Product Specification》（文档第 7.1.2 节及引脚分配表），**QFN48 封装相比标准 aQFN73 封装在硬件功能上有严格的物理级精简**，在原理图和硬件设计中必须重点避坑：

1. **缺失原生 USB 硬件引脚（无 VBUS、D+、D-）**：
   * **原厂物理事实**：QFN48 封装的 48 个引脚中**根本没有引出 VBUS、USB D+ 和 USB D- 管脚**（仅 aQFN73 具备）。
   * **设计对策**：板载 Type-C 接口专用于 5V 锂电池充电输入；日常数据导出主要采用 **BLE 5.0 (2Mbps PHY) 高速无线同步**；同时系统在空闲 GPIO 上**引出了专用的独立硬件调试串口（DEBUG_UART: TXD/RXD）测试焊盘**，用于连接外部 USB-TTL 串口板输出固件实时 Log。
2. **高压供电模式（High Voltage Supply Mode，REG0）内部短接失效**：
   * **原厂规格书明文**：*“Note: VDD and VDDH are shortcircuited inside the package. Therefore the device is only usable in Normal Voltage supply mode, and not High Voltage supply mode.”*
   * **设计对策**：**严禁将单节锂电池（3.7V~4.2V）或 USB 5V 直接接给 nRF52840 芯片电源管脚**！系统必须由圣邦微 SGM6040A 降压 DCDC 将电池电压高效转换为 3.3V 后输入 VDD，工作在 Normal Voltage Supply Mode（1.7V~3.6V）。
3. **真实可用 GPIO 数量精简至 28 个**：
   * aQFN73 引出 48 个 GPIO，而 QFN48 **仅引出 28 个有效 GPIO**：
     * P0 端口保留 24 个管脚（**缺失 P0.06、P0.15、P0.16、P0.25、P0.26、P0.27**）；
     * P1 端口大部分被裁剪，**仅引出 P1.00、P1.08、P1.09、P1.15 四个管脚**（其余全部缺失）。
   * **设计对策**：本工程已严格基于 QFN48 的真实 28 个物理引脚编排所有外设总线，杜绝任何不存在引脚的连线错误。
4. **外部时钟晶振引脚规划**：
   * 必须在专用管脚 `XC1 (Pin 35)` 和 `XC2 (Pin 36)` 外挂 **32MHz 高频石英晶体（HFXO）**，这是 2.4GHz BLE 射频与 64MHz 主频运行的必需时基；
   * 必须在 `P0.00/XL1 (Pin 2)` 和 `P0.01/XL2 (Pin 3)` 外挂 **32.768kHz 低频石英晶体（LFXO）**，为睡眠状态下的 RTC 提供微安级超低功耗时基。

---

### 2.3 GNSS 卫星定位模组：中科微 ATGM336H-F8N-76 (双频旗舰款)
> **权威规格依据**：依据中科微原厂官方《ATGM336H-F8N 模块用户手册》，本型号为中科微新一代基于 **AT9880 旗舰 SoC 架构** 的**全系统多频点（双频 L1+L5）高性能 GNSS 定位导航与高精度授时模组**：

| 关键参数项目 | 官方规格书真实数据 | 详细特性与工程说明 |
| :--- | :--- | :--- |
| **具体订货型号** | **ATGM336H-F8N-76** | 全模多频版本（区别于单北斗专用的 AT9880B 系列），集成片上 Flash |
| **核心 SoC 芯片** | **中科微新一代 AT9880** | 多频多系统 GNSS SoC 单芯片，集成高性能嵌入式 CPU |
| **支持卫星频点** | **L1 + L2/L5 多频点** | **BDS (B1I+B1C+B2a/B2b), GPS (L1+L2/L5), GALILEO (E1+E5a/E5b), GLONASS (L1), IRNSS (L5), QZSS, SBAS** |
| **物理跟踪通道** | **176 通道** | 可同时跟踪多星座数十颗有效卫星，具备极强电离层延迟消除与多径抑制能力 |
| **跟踪灵敏度** | **-167 dBm** | 处于行业顶级水准（冷启动捕获 -148dBm，热启动捕获 -156dBm，重捕获 -160dBm） |
| **定位 / 测速精度** | **< 1.0 m (CEP50)** / **< 0.1 m/s (1σ)** | 优于传统单频模块 2.5m 精度，城市峡谷与林荫道定位更平滑 |
| **启动时间 (TTFF)** | 冷启动 $\le 30\text{ s}$，**热启动 $\le 1\text{ s}$**，重捕获 $\le 1\text{ s}$ | 具备后备电源 VBAT 时，重新通电瞬间实现秒定 |
| **最大定位更新率** | **最高 20 Hz** (默认 1Hz) | 满足户外冲刺、运动竞技及高精度赛段轨迹记录需求 |
| **串口通信特性** | **默认 115200 bps**，8-N-1 | 相比老款 9600bps 提速 12 倍，大幅缩短数据吐出时间，帮助 MCU 提早休眠 |
| **运行功耗参数** | **典型电流 44 mA @ 3.3V** (L1+L5双频满载) | 待机功耗 **< 20 µA**，RTC后备电源 VBAT 供电电流仅 **15 µA** (每日约 0.36 mAh) |
| **独立供电架构** | **圣邦微 SGM2036-3.3 专用 LDO 供电** | **带 EN 使能脚硬切断（关断漏电仅 10nA）**，低噪声高 PSRR 隔绝电源杂波 |
| **射频前端天线方案** | 采用微型**双频有源陶瓷贴片天线** | 模块内部已集成 LNA 与滤波器，直接由模组 Pin 14 `VCC_RF` 输出受控供电，随模组上下电，**废除单频专用的 MAX2659** |
| **防倒灌硬件设计** | 信号线串联 1kΩ 隔离电阻 | `TXD`、`RXD`、`PPS` 线上串接 1kΩ 电阻，下电时 MCU IO 置浮空 (High-Z)，杜绝通过 ESD 二极管倒灌偷电 |

---

### 2.4 6轴惯性测量单元 (IMU)：STMicroelectronics LSM6DSV16X
* **传感器规格**：3 轴数字加速度计（±2g 至 ±16g） + 3 轴数字陀螺仪（±125 至 ±4000 dps）。
* **功耗分级管理（实事求是）**：
  * **日常计步状态**：仅开启低功耗加速度计，电流仅 **$\approx 30\text{ }\mu\text{A}$**；
  * **PDR 惯导接管状态**：全开加速度计 + 陀螺仪 + SFLP 姿态融合，低功耗 Combo 模式下电流为 **$280\text{ }\mu\text{A} \sim 400\text{ }\mu\text{A}$**（SFLP 协处理器单元本身增量仅 15µA）；
* **嵌入式边缘智能三核架构**：
  1. **MLC (Machine Learning Core)**：硬件级机器学习决策树，实时识别静止、步行、慢跑、乘车（Vehicle）等活动模式；
  2. **FSM (Finite State Machine)**：8 个独立可编程有限状态机，负责瞬态动作提取与手势监测；
  3. **SFLP (Sensor Fusion Low Power)**：嵌入式超低功耗传感器融合算法引擎，硬件输出 6 轴姿态四元数（Game Rotation Vector）。
* **扩展能力 (Sensor Hub)**：具备独立 I2C Master，可直接外挂三轴磁力计（**LIS2MDL**）与高精度气压计（**BMP388**），数据统一打包进内部 FIFO，免去频繁唤醒主控 CPU。
* **触摸感知**：具备 **Qvar** 静电电荷变化检测通道，支持设备外壳轻击、滑动手势及人体穿戴检测。

---

### 2.5 全套圣邦微 (SGMICRO) 高效电源管理芯片
为兼顾极致续航、高电源抑制比（PSRR）与完全自主可控供应链，本项目电源部分全部采用**圣邦微电子（SGMICRO）**成熟物料：

1. **主控系统降压 DCDC：圣邦微 SGM6040A**
   * **纳安级超低静态电流**：静态待机电流低至 **60 nA**（0.06 µA），关断电流典型仅 **25 nA**；
   * **微载荷极限效率**：在 $1\text{ }\mu\text{A}$ 微载荷输出下效率依然高达 **80%**，重载峰值效率超过 **90%**；
   * **输出能力与特性**：输入 1.8V ~ 5.5V，最大输出电流 **750 mA**；通过 VSET 引脚单颗精密电阻配置为 **固定 3.3V 输出**；具备 100% 占空比旁路模式（Bypass 模式下 $I_Q$ 仅 120nA）；
   * **STOP 硬件低噪引脚**：具备 STOP 硬件控制脚，在 MCU 执行高灵敏度 ADC 采样时可瞬态冻结开关动作消除开关噪声。
2. **GNSS 独立低噪声 LDO：圣邦微 SGM2036-3.3**
   * **专路专供**：ATGM336H-F8N-76 射频定位模组工作电流大（44mA）且对电源纹波极度敏感。SGM2036 提供高达 300mA 洁净低纹波电源，彻底杜绝 DCDC 开关纹波串入卫星天线前端；
   * **受控物理切断**：具备逻辑使能脚（EN），由 nRF52840 GPIO `P0.04` 控制。休眠时拉低 EN，**关断漏电流仅 10 nA**，实现零漏电硬隔离。
3. **锂电池充电管理：圣邦微 SGM4056**
   * **保护与充电**：1A 单节锂离子/锂聚合物线性充电器，恒流/恒压充电曲线，集成高达 26.5V 耐压与输入过压保护（OVP 6.8V），防电池倒灌漏电 $< 1\text{ }\mu\text{A}$，具备 CHRG 充电状态指示；
   * **充电电流设定**：通过 PROG 引脚电阻设定为 **300mA ~ 400mA**（约 0.3C~0.4C），保护 1000mAh 电芯并防止小壳体内发热。
4. **锂电芯保护回路 (PCM)**：
   * 明确选用**自带保护板（集成功耗保护 IC 如 AP9101C + 双 MOS）的聚合物软包锂电池**，提供过充 4.28V、过放 2.8V 及短路截止保护。
5. **外部存储芯片：Winbond W25Q128JV**
   * 16MB SPI NOR Flash，**运行频率降为 4MHz ~ 8MHz**（杜绝 32MHz 高次谐波干扰 GNSS L1/L5 频段），深休眠电流仅 $1\text{ }\mu\text{A}$。

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
        
        %% 分出两路 3.3V 统一电源拓扑
        BAT -->|"电池原始供电 (3.0V~4.2V)"| SGM6040A["圣邦微 SGM6040A 纳安级 DCDC<br/>(静态功耗 60nA | 750mA | 效率 > 90%)"]:::pwr
        BAT -->|"电池原始供电 (3.0V~4.2V)"| SGM2036["圣邦微 SGM2036-3.3 专用 LDO<br/>(300mA | 高 PSRR 低噪声 | 关断漏电 10nA)"]:::sw
    end

    %% 双域统一 3.3V 供电
    SGM6040A -->|"常通主电源: VDD_3V3 (统一逻辑电平)"| AON_DOMAIN
    SGM2036 -->|"受控低噪声主电: GNSS_3V3 (3.3V)"| GNSS_SUBSYSTEM

    subgraph AON_DOMAIN ["🟢 【常通超低功耗微安域 (Always-On Domain)】静态待机电流 < 15 µA"]
        subgraph MCU_NODE ["主控核心: Nordic nRF52840 (QFN48)"]
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
            FLASH["W25Q128JV (16MB NOR Flash)<br/>低噪声 SPI (4MHz~8MHz) | 休眠 1µA"]:::dev
            IMU["LSM6DSV16X (ST 6轴旗舰 IMU)<br/>日常计步仅开低功耗Acc (30µA)<br/>丢星接管开 SFLP 姿态融合 (280µA)"]:::aon
            
            subgraph SENSOR_HUB ["IMU Sensor Hub (独立 I2C Master)"]
                MAG["LIS2MDL 三轴磁力计<br/>(地磁绝对航向参考)"]:::dev
                BARO["BMP388 高精度气压计<br/>(±0.5m 相对高度/楼层)"]:::dev
            end
        end
    end

    subgraph SW_DOMAIN ["🔵 【受控突发高能毫安域 (Switched Domain)】瞬态工作电流 ~48 mA"]
        subgraph GNSS_SUBSYSTEM ["GNSS 卫星定位系统 (受控独立供电)"]
            GNSS["中科微 ATGM336H-F8N-76<br/>新一代 AT9880 旗舰 SoC<br/>176通道 | 双频 L1+L5 | -167dBm<br/>典型工作功耗: 44mA @ 3.3V"]:::sw
            ACTIVE_ANT["双频微型有源陶瓷贴片天线<br/>(支持 L1+L5 / 50Ω 阻抗共面波导)"]:::dev
            GNSS -->|"Pin 14 VCC_RF (3.3V 受控输出)"| ACTIVE_ANT
            ACTIVE_ANT -->|"RF_IN (Pin 11)"| GNSS
        end
    end

    %% 跨域控制、防倒灌电阻与数据连线
    MCU_CORE -->|"GNSS_LDO_EN 使能控制 (P0.04)"| SGM2036
    GNSS -->|"串联 1kΩ 限流保护"| R_RX["1kΩ 保护电阻"]:::dev -->|"GNSS_RXD (P0.05)"| MCU_CORE
    MCU_CORE -->|"GNSS_TXD (P0.07)"| R_TX["1kΩ 保护电阻"]:::dev -->|"下电前置 High-Z"| GNSS
    GNSS -->|"1PPS (P0.08)"| R_PPS["1kΩ 保护电阻"]:::dev --> MCU_CORE
    SGM6040A -->|"VBAT 常通保持 15µA (星历热启动)"| GNSS

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
| **所属核心硬件** | **nRF52840 SoC、LSM6DSV16X IMU、W25Q128JV Flash** | **ATGM336H-F8N-76 GNSS 模组、双频有源陶瓷天线** |
| **独立供电通道** | **圣邦微 SGM6040A 纳安级 DCDC** (静态电流 60nA, 750mA 输出) | **圣邦微 SGM2036-3.3 专用受控 LDO** (300mA, 高 PSRR, 低噪声) |
| **工作电压基准** | **全板统一 3.3V** (输入跌破 3.3V 时自动 100% Bypass 直通) | **严格受控 3.3V** (消除数字信号线电平倒灌与超限风险) |
| **供电控制机制** | 全天候常通供电，维持系统时基与状态监听 | **通过 MCU GPIO `P0.04` 控制 SGM2036 的 EN 脚**实现硬件级通断 |
| **静态休眠电流** | **$< 15\text{ }\mu\text{A}$** (System ON + RTC + SFLP 融合 + Flash深休眠) | **$< 0.01\text{ }\mu\text{A}$** (SGM2036 关断漏电仅 10nA，彻底消除静电漏电) |
| **动态运行电流** | $0.65\text{ mA} \sim 8\text{ mA}$ (BLE 广播/扫描/姿态运算) | **$44\text{ mA} \sim 48\text{ mA}$** (双频 176 通道满载搜星 + 有源天线) |
| **运行占空比** | 100% 全天候在线守护 | 脉冲式快照工作（常规运动下仅占空约 **4% ~ 10%**） |
| **后备保活设计** | 主控保持高精度时间戳与事件哈希表 | 模组 `VBAT` 引脚接常通 3.3V (15µA)，保证 RTC/SRAM 热启动秒定 |

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

### 5.1 外部高低频时钟源规划
* **32MHz 外部高频晶振 (HFXO)**：接至专用管脚 `XC1 (Pin 35)` 和 `XC2 (Pin 36)`，匹配 12pF 负载电容，频差 $\le \pm 10\text{ ppm}$，为 2.4GHz BLE 射频与 64MHz CPU 提供精准时基；
* **32.768kHz 外部低频晶振 (LFXO)**：接至专用管脚 `P0.00/XL1 (Pin 2)` 和 `P0.01/XL2 (Pin 3)`，匹配 9pF/12.5pF 负载电容，为睡眠 RTC 计时提供微安级时基。

### 5.2 独立预留硬件调试串口 (DEBUG_UART)
为满足开发过程中的日志捕获与固件调试，系统分配了独立于 GNSS 的硬件调试串口：
* **`DEBUG_TXD`**：分配至 `P0.09` (Pin 29)，引出板载测试焊盘 (TP)，波特率 115200bps，输出实时系统 Log；
* **`DEBUG_RXD`**：分配至 `P0.10` (Pin 30)，引出板载测试焊盘 (TP)，用于接收配置调试指令；
* *注：P0.09/P0.10 默认具 NFC 保护电容，在固件 UICR 配置中必须配置 `NFCPINS = 0`，以释放为标准数字 GPIO。*

### 5.3 nRF52840 QFN48 完整物理引脚分配映射表
系统完全基于 QFN48 真实的 28 个有效物理 GPIO 规划，无任何不存在管脚：

| 物理管脚编号 | 端口网络标号 | I/O方向 | 硬件连接与电气功能描述 |
| :--- | :--- | :---: | :--- |
| **Pin 35 / Pin 36**| `XC1 / XC2` | IN/OUT | **32MHz 外部高频石英晶体 (HFXO)**，BLE 射频与 CPU 必需基准 |
| **Pin 2 / P0.00** | `RTC_XTAL_IN (XL1)` | IN | **32.768 kHz 外部低频晶体 (LFXO)** 输入端，微安待机时基 |
| **Pin 3 / P0.01** | `RTC_XTAL_OUT (XL2)`| OUT | **32.768 kHz 外部低频晶体 (LFXO)** 输出端 |
| **Pin 40 / P0.02** | `BAT_ADC_IN (AIN0)` | IN | 电池电压分压采集模拟输入（Standard Drive，低频） |
| **Pin 39 / P0.03** | `BAT_ADC_EN (AIN1)` | OUT | 电池分压回路使能控制（平时高电平截止，采样时拉低导通 P-MOS） |
| **Pin 4 / P0.04** | `GNSS_LDO_EN` | OUT | **控制圣邦微 SGM2036 LDO 的 EN 脚**（高电平通电，低电平硬切断） |
| **Pin 5 / P0.05** | `GNSS_RXD` | IN | UART 接收引脚（串联 1kΩ 电阻连 ATGM336H TXD0，波特率 115200） |
| **Pin 6 / P0.07** | `GNSS_TXD` | OUT | UART 发送引脚（串联 1kΩ 电阻连 ATGM336H RXD0，替代不存在的 P0.06） |
| **Pin 7 / P0.08** | `GNSS_PPS` | IN | 接收 ATGM336H 1PPS 授时脉冲（串联 1kΩ 电阻，纳秒级时间对齐） |
| **Pin 29 / P0.09** | `DEBUG_TXD` | OUT | **独立调试串口 TX**（115200bps，引出测试焊盘，UICR配置NFCPINS=0） |
| **Pin 30 / P0.10** | `DEBUG_RXD` | IN | **独立调试串口 RX**（引出测试焊盘，UICR配置NFCPINS=0） |
| **Pin 10 / P0.11** | `IMU_CS` | OUT | LSM6DSV16X SPI 片选引脚（Active Low） |
| **Pin 11 / P0.12** | `IMU_SCK` | OUT | LSM6DSV16X SPI 时钟信号（最高支持 8MHz） |
| **Pin 13 / P0.13** | `IMU_MISO` | IN | LSM6DSV16X SPI 主机输入引脚 |
| **Pin 14 / P0.14** | `IMU_MOSI` | OUT | LSM6DSV16X SPI 主机输出引脚 |
| **Pin 15 / P0.17** | `IMU_INT1` | IN | LSM6DSV16X 中断 1（静止休眠唤醒、计步事件中断） |
| **Pin 8 / P1.08** | `IMU_INT2` | IN | LSM6DSV16X 中断 2（FIFO 阈值满中断、MLC 运动模式识别） |
| **Pin 9 / P1.09** | `FLASH_CS` | OUT | W25Q128JV NOR Flash 片选引脚（Active Low） |
| **Pin 18 / P0.19** | `FLASH_SCK` | OUT | Flash 高速时钟信号（具备 QSPI/SCK 硬件驱动能力，配置为 4~8MHz） |
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

---

## 6. 超低功耗动态调度策略与三档自洽数学预算

### 6.1 记录模式特性对比 (Mode Contrast Matrix)

| 模式对比维度 | 模式 A：超低功耗自适应模式 (默认) | 模式 B：高精度连续记录模式 (按键2切换) |
| :--- | :--- | :--- |
| **主要定位方式** | 微占空比周期性定位 (Duty-Cycled GNSS) | 连续高频双频卫星跟踪 + 高频惯导融合 |
| **GNSS 供电与占空比** | **4% ~ 10%** (SGM2036 脉冲式通电 2~4 秒锁定即切断) | **100% 常开运行** (SGM2036 持续输出洁净电源) |
| **定位更新频率** | 动态自适应心跳 (静止关闭，行走 45~60s 一次) | **1 Hz ~ 5 Hz 连续输出** (最高支持 20Hz) |
| **电源转换能效** | SGM6040A 静态功耗仅 60nA，轻载效率 80%，重载 >90% | DCDC 持续高效降压转换，杜绝线性压差发热 |
| **适用场景** | 日常生活记录、城市通勤、多日户外徒步 | 越野赛道跑、山地骑行攻顶、复杂弯道精准打点 |
| **电池端平均放电电流**| **$\approx 0.84\text{ mA} \sim 1.99\text{ mA}$** (典型值约 1.99mA) | **$\approx 49\text{ mA} \sim 51\text{ mA}$** (GNSS 44mA + MCU/IMU/损耗) |
| **1000mAh 电池续航** | **理论设计目标：18 ~ 40 天** (典型工况约 19 天) | **理论设计目标：$\approx 17.6\text{ 小时}$** (满足专业赛事单日全开) |

---

### 6.2 默认自适应模式的三档情景自洽功耗数学模型
*以 1000mAh 聚合物电池（扣除化学老化与低温转换衰减，按 **900 mAh 真实有效可用电量** 计算；每日设定为 14 小时静止睡眠待机 + 10 小时户外/通勤运动）：*

| 功耗组成单元 | 🟢 乐观情景 (空旷郊外/星历新鲜) | 🟡 典型情景 (城市开阔/步行通勤) | 🔴 悲观情景 (高楼峡谷/频繁丢星) |
| :--- | :--- | :--- | :--- |
| **单次锁星出 Fix 耗时** | **2.5 秒** (极速热启动) | **4.5 秒** (典型城市路段) | **8.0 秒** (弱信号搜星) |
| **采样周期** | 每 60 秒快照一次 | 每 45 秒快照一次 | 每 45 秒快照一次 (含超时探查) |
| **GNSS 工作占空比** | **4.1%** | **10.0%** | **17.8%** |
| **1. GNSS 脉冲定位能耗** | $44\text{mA} \times 4.1\% \times 10\text{h} = \mathbf{18.04\text{ mAh}}$ | $44\text{mA} \times 10.0\% \times 10\text{h} = \mathbf{44.00\text{ mAh}}$ | $44\text{mA} \times 17.8\% \times 10\text{h} = \mathbf{78.32\text{ mAh}}$ |
| **2. GNSS VBAT 常通保持** | $15\mu\text{A} \times 24\text{h} = \mathbf{0.36\text{ mAh}}$ | $15\mu\text{A} \times 24\text{h} = \mathbf{0.36\text{ mAh}}$ | $15\mu\text{A} \times 24\text{h} = \mathbf{0.36\text{ mAh}}$ |
| **3. 静止待机 (14h, DCDC)** | $12\mu\text{A} \times 14\text{h} = \mathbf{0.17\text{ mAh}}$ | $12\mu\text{A} \times 14\text{h} = \mathbf{0.17\text{ mAh}}$ | $12\mu\text{A} \times 14\text{h} = \mathbf{0.17\text{ mAh}}$ |
| **4. IMU 计步与 PDR 解算** | 纯加速度计计步: $\mathbf{0.30\text{ mAh}}$ | 偶尔 PDR 介入: $\mathbf{1.50\text{ mAh}}$ | 频繁室内 PDR 接管: $\mathbf{4.00\text{ mAh}}$ |
| **5. BLE 扫描与存储写入** | 极低占空比: $\mathbf{1.20\text{ mAh}}$ | 典型占空比: $\mathbf{1.80\text{ mAh}}$ | 高密度写入: $\mathbf{2.50\text{ mAh}}$ |
| **每日总耗电量估算** | **$\mathbf{20.07\text{ mAh / 天}}$** | **$\mathbf{47.83\text{ mAh / 天}}$** | **$\mathbf{85.35\text{ mAh / 天}}$** |
| **日均放电电流** | $\approx 0.84\text{ mA}$ | $\approx 1.99\text{ mA}$ | $\approx 3.56\text{ mA}$ |
| **预估理论续航 (1000mAh)** | **$\mathbf{44.8\text{ 天}}$** | **$\mathbf{18.8\text{ 天}}$** | **$\mathbf{10.5\text{ 天}}$** |

*注：若使用 1200mAh 电池（有效电量 1080mAh），典型工况下理论续航为 **22.5 天**，满足 3 周以上连续记录目标。*

---

## 7. GPS 丢失后的惯导与 PDR 行人航位推算方案

### 7.1 室内与遮挡丢星的技术救赎与物理边界
在进入大型地下车库、隧道、密集高楼群或室内商场时，卫星信号严重衰减。若盲目保持 GNSS 模组通电搜索，不仅无法定位，反而会导致 44mA 的大电流快速耗尽电池。

系统状态机会在连续 15 秒无法定位时**拉低 `P0.04 (GNSS_LDO_EN)` 果断切断 SGM2036 LDO 电源**，无缝切换为基于 **LSM6DSV16X 的 PDR 行人航位推算（Pedestrian Dead Reckoning）**算法接管：

```mermaid
flowchart TD
    classDef raw fill:#1e293b,stroke:#475569,stroke-width:1.5px,color:#f8fafc;
    classDef proc fill:#064e3b,stroke:#059669,stroke-width:1.5px,color:#ecfdf5;
    classDef out fill:#4c1d95,stroke:#7c3aed,stroke-width:2px,color:#f5f3ff;

    RAW["LSM6DSV16X 原始惯导数据<br/>(加速度计 100Hz + 陀螺仪 100Hz)"]:::raw --> SPLIT{"算法分支解耦"}

    SPLIT -->|"加速度通道"| STEP["硬件计步与垂直合加速度过零点检测<br/>(提取单步周期与最大/最小加速度)"]:::proc
    STEP --> WEINBERG["Weinberg 非线性动态步长模型<br/>$L_k = K \cdot \sqrt[4]{a_{\max} - a_{\min}}$<br/>(室外 GPS 在线自动标定增益 K)"]:::proc

    SPLIT -->|"角速度与磁场通道"| SFLP["SFLP 嵌入式姿态融合引擎 (15µA)<br/>硬件输出四元数 Game Rotation Vector"]:::proc
    SFLP --> MAG_FUSE["结合外挂 LIS2MDL 地磁数据互补滤波<br/>绝对北向对齐，消除偏航零偏漂移"]:::proc
    MAG_FUSE --> HEADING["解算当前绝对航向角 $\theta_k$"]:::proc

    WEINBERG & HEADING --> CALC["平面航位推算坐标迭代更新<br/>$X_k = X_{k-1} + L_k \cdot \sin\theta_k$<br/>$Y_k = Y_{k-1} + L_k \cdot \cos\theta_k$"]:::out
    CALC --> FUSE["走出室外捕获卫星时进行 EKF 卡尔曼平滑闭环<br/>输出连续平滑室内外轨迹"]:::out
```

### 7.2 客观限定 PDR 佩戴方式与特殊场景自适应
* **佩戴方式物理约束**：
  * **高置信度模式（刚性固定）**：PDR 航位推算仅在设备**紧密固定于人体（如腰带夹固定、上衣胸包背带固定）**时具备高精度航向收敛能力；
  * **自由摆动降级模式**：若作为钥匙扣在手头甩动或在背包内晃动，由于姿态矩阵和人体行进方向解耦，算法自动降级为**“硬件步数累加 + 粗略距离推算”**，不在地图上生成发散的假轨迹线。
* **乘车与交通工具识别（MLC 介入）**：
  * LSM6DSV16X 内部 MLC 机器学习核识别出“乘车（Vehicle）”状态时，**立即关闭计步与 PDR**，避免车辆颠簸误触发计步；同时 GNSS 切换为每隔 3 分钟探查一次出站/出隧道信号。
* **指数退避探查机制（Backoff Strategy）**：
  * 丢星切断 LDO 后，系统按第 1 分钟、第 3 分钟、第 10 分钟逐步延长探查间隔；一旦 IMU 检测到人体进入静止睡眠状态（如落座办公），彻底停止探查，进入 15µA 深度休眠。

---

## 8. 蓝牙环境扫描与每日打包日志存储架构 (Daily Chunking)

### 8.1 蓝牙环境感知与隐私合规
* **扫描重点**：微占空比 Passive 扫描主要针对**固定商用 iBeacon / Eddystone 信标及固定基站**，记录其短 ID 与 RSSI，用于在室内/高楼提供空间射频指纹辅助参考；
* **隐私过滤**：主动过滤普通手机的 RPA 随机轮换地址，不持久化不可识别的私人设备 MAC，规避隐私与法规合规风险。

### 8.2 严格 32 字节紧凑对齐数据结构 (Binary Storage Layout)
板载 16MB NOR Flash 划分为循环逻辑扇区，每条记录严格固定为 **32 字节**（无跨页跨扇区碎片，速度扩展为独立 16 位）：

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t seq_id;         // [0..3]   全局自增序列号 (用于掉电断点二分查找与对齐)
    uint32_t timestamp;      // [4..7]   UNIX 时间戳 (UTC, 秒级)
    int32_t  latitude;       // [8..11]  纬度 (度 * 1e7)
    int32_t  longitude;      // [12..15] 经度 (度 * 1e7)
    int16_t  altitude;       // [16..17] 海拔高度 (米, -500 ~ 9000)
    uint16_t speed;          // [18..19] 地速 (0.1 km/h, 范围 0 ~ 6553.5 km/h，绝不溢出)
    uint16_t heading;        // [20..21] 航向角 (0 ~ 359.9 度, 分辨率 0.1 度)
    uint16_t step_count;     // [22..23] 累计步数增量 (PDR 记录)
    uint8_t  hdop;           // [24]     水平精度因子 (0.1, 范围 0 ~ 25.5)
    uint8_t  satellites;     // [25]     有效卫星颗数 (0 ~ 255)
    uint8_t  status_flags;   // [26]     状态位 (0x01=GNSS有效, 0x02=PDR接管, 0x04=POI打点, 0x08=高精模式)
    uint8_t  battery_level;  // [27]     剩余电量百分比 (0 ~ 100%)
    uint16_t ble_beacon_id;  // [28..29] 环境最强信标短ID (CRC16/Hash，支持基站指纹)
    int8_t   ble_rssi;       // [30]     蓝牙信号强度 (-128 ~ 0 dBm)
    uint8_t  checksum;       // [31]     前 31 字节 CRC8 校验和
} TrajectoryRecord_t;        // 严格等于 32 字节 (4+4+4+4+2+2+2+2+1+1+1+1+2+1+1 = 32)
#pragma pack(pop)
```

---

### 8.3 每日逻辑块打包存储与轻量索引架构 (Daily Chunking Architecture)
针对 16MB NOR Flash 的物理特性（扇区 4KB 擦除开销大、擦除耗时 45ms~400ms），系统放弃高风险的复杂压缩，采用**“每日独立逻辑分区（Daily Chunking）”**架构：

```mermaid
flowchart TD
    classDef hdr fill:#0f172a,stroke:#38bdf8,stroke-width:1.5px,color:#f0f9ff;
    classDef day fill:#064e3b,stroke:#059669,stroke-width:1.5px,color:#ecfdf5;
    classDef rec fill:#1e293b,stroke:#475569,stroke-width:1px,color:#cbd5e1;

    DIR["Flash 根目录索引区 (Sector 0: 4KB)<br/>[Day 0 Index] [Day 1 Index] ... [Day N Index]"]:::hdr --> D1 & D2 & D3

    subgraph D1 ["2026-10-10 数据块 (按 4KB 扇区对齐)"]
        H1["Day Header (日期, 基础基准点坐标, 记录总数, CRC32)"]:::day
        H1 --> R1["Record 0 (32B)"]:::rec
        R1 --> R2["Record 1 (32B)"]:::rec
        R2 --> RN["Record ... (顺序追加写入)"]:::rec
    end

    subgraph D2 ["2026-10-11 数据块 (只读封存态)"]
        H2["Day Header (封存只读态, 完整性已闭环)"]:::day
    end

    subgraph D3 ["2026-10-12 数据块 (当前活动天追加中)"]
        H3["Day Header (活动态, 掉电仅丢失末尾RAM缓冲)"]:::day
    end
```

#### 每日打包的核心工程优势：
1. **天然掉电安全与数据隔离**：
   * 每天 00:00:00 UTC（或每次开机新日期）建立独立的 `Day Header`；
   * 历史天数全部处于“封存只读（Sealed）”状态，即使当日由于电池耗尽异常断电，**破坏范围仅局限在当天未刷盘的单条缓冲，历史天数数据绝对完整**。
2. **手机 App 极速增量秒传**：
   * 手机连接后仅需读取 4KB 目录索引区，即可知道设备内包含哪些历史日期；
   * 仅按天拉取缺失日期的完整块（以天为单位传输），支持断点续传，用户体验极佳。
3. **零碎片 FIFO 滚动覆盖**：
   * 当 16MB 存满（约 450,000 条记录，日常可记录 50~100 天）触发回滚时，主控直接以**“最早整天”为单位按整扇区擦除释放**，无文件碎片整理负担，避免在运动中途进行耗时耗电的垃圾回收。

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
│   ├── SGM6040A_Datasheet.pdf                   # 圣邦微 SGM6040A 60nA 超低功耗降压 DCDC 规格书
│   ├── SGM2036_Datasheet.pdf                    # 圣邦微 SGM2036 300mA 低噪声受控 LDO 规格书
│   ├── SGM4056_Datasheet.pdf                    # 圣邦微 SGM4056 线性锂电充电 IC 规格书
│   ├── LIS2MDL_Datasheet.pdf                    # STMicroelectronics LIS2MDL 3轴磁力计规格书
│   ├── BMP388_Datasheet.pdf                     # Bosch BMP388 高精度气压计规格书
│   └── W25Q128JV_SPI_Flash.pdf                  # 华邦 16MB SPI NOR Flash 规格书
├── Hardware/                                    # [规划中] 硬件设计工程文件 (立创 EDA / KiCad)
│   ├── Schematics/                              # 原理图设计与 PDF 导出
│   ├── PCB/                                     # PCB 布局布线工程与 Gerber 光绘文件
│   └── BOM/                                     # 元器件物料清单
├── Firmware/                                    # [规划中] 嵌入式固件源码 (基于 Zephyr RTOS / nRF Connect SDK)
│   ├── drivers/                                 # 传感器与外设底层驱动 (LSM6DSV16X, ATGM336H, SGMICRO)
│   ├── algorithms/                              # 惯导与航位推算算法 (PDR, SFLP, EKF)
│   └── app/                                     # 业务逻辑、低功耗状态机与每日打包存储
└── README.md                                    # 本项目总览与架构设计说明
```

---

## 11. 开发路线图 (Roadmap)

- [x] **架构设计与选型论证**：完成主控（nRF52840 QFN48）、双频 GNSS（ATGM336H-F8N-76）、IMU（LSM6DSV16X）及全套圣邦微电源（SGM6040A 60nA DCDC + SGM2036 LDO + SGM4056）的系统论证与功耗三档数学建模。
- [x] **技术资料归档**：收集并整理全部关键核心器件原厂规格书放入 `Doc/` 目录，剔除错误历史手册。
- [x] **芯片限制与封装适配**：明确 nRF52840 QFN48 供电限制（无高压模式）及 USB 引脚缺失，重新规划 28 个物理 GPIO，独立分配调试串口 DEBUG_UART，并规划 32MHz HFXO 与 32.768kHz LFXO 双晶体。
- [x] **交互架构与存储定义**：确立 KEY1 与 KEY2 双按键功能，设计标准 32 字节对齐结构体与每日打包（Daily Chunking）断电安全存储机制。
- [ ] **硬件原理图绘制**：
  - [ ] 完成 SGM4056 充电电路（350mA 恒流）与 SGM6040A 60nA 降压 DCDC 电路（3.3V 统一逻辑电平）；
  - [ ] 完成 SGM2036-3.3 独立受控 LDO 供电与 ATGM336H-F8N-76 双频有源陶瓷天线前端电路（带 1kΩ 信号防倒灌电阻）；
  - [ ] 完成 nRF52840 QFN48 最小系统、32MHz/32.768kHz 双晶体电路与 DEBUG_UART 测试焊盘；
  - [ ] 完成 LSM6DSV16X 与 Sensor Hub（LIS2MDL、BMP388）总线连接；
  - [ ] 完成双按键消抖电路、三色 LED 指示灯及电池微安级分压采样电路。
- [ ] **PCB 布局布线与打样**：4 层高密度沉金板设计，严格规划 2.4GHz BLE 天线与顶层 GNSS 陶瓷天线净空区，严格控制射频 50Ω 阻抗并隔离高频噪声。
- [ ] **固件开发与算法验证**：
  - [ ] 基于 Zephyr / nRF Connect SDK 搭建超低功耗电源管理框架；
  - [ ] 移植 ST 官方 MLC / SFLP 驱动库并实现离线步长自适应 PDR 算法与乘车检测过滤；
  - [ ] 实现每日打包 Flash 循环存储系统与 BLE 5.0 (2Mbps PHY) 手机端增量同步。
- [ ] **整机组装与户外长周期实测**：多场景实地续航与轨迹平滑度测试验证。

---

## 许可协议 (License)
本项目硬件设计与原理图规划遵循 **CERN-OHL-P-2.0** 开源硬件许可协议；固件源码遵循 **MIT License** 开源协议。详情请参阅 [LICENSE](LICENSE) 文件。
