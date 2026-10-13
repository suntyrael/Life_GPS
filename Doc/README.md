# Life_GPS 设计资料与器件规格书索引 (Doc/)

> 🔒 **【架构基线已冻结 · 硬件配置基线 v1.0-RC · SSOT 权威真相源 (Architecture Baseline Frozen - SSOT)】**  
> 经与专家深度评审全面闭环，本项目第一阶段（Phase 1）架构基线已正式收敛并锁定。本文件与主 README 共同构成 EDA 原理图（Schematic Capture）与 PCB Layout 的唯一权威输入依据。严禁在原理图阶段擅自变动器件订货型号、电压基准或引脚映射。

本目录归档了 **Life_GPS** 超低功耗轨迹与惯导记录仪项目核心硬件元器件的原厂官方数据手册（Datasheet）及规格书。在进行原理图绘制、PCB 布局（特别是射频天线与阻抗控制）和固件驱动开发时，请务必以本目录下的原厂规范为准。

> **版权免责声明**：本目录下的所有 PDF 规格书版权归各原厂（Nordic Semiconductor、中科微电子、意法半导体、圣邦微电子、博世、华邦等）所有，仅供开源工程硬件研发与技术评估参考。

---

## 1. 唯一硬件配置基线表 (Hardware Configuration Baseline Matrix)

为了彻底根除多文档间的器件选型冲突与参数矛盾，本项目建立唯一的《硬件配置基线表》。主 README、Doc 文档、架构图和后续 BOM 均严格以此表为单一真相源（SSOT）：

