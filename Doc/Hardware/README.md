# Life_GPS 硬件设计资料与器件规格书索引 (Doc/Hardware/)

> 🔒 **【架构基线已冻结 · 硬件配置基线 v2.0 · Main_V0.2 原理图（2026-11-10）闭环】**
> 经与最新原理图 `Main_V0.2`（嘉立创 EDA，更新 2026-11-10）及网表 `Netlist_Main_V0.2_2026-11-11.tel`（已同步）深度评审与闭环整改，硬件配置基线正式锁定：
> 1. **主控升级为 Nordic nRF54L15-QFAA（QFN48）**：Cortex-M33 @128MHz + RISC-V FLPR，1.9V 单轨供电；三段 GPIO 域（P0/LP、P1/PERI、P2/MCU）能力不同，引脚表见 [`Pinmap_Main_V0.2.md`](Pinmap_Main_V0.2.md)（**SSOT**）。
> 2. **GNSS 模组：默认 u-blox MAX-M10S-00B（唯一默认装配料）**。**Quectel LC79H 系列仅作"可选兼容位"**（同尺寸 10.1×9.7mm，VCC 1.75~1.98V），**默认不装配、默认不贴任何兼容跳线**；切换 LC79H 需改 R13/R28/R27 并**增补 Pin15 对地跳线**（详见 §2.4）。固件默认按 u-blox **UBX** 协议工作，NMEA/PAIR 仅作降级兼容。
> 3. **电源拓扑规范**：**常通主电 1.9V (SGM6040A) + GNSS 受控洁净电 1.8V (SGM2045-1.8)** 极低功耗分层架构；系统"关机"为 nRF54L15 进入 System OFF 深度休眠（整机待机漏电 $<1\mu\text{A}$）。
> 4. **GNSS 射频前端：直通版为唯一量产装配**（`C47=22pF` 隔直 + `C46=0Ω` 跳线；`U5/Q1/C24/C27/C31` 全部 NC）；LNA+SAW 增强路径仅保留焊盘与 BOM 位，**互斥装配红线不变**（详见 §2.1）。
> 5. **长按 8 秒开/关机 + LED 双向慢闪 + 硬件看门狗 (30s)** 策略不变；背面贴装 IMU/磁力计旋转变换矩阵 $\mathbf{R}$ 不变（§2.9）。
> 6. **固件必须落实的 5 项内部上拉/极性**：`KEY1_POI`(P1.06)、`KEY2_MODE`(P1.08)、`CHG_STAT`(P0.04)、`SPI_CS`(P2.05)、`LSM6_SPI_CS`(P2.10) 均无外部上拉电阻，须由固件配置片内上拉（12~16kΩ）；`CHG_EN`(P0.03) 为**低有效**。详见 [`Pinmap_Main_V0.2.md`](Pinmap_Main_V0.2.md) §3。
> 7. **本文件与根 `README.md`、[`Pinmap_Main_V0.2.md`](Pinmap_Main_V0.2.md) 共同构成 EDA 投板与固件开发的唯一权威真相源（SSOT）**；引脚以 Pinmap 文件为准，选型以本文件 §1 基线表为准。

本目录（及 `Datasheets/` 子目录）归档了 **Life_GPS** 超低功耗轨迹与惯导记录仪项目核心硬件元器件的原厂官方数据手册（Datasheet）及规格书。在进行原理图绘制、PCB 布局（特别是射频天线与阻抗控制）和固件驱动开发时，请务必以本目录下的原厂规范为准。

> **版权免责声明**：`Datasheets/` 下的所有 PDF 规格书版权归各原厂（Nordic Semiconductor、中科微电子、意法半导体、圣邦微电子、博世、华邦等）所有，仅供开源工程硬件研发与技术评估参考。

---

## 1. 唯一硬件配置基线表 (Hardware Configuration Baseline Matrix)

为了彻底根除多文档间的器件选型冲突与参数矛盾，本项目建立唯一的《硬件配置基线表》。主 README、Doc/Hardware 文档、架构图和后续 BOM 均严格以此表为单一真相源（SSOT）：

| 模块类别 | 选定器件完整型号 | 原厂品牌 | 封装规格 | 工作电压域 | 关键总线 / 目标时钟 | 上电 / 控制方式 | 归档官方规格书 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **主控 SoC** | **`NRF54L15-QFAA-R`** | Nordic Semi | QFN48<br>*(6×6mm, 0.4mm pitch, 49 脚含 EP)* | 稳定 1.9V<br>*(VDD 1.7~3.6V, POR 阈值 1.75V)* | 128MHz Cortex-M33 + RISC-V FLPR<br>HFXO: 32MHz (CL=**8pF**) / LFXO: 32.768kHz (CL=**9pF**)<br>**⚠️ 现选 X2 12pF / X1 12.5pF 超 6~9pF 规格：X2 必须换 8pF；X1 可换 9pF 或实测豁免**<br>RRAM 1524KB / RAM 256KB | 全天候常通供电 (Always-On)<br>31 个 GPIO，分 P0(LP)/P1(PERI)/P2(MCU) 三域<br>**启用片上硬件看门狗 (WDT 30s)** | `nRF54L15_nRF54L10_nRF54L05_Datasheet_v1.0.pdf` |
| **GNSS 模组（默认装配 · 唯一默认料）** | **MAX-M10S-00B** | u-blox | 18-LCC<br>*(10.1×9.7mm)* | 主电: 1.8V (Pin 8 VCC, 1.76~3.6V)<br>IO: 1.8V (Pin 7 V_IO, `VIO_SEL=GND`)<br>后备: 1.9V (Pin 6 V_BCKP, 1.65~3.6V) | UART 115200bps (UBX / NMEA)<br>多星并发 (GPS+BDS+GAL+GLO) | Pin 8 VCC 经 SGM2045-1.8 受控通断（`GPS_EN` = **P1.07**）<br>Pin 6 V_BCKP 由 SGM6040A 常通 1.9V 保持热启<br>**【MAX-M10S 装配】R13=0Ω(贴), R28=100Ω(贴), R27=NC**<br>Pin 15 VIO_SEL **必须接 GND**（1.8V IO 模式） | `Datasheets/UBLOX/MAX-M10S_DataSheet_UBX-20035208.pdf` |
| **GNSS 备份料（兼容位 · 默认不采购/不装配）** | **LC79H 系列**<br>*(LC79H AL / EL)* | Quectel (移远) | 18-LCC + 10-LGA<br>*(10.1×9.7×2.4mm，1~18 脚与 M10S 同构)* | 主电: 1.8V (Pin 8 VCC, **1.75~1.98V**)<br>IO: 跟随 VCC（1.8V）<br>后备: 1.9V (Pin 6 V_BCKP, 1.62~1.98V) | UART 115200bps (NMEA / PAIR)<br>L1+L5 双频 | **【LC79H 装配】R13=NC, R28=NC（Pin5/Pin7 为 RESERVED，禁止外接）**；若用 WAKEUP 唤醒则贴 R27=100Ω(接 Pin 18)<br>固件自适应协议识别 | `Datasheets/LC79H/quectel_lc79h_series_hardware_design_v1-6.pdf` |
| **GNSS 射频天线** | **`BWGNSCNX25-25W4`（U7）** | 待补原厂手册 | `ANT-TH_L250-W250`<br>*(25×25mm 插件/焊盘)* | 无源器件 | 50Ω 馈电 + 预留 π 型调谐位 | **纯无源，零直流偏置**；`Pin 14 VCC_RF` 保持悬空（若改有源天线必须由 VCC_RF 供电 ≤50mA）<br>馈电端缺超低结容 ESD（改版补齐） | BOM 定稿后归档原厂手册 |
| **板载双频 LNA** | **ATR2652（U5）** | 杭州中科微 | DFN-6 / SOT23-6 | 1.8V 受控<br>*(支持 1.5~3.6V)* | L1 增益 22.4dB / NF 0.85dB<br>L5 增益 22.0dB / NF 0.75dB | **本版 DNP（不贴）**：V0.2 量产装配为直通版，仅保留焊盘/BOM 位；贴装时必须同时 NC 掉 C46/C47<br>工作电流 4.5mA，随 GNSS 受控轨同步断电 | 原厂选型手册 |
| **板载双通带 SAW** | **B39162B2651P810（Q1）** | Qualcomm / RF360 | 1511<br>*(1.5×1.1mm)* | 无源器件 | 单进单出双通带 (L1+L5)<br>2.4GHz 抑制 > 38dB | **本版 DNP（不贴）**；GNSS 模组内部已含 SAW，带外抑制由模组承担<br>若启用增强版必须与 C46/C47 互斥 | 原厂选型手册 |
| **SPI NOR Flash** | **W25Q512NWEIQ**<br>*(1.8V 宽压旗舰)* | Winbond (华邦) | WSON-8<br>*(6×8mm)* | 1.9V 常通<br>*(VCC 1.7~1.95V)* | 标准 4 线 SPI (模式 0)<br>**目标时钟: 8MHz (低噪声)** | `/CS`=**P2.05**（固件内部上拉）、`CLK`=**P2.01**、`MOSI`=**P2.02**、`MISO`=**P2.04**（SPIM00 专用脚）<br>/WP(Pin3)与/HOLD(Pin7) 直连 1.9V 主电轨（改版建议改 10kΩ 上拉） | 原厂规格书 (1.8V 系列) |
| **6轴 IMU** | **LSM6DSVTR** | STMicroelectronics | LGA-14<br>*(2.5×3.0mm)* | 1.9V 常通<br>*(磁珠+电容π型滤波)* | SPI 8MHz：`SCL/SPC`=**P1.03**、`SDA/SDI`=**P1.02**、`SDO/SA0`=**P1.13**、`CS`=**P1.14**（`SPIM22` 同域 P1，无需 Constant Latency）<br>SFLP 输出: 60Hz 姿态四元数 | 静止待机: 低功耗加速度计 (~7.5µA)<br>INT1 = **P0.02**（GPIOTE30）运动唤醒与数据就绪中断 | `Datasheets/LSM6DSV16X_Datasheet.pdf` |
| **三轴磁力计** | **LIS2MDL** | STMicroelectronics | LGA-12<br>*(2.0×2.0mm)* | 1.9V 常通<br>*(磁珠+电容π型滤波)* | I2C (作为 Sensor Hub 从机) | 挂载于 LSM6DSVTR Sensor Hub AUX I2C<br>数据进 IMU FIFO，MCU 固件执行磁校准与姿态融合 | `Datasheets/LIS2MDL_Datasheet.pdf` |
| **气压高度计** | **BMP388** | Bosch Sensortec | LGA-10<br>*(2.0×2.0mm)* | 1.9V 常通<br>*(磁珠+电容π型滤波)* | I2C (作为 Sensor Hub 从机) | 挂载于 LSM6DSVTR Sensor Hub AUX I2C<br>相对精度 typ. ±8Pa (±0.08hPa, 等效约 ±0.66m) | `Datasheets/BMP388_Datasheet.pdf` |
| **常通降压 DCDC** | **SGM6040AXTHL8G/TR** | 圣邦微 (SGMICRO) | **TDFN-2×1.5-8L**<br>*(RSET设定 1.9V)* | 输入 1.8~5.5V<br>**设定输出 1.9V** | 750mA 输出能力，效率 > 90%<br>Pin 5 VSET 接 909Ω；Pin 6 STOP 内部下拉悬空 | 全板常通核心（为MCU/Flash/传感器/GNSS后备供电）<br>静态电流仅 60nA (关断 25nA) | `Datasheets/SGM6040A_Datasheet.pdf` |
| **受控低噪 LDO** | **SGM2045-1.8XXDH4G/TR（U9）** | 圣邦微 (SGMICRO) | **XTDFN-1×1-4L**<br>*(超微型封装)* | 输入 1.1~5.5V（**当前接 1.9V 主轨，见 §2.2 待办**）<br>**固定输出 1.8V** | 300mA 输出能力，**92dB 超高 PSRR**<br>**9.5µVrms 超低噪声，额定压差 100mV** | 专供 GNSS `VCC`(Pin8) 与 `V_IO`(Pin7)，即 `VDD_GPS` 轨<br>由 **`GPS_EN` = P1.07** 使能，关断漏电 30nA | `Datasheets/SGM2045_Datasheet.pdf` |
| **锂电充电 IC** | **SGM4056** | 圣邦微 (SGMICRO) | TDFN-3×3-8L<br>*(带底部散热EP)* | 输入 4.5~6.8V<br>OVP 6.8V | $R_{IREF}=34.0\text{k}\Omega$ 限制 350mA 恒流<br>$R_{IMIN}=340\text{k}\Omega$ 设定约 29 mA 截止电流（$I_{MIN}=10169/R_{IMIN}-0.8$） | Pin 3 `/CHG` 接 **P0.04**（固件内部上拉）；Pin 4 `EN` 接 **P0.03**<br>**低有效**：低电平允充，高电平禁止充电（固件勿写反）<br>Pin 2 `PPR` 悬空（不用电源存在指示） | `Datasheets/SGM4056_Datasheet.pdf` |
| **电池电压采样分压** | **1MΩ + 330kΩ** | 华新科 / 国巨 | 0603 / 0402 (1% 精度) | 电池端常通分压 | 直连 SAADC **AIN4 (P1.11)**，C14 100nF | 理想分压比 0.2481（4.2V→1.042V），常通漏电约 3.16µA（长续航取舍）<br>⚠️ SAADC 输入电阻典型 735kΩ 与 R26 并联后实际分压比 ≈0.186（**读数偏低约 25%**），固件须按实测标定修正 | 标称电阻规范 |
| **锂电保护板 (PCM)** | **DW01A + 8205A** 或<br>**AP9101C 系列** | 行业标准方案 | 电芯内置 PCM | 电池端保护 | 硬件级过放/过充/过流切断 | 3.0V 硬件切断过放保护 (漏电<0.1µA)<br>4.28V 过充保护，2.5A 短路锁死 | 保护板设计规范 |