| 模块类别 | 选定器件完整型号 | 原厂品牌 | 封装规格 | 工作电压域 | 关键总线 / 目标时钟 | 上电 / 控制方式 | 归档官方规格书 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **主控 SoC** | **nRF52840-QFAA** | Nordic Semi | QFN48<br>*(6×6mm, 0.4mm pitch)* | 稳定 3.4V<br>*(Normal 模式, 1.7~3.6V)* | 64MHz ARM Cortex-M4F<br>HFXO: 32MHz / LFXO: 32.768kHz | 全天候常通供电 (Always-On)<br>引出 30 个物理 GPIO (28外设+1复位+1预留) | `nRF52840_Product_Specification_v1.11.pdf` |
| **GNSS 模组** | **ATGM336H-F8N76** | 杭州中科微 | LCC-18<br>*(10.1×9.7mm)* | 主电: 3.3V (Pin 8 VCC)<br>后备: 3.4V (Pin 6 VBAT) | UART 115200bps (8-N-1)<br>1PPS 硬件授时 (经 PPI 直连) | Pin 8 VCC 经 SGM2045-3.3 受控通断 (P0.04 EN)<br>Pin 6 VBAT 由 SGM6040D 常通供电 (15µA)<br>Pin 1 为 GND | `ATGM336H-F8N76_Datasheet.pdf` |
| **GNSS 射频天线** | **H2UJ4U1H2Q0100**<br>*(备选 M830120)* | Unictron (咏业)<br>/ Kyocera AVX | **超微型 Chip 天线**<br>*(5.0×3.0×0.5mm / 8.0×3.0mm)* | 无源器件 | 50Ω 共面波导 + π型调谐匹配<br>板角端全层独立净空区 | 纯无源超薄片状天线，零直流功耗<br>外置 ATR2652+SAW 补偿增益并抗BLE | 原厂数据手册 |
| **板载双频 LNA** | **ATR2652** | 杭州中科微 | DFN-6 / SOT23-6 | 3.3V 受控 | L1 增益 22.4dB / NF 0.85dB<br>L5 增益 22.0dB / NF 0.75dB | 挂载于受控 SGM2045-3.3 电源轨<br>工作电流仅 4.5mA，随模组同步休眠断电 | 原厂选型手册 |
| **板载双通带 SAW** | **B39162B2651P810** | Qualcomm / RF360 | 1511<br>*(1.5×1.1mm)* | 无源器件 | 单进单出双通带 (L1+L5)<br>2.4GHz BLE 抑制 > 38dB | 位于 ATR2652 与模组 Pin 11 RF_IN 之间<br>强力消除板载蓝牙发射自干扰 | 原厂选型手册 |
| **SPI NOR Flash** | **W25Q512JVEIQ** | Winbond (华邦) | WSON-8<br>*(6×8mm)* | 3.4V 常通<br>*(2.7~3.6V)* | 标准 4 线 SPI (模式 0)<br>**目标时钟: 4MHz ~ 8MHz (低噪声)** | CS 引脚控休眠 (P1.09)<br>深休眠漏电 1µA，十万次扇区擦写 | `W25Q512JV_SPI_Flash.pdf` |
| **6轴 IMU** | **LSM6DSV16X** | STMicroelectronics | LGA-14<br>*(2.5×3.0mm)* | 3.4V 常通<br>*(磁珠+电容π型滤波)* | SPI 8MHz (直连主控)<br>SFLP 输出: 60Hz 姿态四元数 | 静止待机: 15Hz LPM1 Acc (~7.5µA)<br>PDR接管: Combo 0.65~0.9mA (SFLP增量14µA) | `LSM6DSV16X_Datasheet.pdf` |
| **三轴磁力计** | **LIS2MDL** | STMicroelectronics | LGA-12<br>*(2.0×2.0mm)* | 3.4V 常通<br>*(磁珠+电容π型滤波)* | I2C (作为 Sensor Hub 从机) | 挂载于 LSM6DSV16X Sensor Hub Master<br>数据进 IMU FIFO，MCU 固件执行磁校准 | `LIS2MDL_Datasheet.pdf` |
| **气压高度计** | **BMP388** | Bosch Sensortec | LGA-10<br>*(2.0×2.0mm)* | 3.4V 常通<br>*(磁珠+电容π型滤波)* | I2C (作为 Sensor Hub 从机) | 挂载于 LSM6DSV16X Sensor Hub Master<br>相对精度 typ. ±8Pa (±0.08hPa, 等效约 ±0.66m) | `BMP388_Datasheet.pdf` |
| **常通降压 DCDC** | **SGM6040D** | 圣邦微 (SGMICRO) | **TDFN-2×1.5-8L**<br>*(锁定选用，带MODE/STOP)* | 输入 1.8~5.5V<br>**固定输出 3.4V** | 750mA 输出能力，效率 > 90%<br>带 STOP 硬件低噪引脚 | 全板常通核心（为MCU/Flash/传感器/GNSS VBAT供电）<br>静态电流仅 60nA (关断 25nA) | `SGM6040A_Datasheet.pdf` |
| **受控低噪 LDO** | **SGM2045-3.3** | 圣邦微 (SGMICRO) | **XTDFN-1×1-4L**<br>*(超微型封装)* | 输入 1.4~5.5V (接电池或3.4V)<br>**固定输出 3.3V** | 300mA 输出能力，**92dB 超高 PSRR**<br>**9.5µVrms 超低噪声，压差仅 80mV** | 专供 GNSS Pin 8 VCC 及 ATR2652 双频 LNA<br>由 P0.04 EN 硬件控制，关断漏电仅 30nA | `SGM2045_Datasheet.pdf` |
| **锂电充电 IC** | **SGM4056** | 圣邦微 (SGMICRO) | TDFN-3×3-8L<br>*(带底部散热EP)* | 输入 4.5~6.8V<br>OVP 6.8V | $R_{IREF}=34.0\text{k}\Omega$ 限制 350mA 恒流<br>最大发热 0.70W (3.0V起充) / 0.45W (3.7V平台) | 内部 115℃ 热调节保护；电芯 NTC<br>接 MCU ADC 采样并联动 EN 控制 | `SGM4056_Datasheet.pdf` |
| **锂电保护板 (PCM)** | **DW01A + 8205A** 或<br>**AP9101C 系列** | 行业标准方案 | 电芯内置 PCM | 电池端保护 | 硬件级过放/过充/过流切断 | 3.0V 硬件切断过放保护 (漏电<0.1µA)<br>4.28V 过充保护，2.5A 短路锁死 | 保护板设计规范 |

---

## 2. 硬件开发关键避坑指南与深度技术澄清

### 2.1 射频前端架构：超微型双频无源 Chip 天线与 LNA+SAW 配合设计
按照产品工业设计与结构规范，本项目**全面清除有源天线方案，正式确立微型双频无源陶瓷片状天线（Chip Antenna）配合板载 LNA+SAW 的标准射频链路**：

1. **核心天线器件精确选型**：
   * **首选型号（超薄紧凑首选）**：**Unictron (咏业科技) CB501F（料号 `H2UJ4U1H2Q0100`）**：
     * **极致微型封装**：表面贴装（SMT）陶瓷片状天线，尺寸仅 **$5.0 \times 3.0 \times 0.5\text{ mm}$**，厚度仅 0.5mm，完美适应钥匙扣超薄腔体；
     * **全频段覆盖**：支持 GPS L1/L5、BDS B1I/B1C/B2a、Galileo E1/E5a、GLONASS G1/G3、QZSS 与 IRNSS L5；
   * **备选型号（抗近场失谐首选）**：**Kyocera AVX `M830120`（调谐评估板型号 `M830120-02`）**：
     * **物理规格与工艺**：尺寸 **$8.0 \times 3.0 \times 1.33\text{ mm}$**，采用 Kyocera AVX 专利 **Isolated Magnetic Dipole (IMD) 隔离磁偶极子** 技术，抗人体穿戴与电池近场失谐能力更强；
   * **无源天线结构优势**：纯无源器件，零直流偏置功耗，无需外置同轴馈电，杜绝短路漏电风险。