---

## 2. 硬件开发关键避坑指南与深度技术澄清

### 2.1 射频前端架构：BLE 匹配网络 + GNSS 无源天线直通链路

V0.2 射频部分由两条**互相独立**的链路构成，GNSS 侧**只保留直通版本作为量产装配**：

1. **BLE 2.4GHz 链路（主控 ANT 脚）**
   * `U14 Pin31 (ANT)` → **`2.7nH` 串联 → `1.5pF` 并联 → `3.5nH` 串联 → `2.0pF` 并联 → `3.5nH` 串联 → `0.3pF` 并联** → 2.4GHz Chip 天线 `L3`（`ANT-SMD_L55-W25`，5.5×2.5mm 级）；
   * 取值与拓扑对齐 `nRF54L15` Datasheet **Figure 180 / Table 87**（Nordic 建议沿用其参考布局与元件值）；末级 `0.3pF` 与各级并容共同承担二/三次谐波抑制（规格书 802.15.4 二次谐波 −63 dBm、三次 −68 dBm）。**待确认**：第三级串联 `3.5nH` 是否已实际加上（Figure 180 为 3 串 3 并）。
   * **接地特殊要求（网表无法表达，必须落到 layout）**：
     - `Pin32 (VSS_PA)` **只能在 IC 正下方顶层连到 `Pin49`（中心焊盘）**，不得在芯片外围直接接全局地；
     - 第一级并联电容接地端**只在顶层接 `Pin32`**；第二级并联电容接地必须与除**底层地**以外的所有地层隔离（挖空），靠过孔电感形成谐波陷波。
2. **GNSS L1 链路（无源天线直通）**
   * **`U7` 为无源陶瓷天线**（封装 `ANT-TH_L250-W250`，MPN **`BWGNSCNX25-25W4`**，1.57542GHz / 3dBi），馈电经 **`C47 (22pF)` 隔直 → `C46 (0Ω 跳线)`** 后直入 `U1 Pin11 (RF_IN)`；
   * **`Pin14 VCC_RF` 保持悬空**：无源天线不需要直流偏置；若改有源天线，必须由 VCC_RF 供电（≤50 mA）并加限流；
   * **GNSS 模组内部自带 SAW**：L1 带外（尤其 2.4GHz BLE）抑制由模组内部滤波器承担，本版**不需要板载 SAW**；
   * **待补强项（改版）**：天线馈电端缺超低结容 ESD（$C_J<0.15\text{pF}$，如 ESD9R3.3ST5G）与并联调谐位；建议在 `C47`/`C46` 中点及 `RF_IN` 侧各留 DNP 并联焊盘，凑成 π 型便于 VNA 调谐。
3. **双 BOM 互斥红线（保留，但本版只贴直通）**

   | 装配 | `C47` / `C46` | `U5 (ATR2652)`、`Q1 (B39162)`、`C24/C27/C31` | 说明 |
   | :--- | :---: | :---: | :--- |
   | **Option A（V0.2 量产默认）** | **22pF 贴 / 0Ω 贴** | **全部 NC** | 全无源直通，零额外静态功耗 |
   | Option B（增强，仅验证用） | **两颗都必须 NC** | 贴 | 补约 22dB 增益 + 带外抑制（`C46=0Ω` 若留着会把 LNA 输出直接短到天线端） |

   > ⚠️ **正反馈自激红线**：严禁同时贴装（C46+C47）与（U5+Q1）——LNA 输出会经跨接电容倒灌回输入端，构成无衰减正反馈闭环，导致自激甚至烧器件。贴片 SOP 必须核对 BOM 互斥。
   > 本版 `U5/Q1/C24/C27/C31` 已在原理图标注 NC，天线节点至 U5/Q1 的走线成为残桩，layout 需控制 **≤5 mm**，或在分支点串一颗 DNP 0Ω 以便彻底断开。
4. **净空与隔离**：BLE Chip 天线 `L3` 与 GNSS 天线 `U7` 分置板边/板角，投影区 1~4 层全层禁铜，两者间距 ≥15~20 mm（≥5 mm 为下限）；馈线 50Ω 共面波导并做阻抗计算。

---

### 2.2 电源拓扑升级：SGM6040A (1.9V) + SGM2045-1.8 (1.8V) 极低功耗分层架构
依据最新硬件整改要求，系统电源拓扑全面迭代为 **1.9V 常通主电 + 1.8V 洁净射频电** 的极致低能耗架构：

1. **常通主电源 DCDC：圣邦微 SGM6040A (设定输出 1.9V)**
   * **芯片型号与封装**：锁定选用 **`SGM6040AXTHL8G/TR` (TDFN-2×1.5-8L)** 封装；
   * **精确电压设定 ($R_{SET}=909\Omega$)**：依据原厂规格书 Table 1 规范，在 Pin 5 (`VSET`) 与 GND 之间接入一颗标称值为 **$0.909\text{ k}\Omega$（909Ω, $\pm 1\%$ 精度, E96 系列）**的精密贴片电阻，精准设定芯片输出为 **1.9V (`VDD_1V9`)**；
   * **超低静态电流与效率**：静态电流仅 **60 nA**，轻载 PFM 模式下转换效率超 85%，重载 PWM 模式下效率超 92%，具备 750mA 输出能力；在全放电周期（3.0V~4.2V）内始终维持高效降压稳压，避免了 3.4V 方案在 3.0~3.45V 区间进入旁路直通的波动；
   * **STOP 引脚处理**：取消"由主控 GPIO 控制 STOP"的逻辑。**V0.2 原理图/网表中 `U6.6 (STOP)` 为悬空**，依靠片内 490kΩ 下拉维持使能（功能可用）；**改版建议**在芯片焊盘就近接 GND，彻底消除高阻脚被相邻 `Pin7(SW)` 开关噪声误触发（>1.1V 即关断 DCDC 开关）的风险；
   * **供电覆盖**：1.9V 稳定供给 **nRF54L15**（VDD 1.7~3.6V，POR ≥1.75V）、W25Q512NW 1.8V SPI Flash、运动传感器体系（`SENSOR_1V9`，现为 0Ω 直连、磁珠滤波待改版），以及 GNSS `Pin6 V_BCKP`（15µA 维持 RTC/SRAM）；
   * **⚠️ 待办（评审遗留，本版未改）**：① `U9` 输入仍取自 `VDD_1V9`（1.9V→1.8V，压差仅 100 mV ≈ 额定 dropout，PSRR 实际失效、`V_IO` 有跌破 1.76 V 风险）→ 建议改接 `VBAT`；② 1.9V 轨经 `R5/R6/R7=0Ω` 并联后总输出电容 ≈177 µF，超出 SGM6040A `COUT 3~40 µF` 推荐范围 → 需实测上电涌流或精简电容；③ `R5/R6/R7` 应改铁氧体磁珠（`BLM15PX121`）方能实现文档承诺的 π 型滤波。
2. **GNSS 受控独立 LDO：圣邦微 SGM2045-1.8 (超高 PSRR 超低压差)**
   * **芯片型号与封装**：选用 **`SGM2045-1.8XXDH4G/TR`**（XTDFN-1×1-4L 微型封装，占板仅 $1.0\times 1.0\text{mm}$）；
   * **极致射频电源品质**：
     * **92 dB 超高 PSRR (@1kHz)**：⚠️ **该指标测试条件为 `VIN = 2.8V, VOUT = 1.8V, IOUT = 20mA`**；而 V0.2 把 `VIN` 接在 1.9V 主轨（仅高于 1.8V 输出 100mV，等于额定 dropout），**LDO 实际工作在准直通区，92dB PSRR 无法兑现**。→ 改版应把 `U9` 输入改接 `VBAT`（3.0~4.2V）后再按此指标评估；
     * **9.5 µVrms 超低底噪**：提供近乎蓄电池纯净度的模拟电源；
     * **压差与裕量（须实测）**：规格书额定 dropout 为 100mV(TYP)/190mV(MAX)@300mA。当前 `VIN=1.9V`、`VOUT=1.8V` 时**压差预算只有 100mV，等于 TYP 值**；GNSS 冷启动约 100mA 峰值时 `VDD_GPS`（同时是模组 `V_IO`）可能跌破 MAX-M10S 的 `V_IO` 下限 1.76V。**必须实测 `GPS_EN` 开启瞬间的 `VDD_GPS` 跌落**；
   * **输入取电与压差发热极小化**：
     * SGM2045-1.8 输入直接接自 SGM6040A 的 **1.9V 稳定电源轨**；
     * 压差仅 $1.9\text{V}-1.8\text{V}=0.1\text{V}$，按 GNSS 典型 15.2mA、最坏 16.2mA（冷启动浪涌约 100mA）估算，稳态线性发热仅约 $\mathbf{1.6\text{ mW}}$（发热不是问题，**问题是压差裕量不足**，见上一条）；
     * 关断漏电仅 **30 nA**，通过 MCU GPIO **`P1.07 (GPS_EN)`** 控制其 EN 使能端，实现硬件级零漏电断电。
3. **逻辑电平完美自适应无倒灌**：
   * 主控 **nRF54L15** 在 1.9V 下 `V_OH ≥ 1.5V`、`V_IH = 1.33V`；GNSS 模组在 1.8V 下 `V_OH ≥ 1.35~1.4V`、`V_IH ≈ 1.17~1.22V`；两端同为低压域，串 100Ω 可直接对接，**无需电平转换**；
   * ⚠️ **反向余量偏紧**：模组 TXD → SoC RX 余量仅 20~70 mV，须示波器确认高电平；
   * ⚠️ **断电倒灌**：`GPS_EN=0` 后模组侧为 0V，若 SoC 侧 `GPS_TX` 仍输出 1.9V 会对 `RXD` 倒灌 → 固件必须先置高阻再断电。

---

---

### 2.2.1 晶振负载电容核算（X1 / X2）

* **规格书口径（`nRF54L15` Datasheet v1.0 §11.9.1 / §11.9.2）**：
  `CL_HFXO = 6 ~ 9 pF`、`CL_LFXO = 6 ~ 9 pF`。这是**电气规格（保证工作范围）**，超出后振荡器的频偏/起振时间/驱动电平均不再保证，不能等同于“典型推荐值”。
* **片内 trim 能力**：`CINT` 是**每端**电容，HFXO `4.0 ~ 17.0 pF`（0.25pF 步进）、LFXO `3 ~ 18 pF`（0.65pF 步进）。
  按 `CL ≈ C/2 + C_pcb`（`C_pcb ≈ 1~2 pF`）折算，trim 上限对应 `CL ≈ 9.5 ~ 10.5 pF` —— 与规格上限 9pF 吻合，说明**片内电容的设计覆盖就是 6~9 pF**。
* **V0.2 实选料**：`X2 = 32MHz / CL 12pF`、`X1 = FC-135R / CL 12.5pF`，均高于片内可达到的上限。
* **频偏估算**（`Δf/f ≈ (C_M/2)·[1/(C0+CL_实际) − 1/(C0+CL_标称)]`，按典型 `C_M ≈ 10fF@32MHz / 4fF@32.768kHz`、`C0 ≈ 1.2pF`）：

  | 位号 | 标称 CL | 片内可达 CL | 估算频偏 | 判定 |
  | :--- | :---: | :---: | :---: | :--- |
  | **X2（HFXO 32MHz）** | 12 pF | ≈ 9.5 pF | **约 +80 ~ +90 ppm** | ❌ 超 BLE `±40 ppm` 约 2 倍 → **必须换 `CL=8pF` 料**（或加外挂电容） |
  | **X1（LFXO 32.768kHz）** | 12.5 pF | ≈ 10 pF | **约 +30 ~ +35 ppm** | ⚠️ BLE LFXO 允许 `±500 ppm`，可容忍；但 ≈3 s/天 走时漂移 → 建议换 `CL=9pF`，或实测后按豁免记录 |

* **三条可行路径**：
  1. **换料**：X2 → `CL=8pF`、X1 → `CL=9pF`（成本最低，推荐）；
  2. **外挂电容**：`INTCAP=0`，`C1=C2`，按 `CL = C1/2 + C_pcb + Cpin(3pF)` 计算，12pF 需 `C1=C2 ≈ 16pF` —— 需在 X1/X2 增补电容位；
  3. **实测豁免**：`INTCAP` 设到最大后测实际频偏（频谱仪量 BLE 载波，或引 `CLKOUTFAST` 测频），若 X2 仍在 `±40ppm` 内方可按超规格豁免记录（需 owner + 到期日 + ticket，符合 `AGENTS.md` META-03）。

### 2.3 专项技术评估：传感器供电拓扑与 STOP 隐患排查