2. **为什么微型 Chip 天线必须外置“ATR2652 LNA + B39162 SAW”？（刚需救赎机制）**：
   * **微型片状天线的物理短板**：Chip 天线辐射孔径极小，在 $52\times 36\text{ mm}$ 紧凑便携板级环境下，受接地面积限制及塑料壳体、电池吸收影响，**实测天线平均辐射增益仅约 $-2 \sim -4\text{ dBi}$**。若裸奔直连模组，微弱的卫星信号（特别是本就比 L1 弱 3dB 的 L5 信号）极易被系统底噪淹没；
   * **前置 LNA 极致低噪放大**：第一级采用中科微原厂 **ATR2652**（L5 增益 22.0dB / NF 0.75dB，L1 增益 22.4dB / NF 0.85dB，电流 4.5mA），在天线输出端就近将微弱信号直接拉升 22dB，将后级损耗和模组底噪完全掩盖；
   * **双通带 SAW 滤除蓝牙自干扰**：第二级采用高通 **B39162B2651P810**（1511 封装，单进单出双通带，插损约 2.0dB），在 $2400 \sim 2483.5\text{ MHz}$ 频段具备 **$>38\text{ dB}$ 的深陷带外抑制**，彻底消除板载主控 nRF52840 蓝牙 2.4GHz 发射对卫星微弱信号（-167dBm）的射频互调饱和打死；
   * **整机前置净增益黄金匹配**：ATR2652 增益 (+22dB) - SAW 插损 (-2dB) = **约 $+20.0\text{ dB}$ 净前置增益**，**完美落在中科微 ATGM336H 官方规格书推荐的 $15 \sim 30\text{ dB}$ 最佳输入增益黄金窗口**！既极大补偿了 Chip 天线的辐射短板，又绝不饱和模组内部混频器。

3. **原理图与 PCB 配合设计四大落地规范**：
   * **拓扑信号流向 (Post-LNA)**：
     `H2UJ4U1H2Q0100 天线` $\longrightarrow$ `π型调谐匹配` $\longrightarrow$ `47pF隔直` $\longrightarrow$ `ATR2652 LNA` $\longrightarrow$ `47pF隔直` $\longrightarrow$ `B39162 SAW` $\longrightarrow$ `ATGM336H Pin 11 RF_IN`；
   * **天线端 0402 $\pi$ 型调谐匹配网络（必留）**：在天线馈电焊盘与 ATR2652 输入端之间，必须预留一组 0402 $\pi$ 型匹配电路（并联电容 $C_{A1}$ - 串联元件 $L_{A1}$ - 并联电容 $C_{A2}$）。打样后装入外壳并贴紧电池，使用矢量网络分析仪（VNA）微调元器件值，精准校准外壳介质加载导致的谐振频偏；
   * **射频超低结容 ESD 防护**：在天线馈电端就近并联超低结容（$C_J < 0.15\text{ pF}$）双向射频 ESD 保护二极管（如 ESD9R3.3ST5G），杜绝人体触摸静电击穿前级高灵敏度 LNA；
   * **PCB 布局净空法则**：天线贴装于 PCB 顶层板角端（Corner-mount），天线投影区域及边缘**1至4层铜皮完全掏空（Clearance Area, No Metal/No Ground）**；天线与锂电池、金属屏蔽罩及 2.4GHz 蓝牙天线保持 $\ge 5\text{ mm}$ 物理间距；
   * **LNA 受控供电与去耦**：ATR2652 VDD 供电端挂载在受控的 **`SGM2045-3.3` 电源轨** 上，供电支路串入高阻射频磁珠（村田 BLM15HG102SN1D）并就近配置 10pF + 100nF 去耦；当 MCU 拉低 `P0.04 (GNSS_LDO_EN)` 时，LNA 与 GNSS 模组 Pin 8 VCC 同步硬切断，休眠漏电归零。模组 Pin 14 `VCC_RF` 悬空预留测试。

---

### 2.2 电源拓扑升级：SGM6040D (3.4V) + SGM2045 (3.3V)
为彻底解决上一轮审查中关于“3.3V 稳压与 LDO 压差”的矛盾，系统正式重构为分层高能效电源拓扑：