针对评审意见中关于传感器抗噪与 DCDC 冻结的审查，硬件团队确立如下规范：

#### 1. 传感器供电与滤波规范：
> **明确结论：传感器无需独立 LDO，严禁使用 STOP 冻结主电源！**  
> 1. **物理隔离与滤波（⚠️ 现状与设计目标不一致）**：设计目标是传感器轨由 1.9V 主轨经 **`BLM15PX121 (120Ω@100MHz)` 磁珠** + $10\mu\text{F}+0.1\mu\text{F}$ 构成 $\pi$ 型滤波；**但 V0.2 原理图实际用的是 `R5/R6/R7 = 0Ω` 跳线**，等同三条 1.9V 轨直接短接。改版须把 R7 换成磁珠后本段结论才成立，当前按"未滤波"评估底噪；
> 2. **消除 STOP 掉电隐患**：取消原设计中“拉高 STOP 冻结 DCDC”的操作。LSM6DSVTR、LIS2MDL 与 BMP388 均为内置高精度 ADC 与精密参考源的纯数字输出传感器，内部模拟部分集成专用微型稳压器与电荷泵，在 $\pi$ 型滤波后供电下底噪完全达标，硬件悬空（内部下拉）确保供电稳固；
> 3. **Sensor Hub 拓扑锁定**：LIS2MDL（磁力计）与 BMP388（气压计）挂载于 LSM6DSVTR 的 AUX I2C（Sensor Hub）总线上，主控仅通过一组 SPI 直连 IMU，多传感器融合数据打包进 4.5KB FIFO 批量突发读取，最大化主控休眠周期。

---

### 2.4 主控与 GNSS 真实物理引脚核验（V0.2 / nRF54L15）

> 完整 49 脚矩阵与双模组对照见 **[`Pinmap_Main_V0.2.md`](Pinmap_Main_V0.2.md)**（SSOT）。本节只列与固件/评审强相关的结论。

* **主控料号**：全量统一为 **`nRF54L15-QFAA`（QFN48, 6×6mm, 49 脚含中心焊盘）**；
* **GNSS 模组：`MAX-M10S-00B`（唯一默认/采购料；`Quectel LC79H` 仅为备份方案，默认不装配）**，引脚依据 `UBX-20035208` Table 10 与
  `quectel_lc79h_series_hardware_design_v1-6` §2：
  * **Pin 1 / 10 / 12 为 `GND`**（接地）；LC79H 同位置亦为 GND；
  * **Pin 2 为 `TXD`**（模组输出），经 R11 (100Ω) 连至 **主控 P1.05（Pin 6 / GPS_RX）**；
  * **Pin 3 为 `RXD`**（模组输入），经 R10 (100Ω) 连至 **主控 P1.04（Pin 5 / GPS_TX）**；
  * **Pin 4 为 `TIMEPULSE`/`1PPS`**：悬空（裁撤硬件 1PPS，依赖 UBX `NAV-PVT` 时间戳 + 本地 RTC 软时基）；
  * **Pin 5 为 `EXTINT`（MAX-M10S）/ `RESERVED`（LC79H）**：MAX-M10S 经 **R28 (100Ω)** 连 `GPS_WAKE`（**P1.09 / Pin 37**）；
    **LC79H 装配时 R28 必须 NC**；
  * **Pin 6 为 `V_BCKP`**：挂 `VDD_1V9`（1.9V 常通），维持 RTC/SRAM 热启；MAX-M10S 允许 1.65~3.6V，LC79H 允许 1.62~1.98V（**接近上限，需实测纹波**）；
  * **Pin 7 为 `V_IO`（MAX-M10S）/ `RESERVED`（LC79H）**：MAX-M10S 经 **R13 (0Ω)** 接 `VDD_GPS`（1.8V）；
    **LC79H 装配时 R13 必须 NC**；
  * **Pin 8 为 `VCC`**：由受控 LDO SGM2045-1.8 供给 1.8V（MAX-M10S 1.76~3.6V；LC79H 1.75~1.98V）；
  * **Pin 9 为 `RESET_N`**：R12 (2.2kΩ) 上拉至 `VDD_GPS` + C23 (100nF) 到地；
  * **Pin 11 为 `RF_IN`**（50Ω），来自直通路径 `C47(22pF) → C46(22pF)`；
  * **Pin 13 `LNA_EN` / Pin 14 `VCC_RF`**：本版**悬空**（无源天线，不需要偏置供电）；若改有源天线，VCC_RF 必须供电（≤50mA）；
  * **Pin 15 为 `VIO_SEL`（MAX-M10S）/ `RESERVED`（LC79H）**：**MAX-M10S 必须接 GND 才是 1.8V I/O 模式**（悬空 = 3.3V 模式，`V_IO` 要求 2.7~3.6V，与本板 1.8V 冲突）；
    **⚠️ 但 Quectel `lc79h_series_hardware_design_v1-6` Table 6 明确规定 Pin 5/7/15/19/20/24–27 为 RESERVED，"must be left floating and cannot be connected to power or GND"** ——
    V0.2 定案已把该脚改为经 **`R29 (0Ω)` 接地**（默认 MAX-M10S 贴装、启用 LC79H 时拆掉 R29），双模组跳线因此为 **3 只**（R13 / R28 / R29）+ R27 可选。
  * **Pin 16 / 17 为 `SDA` / `SCL`**：悬空（不使用模组 I2C）；
  * **Pin 18 为 `SAFEBOOT_N`（M10S）/ `WAKEUP`（LC79H）**：**R27 = NC**（默认不贴）；
* **跳线矩阵（默认 u-blox；LC79H 为可选兼容位）**：

  | 装配 | R13 (0Ω, Pin7) | R28 (100Ω, Pin5) | R27 (100Ω, Pin18) |
  | :--- | :---: | :---: | :---: |
  | **MAX-M10S（默认）** | 贴 | 贴 | NC |
  | **LC79H** | **NC** | **NC** | 需要 WAKEUP 唤醒时贴（另：Pin15 对地跳线必须 NC） |