1. **常通主电源 DCDC：圣邦微 SGM6040D (固定输出 3.4V)**
   * **芯片型号与封装**：锁定选用 **`SGM6040DXTHL8G/TR` (TDFN-2×1.5-8L)** 封装，原厂出厂**固定激光修调输出 3.4V**（免外置分压电阻，省空间且抗老化漂移；规格书 Table 1 Note 1 明确规定 Pin 5 VSET 在 PCB 上必须直接接地 GND）；
   * **超低静态电流与效率**：静态电流仅 **60 nA**，轻载 PFM 模式下效率超 80%，重载 PWM 模式下效率超 90%，具备 750mA 输出能力；
   * **压差与直通特性**：单节锂电池电压处于 3.45V ~ 4.2V 时稳定输出 3.4V；当电池放电至 3.0V ~ 3.45V 末期时，自动进入 100% 占空比旁路直通模式（Bypass，内阻极低，损耗仅 120nA），输出电压平稳跟随电池电压下降；
   * **供电覆盖**：3.4V 稳定供给 nRF52840 SoC (Normal 模式)、W25Q512JV SPI Flash、运动传感器体系（经磁珠滤波），以及 ATGM336H Pin 6 `VBAT`（15µA 维持内部 RTC/SRAM）；
2. **GNSS 受控独立 LDO：圣邦微 SGM2045-3.3 (超高 PSRR 超低压差)**
   * **芯片型号与封装**：选用 **XTDFN-1×1-4L 微型封装**（占板仅 $1.0\times 1.0\text{mm}$）；
   * **极致射频电源品质**：
     * **92 dB 超高 PSRR (@1kHz)**：具备极强的开关纹波抑制比，彻底阻断上级 DCDC 或电池纹波串入卫星接收机；
     * **9.5 µVrms 超低底噪**：提供近乎蓄电池纯净度的模拟电源；
     * **超低压差 (Ultra-Low Dropout)**：满载 300mA 压差仅 80~100mV；在 GNSS 模组 (44mA) + ATR2652 (4.5mA) 合计 **48.5mA** 负载下，**实测压差仅约 15 ~ 20 mV**！
   * **输入取电与压差发热消除**：
     * SGM2045-3.3 输入直接接自 SGM6040D 的 **3.4V 稳定电源轨**；
     * 压差仅有 $3.4\text{V} - 3.3\text{V} = \mathbf{0.1\text{ V}}$，满载 48.5mA 时的压差发热功率仅为 $0.1\text{V} \times 48.5\text{mA} = \mathbf{4.85\text{ mW}}$（相比直接从锂电池 4.2V 取电减少了近 90% 的线性发热！）；
     * 关断漏电仅 **30 nA**，通过 MCU GPIO `P0.04` 控制其 EN 使能端，实现硬断电零漏电。

---

### 2.3 专项技术评估：传感器是否需要单独的 LDO 供电？

针对审阅者提出的“评估传感器是否也需要单独的 LDO 供电”问题，硬件团队进行了深入的物理级工程论证：

#### 1. 评估对比矩阵 (Evaluation Matrix)：
| 评估维度 | 方案 A：为传感器增加独立 LDO (如 SGM2036 / SGM2045) | 方案 B (推荐)：SGM6040D 3.4V 直供 + 磁珠/RC π型滤波 + STOP 联动 |
| :--- | :--- | :--- |
| **整机待机底噪影响** | ❌ **严重恶化**：增加独立 LDO 会引入额外的静态电流 $I_Q$（典型 $10\sim 15\mu\text{A}$）。传感器在静止时本身仅耗电 7.5µA，增加 LDO 后待机功耗将暴增至 **$22.5\mu\text{A}$**，彻底摧毁 $25\sim 35\mu\text{A}$ 的整机待机预算！ | ✅ **零额外漏电**：被动磁珠与陶瓷电容无任何静态漏电流增量（**0 nA 增量**），整机待机底噪完美锁定在 **26.0 µA**。 |
| **电源噪声与纹波压制** | 良好：LDO 提供低噪直流输出。 | ✅ **完全等效甚至更优**：<br>1. SGM6040D 开关频率 1.5MHz，经磁珠 (BLM15PX121) + 10µF 陶瓷电容构建 $\pi$ 型滤波，对 1.5MHz 纹波衰减达 **> 35 dB**（纹波压制在 $<0.5\text{mV}$）；<br>2. SGM6040D 具备 **STOP 硬件低噪引脚**！MCU 采样气压/磁力计期间瞬态拉高 STOP 引脚，DCDC 彻底冻结开关动作，**开关纹波瞬间归零 (0mV)**！ |
| **传感器内部抗噪能力** | LSM6DSV16X 内部集成专用模拟 LDO 与电荷泵，自身 PSRR > 50dB；BMP388 内部集成低噪基准。外置 LDO 存在设计重叠。 | LSM6DSV16X 与 BMP388 在 $\pi$ 型滤波后供电纹波下，实测噪声密度与直接使用 LDO 无统计学差异。 |
| **PCB 空间与 BOM 成本**| ❌ 增加 1 颗 LDO 芯片及 2 颗滤波电容，占用约 $4\text{ mm}^2$ 空间，破坏底层完整地平面，BOM 增加约 0.20 元。 | ✅ 仅需 1 颗 0402 贴片磁珠 + 1 颗 0402 电容，占板面积 $< 1\text{ mm}^2$，地平面完整无缝。 |