* **模组-主控电平校验（1.9V SoC ↔ 1.8V 模组）**：
  * 模组 → SoC：MAX-M10S `V_OH ≥ V_IO−0.4 ≈ 1.4V`；LC79H `V_OH ≥ 0.75×VCC = 1.35V`；SoC `V_IH = 0.7×1.9 = 1.33V`
    → **通过，但余量仅 20~70mV**（100Ω 串阻后负载极轻，仍需示波器确认）；
  * SoC → 模组：SoC `V_OH ≥ 1.5V`；模组 `V_IH ≈ 0.65~0.68×V_IO ≈ 1.17~1.22V` → **通过，余量 ≥280mV**；
  * **默认 MAX-M10S 方案两端均为 1.8/1.9V 低压域，无需电平转换器件**（若启用备份 LC79H 的 `WAKEUP` 唤醒路径，其 `VIH(min)=1.62V` > SoC `VOH(min)=1.5V`，余量为负，见 Pinmap §5）；**GNSS 断电必须按序**：① 发 `UBX-RXM-PMREQ`（MAX-M10S）/`PAIR650`（LC79H）进软件待机 → ② 把 `GPS_TX/GPS_RX/GPS_WAKE` 置高阻 → ③ 才拉低 `GPS_EN`。
    原因：`V_IO`(Pin7) 与 `VCC`(Pin8) 同挂 `VDD_GPS`，无法实现 u-blox 要求的“`V_IO` 提前 100 ms 断电”；且 1.9V 高电平会对已断电模组的 `RXD` 倒灌。

---

### 2.5 功耗数学模型重构自洽 (依据 u-blox 官方规格书 Table 16 精准测算)
得益于全系统由 3.4V 降至 1.9V / 1.8V，结合 u-blox MAX-M10S 真实规格书数据，系统在电能利用率上获得严谨数据闭环：

* **GNSS 子系统负载与功率计算 (依据官方 Data Sheet Table 16: Typical currents for 1.8 V supply)**：
  MAX-M10S 在 1.8V 供电下包含两个供电引脚：Pin 8 ($V_{CC}$) 与 Pin 7 ($V_{IO}$)，总功耗公式为：
  $$P_{total} = V_{CC} \times I_{VCC} + V_{IO} \times I_{V\_IO} = 1.8\text{V} \times (I_{VCC} + I_{V\_IO})$$
  1. **默认三星并发连续跟踪 (GPS + GAL + BDS B1I, default)**：
     * $I_{VCC} = 13.0\text{ mA}$，$I_{V\_IO} = 2.2\text{ mA}$，总供电电流为 $15.2\text{ mA} @ 1.8\text{V}$；
     * 模组总功耗为：$1.8\text{V} \times 15.2\text{mA} = \mathbf{27.36\text{ mW}}$；
  2. **默认三星并发省电跟踪模式 (Power Save Mode / PSM)**：
     * $I_{VCC} = 6.5\text{ mA}$，$I_{V\_IO} = 2.0\text{ mA}$，总供电电流为 $8.5\text{ mA} @ 1.8\text{V}$；
     * 模组总功耗为：$1.8\text{V} \times 8.5\text{mA} = \mathbf{15.3\text{ mW}}$ *(此即行业广为引用的 15mW 超低功耗数据确切出处！)*；
  3. **四星并发全开连续跟踪 (GPS + GAL + BDS B1C + GLO)**：
     * $I_{VCC} = 14.0\text{ mA}$，$I_{V\_IO} = 2.2\text{ mA}$，总供电电流为 $16.2\text{ mA} @ 1.8\text{V}$，总功耗为 $\mathbf{29.16\text{ mW}}$；
  4. **单 GPS 连续跟踪与省电模式**：
     * 连续跟踪：$9.5\text{mA} + 2.1\text{mA} = 11.6\text{mA} @ 1.8\text{V}$（功耗 $20.88\text{ mW}$）；
     * 省电 PSM：$5.5\text{mA} + 2.0\text{mA} = 7.5\text{mA} @ 1.8\text{V}$（功耗 $13.5\text{ mW}$）。
* **电池端放电电流精准折算公式**：
  SGM2045-1.8 从 SGM6040A 1.9V 主轨取电，SGM6040A 从 3.8V 锂电池降压（效率 $\eta \approx 92\%$）：
  $$I_{bat\_gnss} = \frac{1.9\text{V} \times I_{total\_1.8V}}{3.8\text{V} \times 0.92} = \frac{I_{total\_1.8V}}{1.84} \approx 0.5435 \times I_{total\_1.8V}$$
  * **默认三星连续跟踪（直通版）**：$15.2\text{mA} / 1.84 = \mathbf{8.26\text{ mA}}$；
  * **默认三星 PSM 省电模式（直通版）**：$8.5\text{mA} / 1.84 = \mathbf{4.62\text{ mA}}$；
  * **若启用 BOM Option B 增强 LNA (ATR2652 额外 4.5mA)**：连续跟踪为 $(15.2 + 4.5) / 1.84 = \mathbf{10.71\text{ mA}}$。
* **高精度连续记录整机电池侧总电流**：
  $8.26\text{mA (GNSS直通)} + 2.8\text{mA (MCU+Flash+BLE)} + 0.4\text{mA (IMU)} \approx \mathbf{11.46\text{ mA}}$（若配增强 LNA 为 $\mathbf{13.91\text{ mA}}$）。
  在 1000mAh 电池（900mAh 有效电量）下，连续记录时长为：
  $$T_{cont} = \frac{900\text{ mAh}}{11.46\text{ mA}} \approx \mathbf{78.5\text{ 小时}} \quad (\text{增强版仍长达 } \approx \mathbf{64.7\text{ 小时}})$$！
* **深度静止待机底噪（包含电池采样恒定漏电）**：
  * 1.9V 主轨待机总静态电流：IMU 低功耗模式 (7.5µA) + MAX-M10S V_BCKP（按 15µA 保守估算；手册 28µA@3.3V，1.9V 下待实测） + nRF54L15 RTC (≈1.5µA) + Flash (1µA) = **25.0 µA @ 1.9V**（换 SoC 后须用 PPK2 重新实测）；
  * 经 SGM6040A（轻载 PFM 效率约 85%）折算至 3.8V 锂电池端：
    $$I_{bat\_active\_idle} \approx \frac{1.9\text{V} \times 25.0\mu\text{A}}{3.8\text{V} \times 0.85} \approx \mathbf{14.7\text{ }\mu\text{A}}$$
  * 电池电压采样常通电阻漏电：$4.2\text{V} / 1.33\text{M}\Omega \approx \mathbf{3.16\text{ }\mu\text{A}}$；
  * 整机电池侧总静态待机底噪：$14.7\,\mu\text{A} + 3.16\,\mu\text{A} \approx \mathbf{17.9\text{ }\mu\text{A}}$。

---

### 2.6 射频 DFT 默认焊接 BOM 与 Golden Sample 对标规范

> ⚠️ **本节部分位号基于 V0.1 原理图**（`R_INT`/`R_EXT`/`J_EXT`/`PESD0402` 在 V0.2 中不存在）。
> V0.2 实际装配以 §2.1 的 Option A/B 表与 `Hardware/BOM/README.md` 为准；Golden Sample 判定标准仍然有效。


为解决微型无源 Chip 天线在打样调试期可能面临的板级阻抗失谐与近场吸收风险，硬件在原理图与装配 BOM 中建立**可测试性设计（DFT）双通路切换机制**，并制定绝对基准比对规范：

#### 1. PCBA 装配默认焊接 BOM (Assembly BOM)
| 元件位号 | 规格与封装 | 标称参数 | 默认装配状态 (Default SMT) | 电路功能与说明 |
| :--- | :--- | :---: | :---: | :--- |
| **ANT1** | *（V0.2 已不使用该位号）* | — | **V0.2 不存在** | V0.2 GNSS 天线为 `U7`（ANT-TH_L250-W250，25×25mm 无源天线） |
| **U1**   | MAX-M10S-00B (u-blox) | 18-LCC (10.1×9.7mm) | **【默认方案】贴片焊接** | 低功耗多星定位模组（备选 Quectel LC79H；切换时 R13/R28 必须 NC） |
| **R28**  | 0603 / 0402 厚膜电阻 | **100 Ω** | **【M10S 默认装配】贴片** | 将 GPS_WAKE (**P1.09**) 连至 U1 Pin 5 (EXTINT) |
| **R27**  | 0603 / 0402 厚膜电阻 | **100 Ω** | **【默认不贴片 NC】** | 标注 `100R For LC79H`，LC79H 方案若用 WAKEUP 唤醒时贴片接 Pin 18 |
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

### 2.7 主控物理引脚矩阵与固件驱动配置说明

> **完整 49 脚矩阵（含域/时钟脚/方向/固件配置）已迁移至 [`Pinmap_Main_V0.2.md`](Pinmap_Main_V0.2.md) §2**，
> 本节只保留必须写进固件的配置约定，避免两处表格漂移。

> **⚠️ 关键电气特性与固件驱动配置说明 (Firmware Implementation Notes)**：
> 1. **5 处强制片内上拉（硬件无外部上拉电阻）**：
>    `KEY1_POI`（**P1.06 / Pin 7**）、`KEY2_MODE`（**P1.08 / Pin 9**）、`CHG_STAT`（**P0.04 / Pin 29**，SGM4056 开漏）
>    必须 `nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLUP)`；`SPI_CS`（**P2.05 / Pin 16**）与 `LSM6_SPI_CS`（**P1.14 / Pin 42**）
>    也建议配置内部上拉（12~16kΩ），防止 GPIO 高阻期间外设被误选通。
> 2. **`SPI_CS` 的上拉窗口限制**：片内上拉只有在固件初始化 GPIO 之后才生效，从"上电"到"初始化"之间
>    Flash `/CS` 处于浮空状态（W25Q512NW 要求 `/CS` 跟随 VCC）。若实测 Flash 待机电流偏高或误进入命令态，
>    改版应在 `/CS` 增加 10kΩ 外部上拉；当前版本靠固件把 `SPI_CS` 初始化提到最早期。
> 3. **按键唤醒必须用 P0/P1**：`nRF54L15` 的 **P2 域无 GPIOTE、无 SENSE/DETECT、不能唤醒**（Datasheet Table 40），
>    因此 `KEY1_POI` 已从原 `P2.03` 迁至 `P1.06`；**后续任何唤醒源都禁止落在 P2.00~P2.10**。
> 4. **同一外设不得跨端口**，且 `SCK/SCL` 必须落在 clock pin（跨域用法的官方依据与改脚方案见 [`Pinmap_Main_V0.2.md`](Pinmap_Main_V0.2.md) §1.1.1）：
>    Flash 用 `SPIM00`（P2.01/02/04/05 为专用脚）；IMU 用 **`SPIM22`**（P1 同域：`SCK=P1.03`、`MOSI=P1.02`、
>    `MISO=P1.13`、`CS=P1.14`）→ **同域，无需 Constant Latency**（V0.2 已从 P2 跨域方案迁出）。
> 5. **调试串口**：`DEBUG_TX`=**P0.00**、`DEBUG_RX`=**P0.01**，两者同属 P0(LP) 域 → 必须用 **`UARTE30`**（115200 8N1）；
>    不要误用 `UARTE00/20`（跨域，需 Constant Latency）。
> 6. **NFC 脚（Pin 3/4 = P1.02/P1.03）本版已用于 IMU SPI**（MOSI/SCK）→ 固件**必须**先释放 NFC（`&uicr { nfct-pins-as-gpios; }` 或 `NRF_NFCT->PADCONFIG=0`），否则这两根脚无法当 GPIO 用；
>    且其 pad 电容为 5pF（普通脚 1pF）。
> 7. **SAADC 通道 = AIN4（P1.11 / Pin 39）**，不是 nRF52840 时代的 AIN0(P0.02)；分压网络 1MΩ+330kΩ 与
>    735kΩ 输入电阻并联后实际比值 ≈0.186，固件必须按实测标定（详见 Pinmap §3）。
> 8. **调试排针 H1 严禁把 `VBAT` 当 VTref**：`H1.7 = VBAT`（3.0~4.2V），仅用于调试台架外部供电输入；
>    J-Link 的 VTref 必须接 1.9V（或使用固定 1.9V 电平的调试器），否则 4.2V 会经 SWDIO/SWDCLK 倒灌，
>    超过 `nRF54L15` 的 `V_I/O ≤ VDD+0.3V = 2.2V` 绝对最大额定值（Datasheet Table 94）。
>    H1 脚序：`1=GND, 2=NRESET, 3=SWDCLK, 4=SWDIO, 5=DEBUG_RX, 6=DEBUG_TX, 7=VBAT`。

---

### 2.8 nRF54L15 硬件看门狗 (Hardware WDT) 配置与自愈设计规范

为确保设备在长达数周至数月的野外、车载及日常无人值守记录中绝对不死机，系统必须在固件底层正式开启 **nRF54L15** 硬件看门狗（下列寄存器/计数须以 `nRF54L15` PS 的 WDT 章节与 NCS v3.4.2 `wdt` 驱动复核；nRF52840 时代的 `CRV=983040` 仅作参考）：

1. **时钟源与硬件寄存器规划**：
   * **时钟源基准**：由 32.768kHz LFXO 驱动计数器，独立于 128MHz CPU 主时钟；`nRF54L15` 的看门狗实例为 **`WDT30` / `WDT31`**；
   * **超时时间窗口设定 (CRV)**：
     $$\text{timeout}=\frac{\text{CRV}+1}{32768\text{Hz}}\Rightarrow \text{CRV}=30\times32768-1=\mathbf{983{,}039}$$
     30 秒超时为 Flash 4KB/64KB 扇区擦除长时等待（典型 100~400ms）及 GNSS 冷启动密集搜星预留了充裕的防误触安全裕量；
   * **低功耗运行行为配置 (`CONFIG` 寄存器)**：
     - 配置 `WDT->CONFIG = 0x01`（`SLEEP = 1, HALT = 0`）；
     - **休眠持续计数**：在 CPU 执行 WFI/WFE 进入 System ON 深度睡眠时，看门狗依然在硬件底层持续计时，杜绝休眠中挂死导致永久死机；
     - **调试断点挂起**：在连接 J-Link 单步调试停住（HALT）时看门狗自动暂停计数，防止调试被强行复位打断。