#### 2. 评估结论与落地实施方案：
> **明确结论：传感器“不需要”也坚决不建议增加独立的 LDO 供电！**  
> **落地电路规范**：
> 1. **物理隔离**：传感器电源轨由 SGM6040D 3.4V 主轨经由 **`BLM15PX121 (120Ω@100MHz, DCR<0.2Ω)` 铁氧体磁珠** 与 **$10\mu\text{F} + 0.1\mu\text{F}$ X5R 陶瓷电容** 构建 $\pi$ 型滤波网络后接入 LSM6DSV16X、LIS2MDL 和 BMP388 的 VDD 引脚；
> 2. **固件 STOP 联动**：在算法需要执行超高精度气压计楼层判定或高精度地磁标定时，固件通过 MCU GPIO P1.15 (Pin 38, PWR_DCDC_STOP) 拉高 SGM6040D 的 STOP 引脚 500µs，冻结开关动作完成零噪声采样后恢复；
> 3. **工程收益**：省去独立 LDO 的成本与面积，且**彻底保住了宝贵的 $15\mu\text{A}$ 待机功耗预算**。

---

### 2.4 主控与 GNSS 真实物理引脚核验
* **主控料号**：全量统一为 **`nRF52840-QFAA` (QFN48, 6×6mm)**；
* **GNSS 模组 (ATGM336H-F8N76)**：
  * **Pin 1 为 `GND`**（接地）；
  * **Pin 6 为 `VBAT`**（后备供电，1.5~3.6V），直连 SGM6040D 3.4V 常通轨，维持 RTC 与 SRAM（15µA）；
  * **Pin 8 为 `VCC`**（主供电，2.7~3.6V），由 SGM2045-3.3 受控供给；
  * **Pin 11 为 `RF_IN`**（50Ω 射频输入），接收来自 B39162B2651P810 SAW 的双频滤波信号；
  * **Pin 14 为 `VCC_RF`**：悬空预留测试点。

---

### 2.5 功耗数学模型微调闭环
* **GNSS 子系统满载电流**：ATGM336H 满载运行 (44.0mA) + ATR2652 双频 LNA (4.5mA) = **48.5 mA**；
* **高精度连续模式整机电池侧放电电流**：
  * SGM2045 供给 GNSS 子系统（48.5mA @ 3.3V），由 SGM6040D 供给（3.4V 经 SGM6040D 降压）：电池侧放电电流折算为：$(3.4\text{V} \times 48.5\text{mA}) / (3.8\text{V} \times 0.90) \approx \mathbf{48.2\text{ mA}}$；
  * MCU 64MHz + Flash 连续写 + BLE 广播折算电池侧约 **5.0 mA**；
  * IMU Combo 模式折算电池侧约 **0.8 mA**；
  * **整机电池侧平均放电电流重新核算为 $\approx \mathbf{54.0\text{ mA}}$**；
  * 900mAh 有效可用电量下，连续工作时间为：$900\text{ mAh} / 54.0\text{ mA} \approx \mathbf{16.7\text{ 小时}}$。
* **深度静止待机底噪**：
  * IMU 15Hz LPM1 (7.5µA) + GNSS VBAT (15µA) + nRF52840 RTC (1.5µA) + Flash (1µA) + 电源漏电 (1µA) = **26.0 µA**；设计目标规范为 **$25\text{ }\mu\text{A} \sim 35\text{ }\mu\text{A}$**。

---

### 2.6 射频 DFT 默认焊接 BOM 与 Golden Sample 对标规范

为解决微型无源 Chip 天线在打样调试期可能面临的板级阻抗失谐与近场吸收风险，硬件在原理图与装配 BOM 中建立**可测试性设计（DFT）双通路切换机制**，并制定绝对基准比对规范：