2. **多任务分层喂狗机制 (Hierarchical Watchdog)**：
   * 启用独立通道寄存器 `RR[0]`（写入固定序列 `0x6E524635` 触发重载）；
   * 在 Zephyr RTOS 主任务事件循环中建立三位心跳掩码 `wdt_task_mask`：
     - Bit 0: IMU 采样线程心跳（定期由 INT1 中断或定时器置位）；
     - Bit 1: Flash 写入与 Journaling 线程心跳；
     - Bit 2: GNSS 报文解析状态机心跳；
   * 仅当所有任务位均在周期内成功打卡时，主循环方可执行喂狗并清空掩码；任一线程卡死均将在 30 秒内超时复位。
3. **掉电前状态追踪与复位自愈 (Reset & Recovery)**：
   * 复位后系统 Boot 阶段读取 **`NRF_RESET->RESETREAS`**（`nRF54L15` 的复位原因寄存器挂在 RESET 外设，不是 nRF52840 的 `NRF_POWER`）；
   * 若检测到 **`RESETREAS.DOG0`（=WDT30）或 `DOG1`（=WDT31）** 置位，判定为看门狗复位，固件将异常标记写入 RRAM 异常追溯日志，重新初始化外设并无缝恢复连续记录（`RESETREAS` 为累积寄存器，读取后须写 1 清除）。

---

### 2.9 背面贴装传感器空间姿态重标定与 Sensor Hub 1:1 对齐解析

依据 V0.1 背面贴装图（**V0.2 版图尚未归档，贴装面与朝向需在 V0.2 PCB 定稿后复核**），传感器体系（LSM6DSVTR、LIS2MDL、BMP388）均贴装于 **PCB 背面（Bottom Layer）**。按照系统机体基准（**GPS 陶瓷天线为正面，USB Type-C 母座向下**），物理矢量与变换矩阵严格推导如下：

#### 1. 整机机体坐标系 (Body Frame: $\{X_b, Y_b, Z_b\}$)
* **$X_{\text{body}}$**：在机身正面平面内，**水平向右（Right）**；
* **$Y_{\text{body}}$**：在机身正面平面内，**垂直向上（Up / Forward，指向机头天线端）**；
* **$Z_{\text{body}}$**：**垂直于正面天线向外穿出，指向天空（Towards Sky / Front）**；
* 遵循右手定则：$\vec{X}_{\text{body}} \times \vec{Y}_{\text{body}} = \vec{Z}_{\text{body}}$。

#### 2. IMU LSM6DSVTR (U12) 背面贴装变换推导
* **贴装方式**：横向贴片于背面右上部，丝印 `LGA14`，Pin 1 白圆点位于右上角（Top-Right）；
* **芯片出厂坐标系（Figure 5）物理矢量映射**：
  * $X_{\text{IMU}}$（沿短边指向 Pin 14）：在 PCB 背面**垂直向上（Up）** $\longrightarrow +Y_{\text{body}}$；
  * $Y_{\text{IMU}}$（沿长边指向 Pin 1）：在 PCB 背面**水平向右（Right on PCB Back）**，翻转至正面看属于机身**向左** $\longrightarrow -X_{\text{body}}$；
  * $Z_{\text{IMU}}$（垂直穿出芯片正面）：指向机身**向后** $\longrightarrow -Z_{\text{body}}$；
* **变换矩阵**：
  $$\begin{bmatrix} X_{\text{body}} \\ Y_{\text{body}} \\ Z_{\text{body}} \end{bmatrix} = \mathbf{R}_{\text{IMU}} \begin{bmatrix} X_{\text{IMU}} \\ Y_{\text{IMU}} \\ Z_{\text{IMU}} \end{bmatrix} = \begin{bmatrix} 0 & -1 & 0 \\ 1 & 0 & 0 \\ 0 & 0 & -1 \end{bmatrix} \begin{bmatrix} X_{\text{IMU}} \\ Y_{\text{IMU}} \\ Z_{\text{IMU}} \end{bmatrix}$$

#### 3. 磁力计 LIS2MDL (U3) 背面贴装变换推导
* **贴装方式**：逆时针旋转 90° 贴片于背面最右上角，Pin 1 白圆点位于左下角（Bottom-Left）；
* **芯片出厂坐标系（Figure 2）物理矢量映射**：
  * $X_{\text{MAG}}$（由 Pin 1 指向 10）：在 PCB 背面**垂直向上（Up）** $\longrightarrow +Y_{\text{body}}$；
  * $Y_{\text{MAG}}$（由 Pin 1 指向 4）：在 PCB 背面**水平向右（Right on PCB Back）** $\longrightarrow -X_{\text{body}}$；
  * $Z_{\text{MAG}}$（垂直穿出芯片正面）：指向机身**向后** $\longrightarrow -Z_{\text{body}}$；
* **变换矩阵**：
  $$\begin{bmatrix} X_{\text{body}} \\ Y_{\text{body}} \\ Z_{\text{body}} \end{bmatrix} = \mathbf{R}_{\text{MAG}} \begin{bmatrix} X_{\text{MAG}} \\ Y_{\text{MAG}} \\ Z_{\text{MAG}} \end{bmatrix} = \begin{bmatrix} 0 & -1 & 0 \\ 1 & 0 & 0 \\ 0 & 0 & -1 \end{bmatrix} \begin{bmatrix} X_{\text{MAG}} \\ Y_{\text{MAG}} \\ Z_{\text{MAG}} \end{bmatrix}$$

#### 4. Sensor Hub 内部免重映射硬件优势
$$\mathbf{R}_{\text{IMU}} \equiv \mathbf{R}_{\text{MAG}} \implies \begin{bmatrix} X_{\text{IMU}} \\ Y_{\text{IMU}} \\ Z_{\text{IMU}} \end{bmatrix} \equiv \begin{bmatrix} X_{\text{MAG}} \\ Y_{\text{MAG}} \\ Z_{\text{MAG}} \end{bmatrix}$$
在 LSM6DSVTR 芯片内部 Sensor Hub 读取 LIS2MDL 数据时，两颗芯片物理敏感轴天然 1:1 同向对齐：
* `EXT_CFG_A (D4h)` 保持默认值 `0x05`（$Y=Y, Z=Z$）；
* `EXT_CFG_B (D5h)` 保持默认值 `0x02`（$X=X$）；
无需在驱动层执行繁复的轴向交换重排序，直接同向打包进 FIFO！