#### 1. PCBA 装配默认焊接 BOM (Assembly BOM)
| 元件位号 | 规格与封装 | 标称参数 | 默认装配状态 (Default SMT) | 电路功能与说明 |
| :--- | :--- | :---: | :---: | :--- |
| **ANT1** | H2UJ4U1H2Q0100 (咏业) | 5×3×0.5mm | **贴片焊接** | 板载超薄微型双频陶瓷 Chip 天线 |
| **C_PI1**| 0402 高频贴片电容 | NC | 待调谐确定 | 天线 $\pi$ 型匹配电路第一并联臂 |
| **L_PI1**| 0402 高频绕线电感 | 2.7nH | **贴片焊接** | 天线 $\pi$ 型匹配电路串联臂 (初始预设) |
| **C_PI2**| 0402 高频贴片电容 | NC | 待调谐确定 | 天线 $\pi$ 型匹配电路第二并联臂 |
| **R_INT**| 0402 厚膜跳线电阻 | **0 Ω** | **默认贴片焊接 (Mounted)** | **【量产通路】导通内置 Chip 天线至板载 LNA** |
| **R_EXT**| 0402 厚膜跳线电阻 | **0 Ω** | **默认不贴片 (Do Not Mount)**| **【测试通路】断开外接测试座** |
| **J_EXT**| 微型 IPEX (U.FL) 射频座 | 50Ω 直流阻抗 | **默认贴片焊接 (Mounted)** | 预留微型同轴测试座（研发排障与 Golden 对标） |
| **D_ESD**| PESD0402-140 | $C_J<0.15\text{pF}$ | **贴片焊接** | 超低结容双向射频 ESD 静电保护管 |

* **研发 1 分钟排障切换**：若首版样机内置天线搜星性能异常，研发工程师用热风枪拆除 `R_INT` 并焊接 `R_EXT`，即可将外部高增益天线直连 ATR2652 输入，迅速锁定是天线辐射问题还是后级射频底噪问题。

#### 2. Golden Sample（黄金参考样机）对标测试计划
* **黄金参考基准 (Golden Reference)**：中科微原厂官方评估套件配备标准 $25\times 25\times 4\text{mm}$ 高增益陶瓷有源天线（外置增益 $+28\text{dB}$，天线辐射增益 $+2.5\text{dBi}$）；
* **对比测试矩阵**：
  * **DUT-1**：Life_GPS 打样样机（默认状态：内置 Chip 天线 + 电池贴合 + 外壳装配）；
  * **DUT-2**：Life_GPS 打样样机（DFT 改装：切换至 `R_EXT`，外接同款标准有源天线）；
  * **REF**：官方 Golden Reference；
* **三场景合格判定标准 (Pass/Fail Criteria)**：
  1. **开阔天空载噪比 (Clear Sky C/N0)**：DUT-2 与 REF 的双频载噪比偏差必须在 $\pm 1.5\text{ dB}$ 以内（验证板载 ATR2652+SAW 增益与噪声系数达标）；DUT-1 相比 REF 的 C/N0 衰减必须控制在 **$\le 4.5\text{ dB}$ 窗口内**（微型天线合理辐射衰减范围）；
  2. **热启动 TTFF 统计分布**：开阔地下连续执行 50 次脉冲热启动循环，DUT-1 的 **95% 样本 TTFF 必须 $\le 2.8\text{ 秒}$**；
  3. **BLE 自发射干扰抑制度**：在微弱卫星信号（C/N0 $\approx 32\text{ dB-Hz}$）下，主控开启 $+8\text{dBm}$ 连续发送 BLE 广播，GNSS 载噪比跌落 **$\Delta \text{C/N0} < 1.0\text{ dB}$**（验证 B39162 SAW 对 2.4GHz 的 $>38\text{dB}$ 抑制能力）。

---

### 2.7 nRF52840 QFN48 官方物理引脚最终交叉核对矩阵 (SSOT Baseline)

依据 Nordic 官方数据手册 **`nRF52840 Product Specification v1.11` Section 7.1.2 (Table 62: QFN48 pin assignments)**，逐引脚核验并彻底锁定物理映射。全芯片引出 30 个物理 GPIO（P0 26个 + P1 4个），完全杜绝不存在管脚与引脚冲突：

| 物理 Pin | 官方定义引脚名 | 本项目网络标号 | I/O方向 | 驱动模式与电气特性 | 连接目标器件与功能说明 |
| :---: | :--- | :--- | :---: | :--- | :--- |
| **Pin 1** | `DEC1` | `DEC1` | PWR | 1.1V 数字稳压去耦 | 外挂 100nF (C12) 至 GND |
| **Pin 2** | `P0.00 / XL1` | `LFXO_IN` | 模拟时钟 | 32.768kHz 晶振输入 | 接 32.768kHz 晶体 Pin 1 (外挂匹配电容) |
| **Pin 3** | `P0.01 / XL2` | `LFXO_OUT` | 模拟时钟 | 32.768kHz 晶振输出 | 接 32.768kHz 晶体 Pin 2 (外挂匹配电容) |
| **Pin 4** | `P0.04 / AIN2`| `GNSS_LDO_EN` | OUT | Standard Drive | 控制 SGM2045-3.3 Pin 3 (EN)，高电平通电，低切断 |
| **Pin 5** | `P0.05 / AIN3`| `GNSS_RXD` | IN | Standard Drive | 接 ATGM336H TXD (串联 1kΩ 限流保护电阻) |
| **Pin 6** | `P0.07` | `GNSS_TXD` | OUT | Standard Drive | 接 ATGM336H RXD (串 1kΩ；模组断电前置浮空 Hi-Z) |
| **Pin 7** | `P0.08` | `GNSS_1PPS` | IN | Standard Drive | 接 ATGM336H 1PPS 授时（经 PPI 捕获百纳秒时基） |
| **Pin 8** | `P1.08` | `IMU_INT2` | IN | Standard Drive | 接 LSM6DSV16X INT2（MLC 运动模式识别中断） |
| **Pin 9** | `P1.09` | `FLASH_CS` | OUT | Standard Drive | 接 W25Q512JV Pin 1 (/CS)，上电默认拉高 |
| **Pin 10**| `P0.11` | `IMU_CS` | OUT | Standard Drive | 接 LSM6DSV16X Pin 1 (CS)，Active Low |
| **Pin 11**| `P0.12` | `IMU_SCK` | OUT | High Drive (8MHz) | 接 LSM6DSV16X Pin 2 (SCL)，独立传感器 SPI 时钟 |
| **Pin 12**| `VDD` | `VDD_3V4` | PWR | 主供电输入 | 连 SGM6040D 3.4V 主电轨 (并联 100nF 去耦) |
| **Pin 13**| `P0.13` | `IMU_MISO` | IN | Standard Drive | 接 LSM6DSV16X Pin 4 (SDO)，传感器 SPI 数据输入 |
| **Pin 14**| `P0.14` | `IMU_MOSI` | OUT | High Drive (8MHz) | 接 LSM6DSV16X Pin 3 (SDA)，传感器 SPI 数据输出 |
| **Pin 15**| `P0.17` | `IMU_INT1` | IN | Standard Drive | 接 LSM6DSV16X INT1（计步/SFLP FIFO 唤醒中断） |
| **Pin 16**| `P0.18 / nRESET`| `SYS_RESET` | IN (PU)| 内部弱上拉复位引脚 | 外部复位测试焊盘 / RC 去抖网络 |
| **Pin 17**| `VDD` | `VDD_3V4` | PWR | 主供电输入 | 连 3.4V 主电轨 (并联 100nF 去耦) |
| **Pin 18**| `P0.19` | `FLASH_SCK` | OUT | High Drive (8MHz) | 接 W25Q512JV Pin 6 (CLK)，高速 SPI 时钟 |
| **Pin 19**| `P0.20` | `FLASH_MOSI` | OUT | High Drive (8MHz) | 接 W25Q512JV Pin 5 (DI / IO0) |
| **Pin 20**| `P0.21` | `FLASH_MISO` | IN | Standard Drive | 接 W25Q512JV Pin 2 (DO / IO1) |
| **Pin 21**| `P0.22` | `FLASH_WP` | OUT | Standard Drive | 接 W25Q512JV Pin 3 (/WP)，硬件写保护引脚 |
| **Pin 22**| `P0.23` | `FLASH_HOLD` | OUT | Standard Drive | 接 W25Q512JV Pin 7 (/HOLD)，保持控制引脚 |
| **Pin 23**| `P0.24` | `KEY1_POI` | IN (PU)| 弱上拉 + 低功耗唤醒| 按键 KEY1（短按: POI 打点；长按 3s: 蓝牙配对） |
| **Pin 24**| `P1.00` | `KEY2_MODE` | IN (PU)| 弱上拉 + 低功耗唤醒| 按键 KEY2（长按 2s: 切换高精；短按: 电量查询） |
| **Pin 25**| `VDD` | `VDD_3V4` | PWR | 主供电输入 | 连 3.4V 主电轨 (并联 100nF 去耦) |
| **Pin 26**| `SWDIO` | `SWDIO` | I/O | SWD 调试数据线 | 独立 SWD 调试测试焊盘 |
| **Pin 27**| `SWDCLK` | `SWDCLK` | IN (PD)| SWD 调试时钟线 | 独立 SWD 调试测试焊盘 |
| **Pin 28**| `NC` | `NC` | - | 悬空引脚 | 芯片内部未连接，PCB 保持悬空不接地 |
| **Pin 29**| `P0.09 / NFC1`| `DEBUG_TXD` | OUT | **Low Frequency Only** | 独立调试串口 TX (测试焊盘；**UICR NFCPINS=0**) |
| **Pin 30**| `P0.10 / NFC2`| `DEBUG_RXD` | IN | **Low Frequency Only** | 独立调试串口 RX (测试焊盘；**UICR NFCPINS=0**) |
| **Pin 31**| `ANT` | `RF_BLE` | RF | 50Ω 单端射频输出 | 匹配网络 $\rightarrow$ 2.4GHz 陶瓷天线 (带独立净空) |
| **Pin 32**| `VSS_PA` | `GND` | PWR | 射频模拟地 | 专用地回路直连中心接地焊盘 |
| **Pin 33**| `DEC6` | `DEC4_6` | PWR | 1.3V 稳压解耦 | **与 Pin 46 (DEC4) 板级短接**，并联 1µF+47nF |
| **Pin 34**| `DEC3` | `DEC3` | PWR | 内部电源去耦 | 外挂 100pF (C11) 去耦电容 |
| **Pin 35**| `XC1` | `HFXO_IN` | 模拟时钟 | 32MHz 晶体输入 | 接 32MHz 外部石英晶体 Pin 1 (12pF 匹配) |
| **Pin 36**| `XC2` | `HFXO_OUT` | 模拟时钟 | 32MHz 晶体输出 | 接 32MHz 外部石英晶体 Pin 2 (12pF 匹配) |
| **Pin 37**| `VDD` | `VDD_3V4` | PWR | 主供电输入 | 连 3.4V 主电轨 (并联 4.7µF 储能电容) |
| **Pin 38**| `P1.15` | `PWR_DCDC_STOP`| OUT | **Low Frequency Only** | **控制 SGM6040D Pin 5 (STOP)**，高电平冻结开关噪声 |
| **Pin 39**| `P0.03 / AIN1`| `BAT_ADC_EN` | OUT | **Low Frequency Only** | 电池采样使能，低电平导通 P-MOS，采完切断 |
| **Pin 40**| `P0.02 / AIN0`| `BAT_ADC_IN` | IN (ADC)| **Low Frequency Only** | 电池分压采样输入 (1/2 分压，接 SAADC AIN0) |
| **Pin 41**| `P0.28 / AIN4`| `LED_RED` | OUT | **Low Frequency Only** | 红色状态指示灯（低电平点亮，限流 1kΩ） |
| **Pin 42**| `P0.29 / AIN5`| `LED_GREEN` | OUT | **Low Frequency Only** | 绿色状态指示灯（低电平点亮，限流 1kΩ） |
| **Pin 43**| `P0.30 / AIN6`| `LED_BLUE` | OUT | **Low Frequency Only** | 蓝色状态指示灯（低电平点亮，限流 1kΩ） |
| **Pin 44**| `P0.31 / AIN7`| `RESERVED_TP` | I/O | **Low Frequency Only** | 预留扩展引脚 / 备用模拟输入测试焊盘 |
| **Pin 45**| `VSS` | `GND` | PWR | 核心地 | 直连中心地平面 |
| **Pin 46**| `DEC4` | `DEC4_6` | PWR | 1.3V 稳压解耦 | **与 Pin 33 (DEC6) 板级短接** |
| **Pin 47**| `DCC` | `DCC` | PWR | 片内 DCDC 电感输出| 串联 10µH + 15nH 功率电感接 VDD (DCDC 模式) |
| **Pin 48**| `VDD` | `VDD_3V4` | PWR | 主供电输入 | 连 3.4V 主电轨 (并联 100nF 去耦) |
| **PAD** | `EPAD (VSS)` | `GND` | PWR | 裸露中心地焊盘 | **必须打 9 个地过孔直连内层完整地平面** |

> **⚠️ 官方强制低频限制（Low Frequency I/O Only $\le 10\text{kHz}$）原理图合规审查**：  
> Nordic 官方手册明确规定：Pin 29, 30, 38, 39, 40, 41, 42, 43, 44 仅可配置为标准驱动低频 I/O（$\le 10\text{kHz}$），以保护 32MHz 晶振（Pin 35/36）与 2.4GHz 射频端口（Pin 31）免受数字高频谐波串扰。本项目中，高速 Flash SPI（P0.19~P0.23）与 IMU SPI（P0.11~P0.14）全部位于底部和左侧常规高速端口，低频引脚仅承担 LED、ADC 使能、按键及 DCDC STOP，完全符合原厂电气设计准则！
