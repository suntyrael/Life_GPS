# Main_V0.2 引脚映射 SSOT（nRF54L15-QFAA / QFN48）

> **适用范围**：硬件版本 `Main_V0.2`（原理图创建 2026-10-30，更新 2026-11-10），主控
> **nRF54L15-QFAA（QFN48，49 脚含中心焊盘）**。
> **权威来源**：① 原理图 `Main_V0.2`（嘉立创 EDA，图纸 P1）；② 网表
> `Netlist_Main_V0.2_2026-11-11.tel`（2026-11-11 从 V0.2 原理图重新导出，**已与原理图同步**）；③ Nordic 官方 `nRF54L15 / nRF54L10 / nRF54L05 Datasheet v1.0`
> Section 10.1.4（Table 79，引脚分配）与 Section 10.3（Figure 180 / Table 87，参考电路）。
>
> ✅ **网表已同步**：`Netlist_Main_V0.2_2026-11-11.tel` 与当前原理图一致，以下均已在网表中确认：
> ① `KEY1_POI` = **`U14.7 (P1.06)`**；② `FB1` = **120Ω@100MHz**；③ ANT 匹配 **3 串 3 并**
> （`L4 2.7nH → C13 1.5pF → L5 3.5nH → C12 2pF → L6 3.5nH → C1 0.3pF`）；
> ④ GNSS 直通 = `C47 22pF` 隔直 + **`C46 = 0Ω` 跳线**，`C24/C25/C26/C27/C28/C30/C31` 全 **NC**。

---

## 1. 软硬件接口约定（先读）

| 项 | 约定 |
| :--- | :--- |
| 逻辑电平域 | 全系统 **1.9V**（SGM6040A 常通主轨 `BT_1V9` / `VDD_1V9` / `SENSOR_1V9`）；GNSS 受控域 **1.8V**（SGM2045-1.8 → `VDD_GPS`） |
| SoC 供电 | 5 个 VDD（pin 10/22/36/47/48）全部接 `BT_1V9`，允许范围 1.7–3.6V（`VDD,POR` ≥ 1.75V） |
| GPIO 驱动能力 | 标准档 `VOH ≥ VDD−0.4V @0.5mA`、`VIH ≥ 0.7×VDD ≈ 1.33V`；高驱动档 3mA@VDD≥1.7V，5mA 档需 VDD ≥ 2.7V（本设计 1.9V **不可用**） |
| 内部上下拉 | `RPU = 12/14/16 kΩ`、`RPD = 12/14/18 kΩ`（Datasheet §11.5.1） |
| 未使用引脚 | 固件应把未用 GPIO 设为**输入并断开输入缓冲**（`PIN_CNF.INPUT=Disconnect`）以省电 |

### 1.1 GPIO 端口能力（Datasheet Table 40）——**改脚前必看**

| 端口 | 所属域 | 可唤醒 | SENSE/DETECT | GPIOTE | 超高驱动 E0/E1 | 最高速率 |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **P0**（P0.00–P0.04） | LP 低功耗域 | ✅ | ✅ | ✅（GPIOTE30） | ❌ | 8 MHz |
| **P1**（P1.00–P1.14） | PERI 外设域 | ✅ | ✅ | ✅（GPIOTE20） | ❌ | 8 MHz |
| **P2**（P2.00–P2.10） | MCU 高速域 | ❌ | ❌ | ❌ | ✅ | 64 MHz |

硬约束：
1. **P2 不能做按键、不能做中断输入**（无 GPIOTE/SENSE）——只能做输出或高速 SPI/UART；
2. **同一外设的引脚不得跨端口混用**（"Peripherals cannot mix pins from different ports"）；
3. P2 引脚跑 PERI 域外设（如 `SPIM21`）属**跨域**用法，仅在 `Constant Latency` 子功耗模式下可用，功耗不优；
4. `SCL/SCK` 类时钟信号必须落在 Datasheet Table 79 标注为 **clock pin** 的引脚上；
5. **NFC 脚（pin 3/4 = P1.02/P1.03）复位后默认是 NFC 天线模式**，当 GPIO 用必须先经
   `NFCT.PADCONFIG` 关闭 NFC；其 pad 电容为 5pF（普通脚 1pF）。

---

## 2. U14 主控引脚全表（nRF54L15-QFAA，QFN48）

| Pin | 官方引脚名（Table 79） | 本项目网络 | 域/时钟脚 | 方向 | 连接与外围 | 固件配置要点 |
| :---: | :--- | :--- | :---: | :---: | :--- | :--- |
| 1 | `P1.00 / XL1` | `LFXO_IN` | P1 / 模拟 | — | 32.768kHz 晶体 X1.1（FC-135R，**CL=9pF**） | 晶振模式；片内负载电容（`XOSC32KI.INTCAP`），无外挂电容 |
| 2 | `P1.01 / XL2` | `LFXO_OUT` | P1 / 模拟 | — | 32.768kHz 晶体 X1.2 | 同上 |
| 3 | `P1.02 / NFC1` | `NC` | P1 | — | 悬空 | 保持默认（NFC 关断后当 GPIO 亦须断开输入缓冲） |
| 4 | `P1.03 / NFC2` | `NC` | P1 / clock | — | 悬空 | 同上 |
| 5 | `P1.04 / AIN0` | `GPS_TX` | P1 / clock | OUT | `UARTE20/21 TXD` → R10 100Ω → U1.3 `RXD` | 115200 8N1；GPS 断电前先置高阻/低电平 |
| 6 | `P1.05 / AIN1`（RADIO[6]） | `GPS_RX` | P1 | IN | `UARTE20/21 RXD` ← R11 100Ω ← U1.2 `TXD` | 同域 UART，固件用 `UARTE2x` |
| 7 | `P1.06 / AIN2` | `KEY1_POI` | P1 | IN | R20 100Ω → SW7（按下接地） | **内部上拉**；SENSE 低电平唤醒（System OFF） |
| 8 | `P1.07 / AIN3` | `GPS_EN` | P1 | OUT | U9.3 `EN`（SGM2045-1.8） | **高 = 给 GNSS 供电**，低 = 断电（关断漏电 30nA） |
| 9 | `P1.08 / CLK16M` | `KEY2_MODE` | P1 / clock | IN | R21 100Ω → SW1（按下接地） | **内部上拉**；占一个 clock 脚，注意未来复用 |
| 10 | `VDD` | `BT_1V9` | PWR | — | 1.9V 主轨，就近 100nF | — |
| 11 | `P2.00` | `NC` | P2 | — | 悬空 | 断开输入缓冲 |
| 12 | `P2.01 / SPIM00.SCK` | `SPI_CLK` | P2 / clock | OUT | U8.6 `CLK`（W25Q512NW） | `SPIM00`；时钟脚必须用本脚 |
| 13 | `P2.02 / SPIM00.SDO` | `SPI_MOSI` | P2 | OUT | U8.5 `DI(IO0)` | 目标 8MHz（低噪声优先） |
| 14 | `P2.03` | `NC`（原 KEY1，已迁出） | P2 | — | 悬空 | 断开输入缓冲；**不可再用于按键/中断** |
| 15 | `P2.04 / SPIM00.SDI` | `SPI_MISO` | P2 | IN | U8.2 `DO(IO1)` | — |
| 16 | `P2.05 / SPIM00.CSN` | `SPI_CS` | P2 | OUT | U8.1 `/CS` | **内部上拉**（见 §3 注）；复位到 GPIO 初始化之间无上拉 |
| 17 | `P2.06 / SPIM00-21.SCK` | `LSM6_SPI_CLK` | P2 / clock | OUT | U12.13 `SCL/SPC` | `SPIM21` 跨域 → 需 Constant Latency 子模式；PSEL 必须用本脚 |
| 18 | `P2.07 / SWO` | `NC` | P2 | — | 悬空（可留 SWO 测试点） | 断开输入缓冲 |
| 19 | `P2.08 / SPIM00-21.SDO` | `LSM6_SPI_MOSI` | P2 | OUT | U12.14 `SDA/SDI/SDO` | 同上（跨域 PSEL 固定） |
| 20 | `P2.09 / SPIM00-21.SDI` | `LSM6_SPI_MISO` | P2 | IN | U12.1 `SDO/SA0` | 同上 |
| 21 | `P2.10 / SPIM00-21.CSN` | `LSM6_SPI_CS` | P2 | OUT | U12.12 `CS` | **建议内部上拉**；跨域 PSEL 固定 |
| 22 | `VDD` | `BT_1V9` | PWR | — | 就近 100nF | — |
| 23 | `P0.00` | `DEBUG_TX` | P0 | OUT | `UARTE30 TXD` → R19 100Ω → H1.6 | 115200 8N1 |
| 24 | `P0.01` | `DEBUG_RX` | P0 | IN | `UARTE30 RXD` ← R18 100Ω ← H1.5 | 同域 UART（P0 只有 UARTE30） |
| 25 | `SWDIO` | `SWDIO` | Debug | I/O | R1 100Ω → H1.4 | 片内上拉；SWD 无需外部上拉 |
| 26 | `SWDCLK` | `SWDCLK` | Debug | IN | R2 100Ω → H1.3 | 片内下拉 |
| 27 | `P0.02` | `LSM6_SPI_INT` | P0 | IN | U12.4 `INT1` | GPIOTE30 中断 + 运动唤醒；无上拉/下拉（IMU 推挽输出） |
| 28 | `P0.03`（GRTC PWM） | `CHG_EN` | P0 / clock | OUT | U10.4 `EN`（SGM4056，**低有效**） | **低 = 允许充电，高 = 禁止充电**；复位期间内部 200kΩ 下拉 → 默认允许充电 |
| 29 | `P0.04`（GRTC CLKOUT32K） | `CHG_STAT` | P0 / clock | IN | U10.3 `/CHG`（开漏） | **内部上拉**；低 = 正在充电 |
| 30 | `nRESET` | `NRESET` | Reset | — | C29 3.9pF→GND，R3 1kΩ→H1.2 | 片内上拉；与 Figure 180（R1=1k/C13=3.9pF）一致 |
| 31 | `ANT` | `RF_BLE` | RF | — | `2.7nH → 1.5pF → 3.5nH → 2.0pF → 3.5nH → 0.3pF` → 2.4GHz Chip 天线 L3 | 匹配网络按 Figure 180/Table 87；**接地见 §4** |
| 32 | `VSS_PA` | `GND` | PWR | — | 射频地 | **仅顶层连 pin49**，见 §4 |
| 33 | `DECRF` | `DECA/RF` | PWR | — | 与 pin43 短接；2.2µF + 10nF + 2.2nF | 经 FB1 与 DECD 隔离 |
| 34 | `XC1` | `XC1` | 模拟 | — | 32MHz 晶体 X2.3（**CL=8pF**） | 片内 `XOSC32M.CONFIG.INTCAP`，无外挂电容 |
| 35 | `XC2` | `XC2` | 模拟 | — | 32MHz 晶体 X2.1（X2.2/2.4 接地） | 同上 |
| 36 | `VDD` | `BT_1V9` | PWR | — | 就近 100nF | — |
| 37 | `P1.09`（RADIO[0]） | `GPS_WAKE` | P1 | OUT | R28 100Ω → U1.5 `EXTINT`（MAX-M10S）；R27 **NC** → U1.18 | 唤醒 GNSS 用脉冲；LC79H 装配时 R28 必须 NC |
| 38 | `P1.10` | `LED_RED` | P1 | OUT | Q3 基极（DTC143ZETL 带阻数字 NPN，内置 R1/R2） | 高电平点亮；无需外部基极电阻 |
| 39 | `P1.11 / AIN4` | `BAT_ADC` | P1 / clock | IN (SAADC) | R25 1MΩ + R26 330kΩ + C14 100nF | **AIN4（不是 AIN0）**；见 §3 注 3 |
| 40 | `P1.12 / AIN5` | `LED_BLUE` | P1 / clock | OUT | Q13 基极（DTC143ZETL） | 高电平点亮 |
| 41 | `P1.13 / AIN6` | `LED_GREEN` | P1 | OUT | Q2 基极（DTC143ZETL） | 高电平点亮 |
| 42 | `P1.14 / AIN7` | `NC` | P1 | — | 悬空 | 建议留作第二唤醒键/扩展；断开输入缓冲 |
| 43 | `DECA` | `DECA/RF` | PWR | — | 与 pin33 短接 | 必须与 DECRF 相连（Table 79 强制） |
| 44 | `VSS` | `GND` | PWR | — | 核心地 | — |
| 45 | `DECD` | `DECD` | PWR | — | C11 2.2µF→GND；L1 至 pin46；FB1 至 DECA | 与 Figure 180 一致 |
| 46 | `DCC` | `DCC` | PWR | — | L1 4.7µH（0603，≥120mA） | DC/DC 必须经 `VREGMAIN.DCDCEN` 使能；无电感会退回 LDO |
| 47 | `VDD` | `BT_1V9` | PWR | — | 就近 100nF | — |
| 48 | `VDD` | `BT_1V9` | PWR | — | 就近 100nF | — |
| 49 | `VSS`（中心焊盘） | `GND` | PWR | — | 密集地过孔 | 必须接地；VSS_PA 只连本脚 |

**未使用引脚**：3、4、11、14、18、42（全部为可选 GPIO/NFC，合规）。

---

## 3. 固件必须落实的内部上拉/极性与 ADC 配置

| 信号 | 引脚 | 硬件形态 | 固件要求 |
| :--- | :--- | :--- | :--- |
| `KEY1_POI` | P1.06 | 按键直连 GND（串 100Ω） | **内部上拉**（`PULL_UP`）+ System OFF `SENSE_LOW` 唤醒 |
| `KEY2_MODE` | P1.08 | 按键直连 GND（串 100Ω） | **内部上拉** |
| `CHG_STAT` | P0.04 | SGM4056 `/CHG` 开漏 | **内部上拉**（无外部上拉电阻） |
| `SPI_CS` | P2.05 | Flash `/CS`，无外部上拉 | **内部上拉**：上电后**第一优先级**配置，避免 `/CS` 悬空误选通；⚠️ 从"上电"到"固件配置 GPIO"之间存在**无上拉窗口**，若实测 Flash 待机电流偏高，改版加 10kΩ 外部上拉（W25Q512NW 要求 `/CS` 跟随 VCC） |
| `LSM6_SPI_CS` | P2.10 | IMU `CS`，无外部上拉 | 建议同样配置内部上拉 |
| `CHG_EN` | P0.03 | SGM4056 `EN`（**低有效**，内部 200kΩ 下拉） | 低 = 允许充电；网名"EN"不代表高有效，禁止写反 |
| `GPS_EN` | P1.07 | SGM2045 `EN`（高有效） | 高 = 供电。**断电时序（必须按此顺序）**：① 先发 `UBX-RXM-PMREQ`（MAX-M10S）或 `PAIR650`（LC79H）让模组进入软件待机 → ② 把 `GPS_TX/GPS_RX/GPS_WAKE` 置为高阻输入（或低电平）→ ③ 再拉低 `GPS_EN`。原因：`V_IO`(Pin7) 与 `VCC`(Pin8) 同挂在 `VDD_GPS`，无法满足 u-blox 要求的「`V_IO` 比 `VCC` 提前 100 ms 断电」，只能走软件待机路径；同时避免 1.9V 高电平对已断电模组的 `RXD` 倒灌 |
| `BAT_ADC` | P1.11 / AIN4 | 1MΩ+330kΩ 常通分压 + 100nF | ① 通道必须选 **AIN4**；② 采集时间取长（≥10–40µs），单次触发后关闭 SAADC；③ **必须做负载修正**：SAADC 输入电阻典型 735kΩ（Datasheet §11.17.1）会与 R26 并联，使分压比由 0.248 降到 ≈0.186（**读数偏低约 25%**），固件按实测标定或使用 `R26∥R_IN` 修正公式 |

---

## 4. 射频/电源布局硬约束（来自 Figure 180 注记，网表无法表达）

1. `VSS_PA`（pin 32）**只能在 IC 正下方顶层连到 pin 49**，不得在芯片外围直接接全局地平面。
2. ANT 匹配第一级并联电容（1.5pF）的接地端**只在顶层接 pin 32**；第二级并联电容（2.0pF）的接地
   必须与除**底层地**以外的所有地层隔离（挖空），利用过孔电感形成谐波陷波。
3. 匹配网络紧贴 IC；DCC/DECD 的 L1、C11 与 DECA/DECRF 的 FB1、三颗去耦电容全部就近。
4. 每颗 VDD 脚（10/22/36/47/48）独立 100nF；`VDD` bulk 电容靠近入口。
5. 晶振走线极短、对称、包地，下方禁布；器件下方保持完整地平面。
6. SWD/调试串阻靠 MCU；产测点置底层可顶针。

---

## 5. GNSS 模组引脚对照（U1，18-pin LCC；**默认 u-blox MAX-M10S**，LC79H 为可选兼容位）

| Pin | MAX-M10S（**默认装配**） | LC79H（可选兼容位，默认不装配） | 本项目网络 | 装配/固件要点 |
| :---: | :--- | :--- | :--- | :--- |
| 1 | `GND` | `GND` | `GND` | — |
| 2 | `TXD`（PIO1） | `TXD` | `GPS_RX`（→ U14.6） | 模组输出，100Ω 串阻 |
| 3 | `RXD`（PIO0） | `RXD` | `GPS_TX`（← U14.5） | 模组输入，100Ω 串阻 |
| 4 | `TIMEPULSE`（PIO4） | `1PPS` | `NC` | 裁撤 1PPS，靠 UBX/PAIR 时间戳 |
| 5 | `EXTINT`（PIO5） | **`RESERVED`** | R28 100Ω → `GPS_WAKE` | **MAX-M10S 贴 R28；LC79H 必须 NC** |
| 6 | `V_BCKP` | `V_BCKP` | `VDD_1V9`（1.9V） | 后备域：MAX-M10S 1.65–3.6V；LC79H 1.62–1.98V（1.9V 在范围内但余量小） |
| 7 | `V_IO` | **`RESERVED`** | R13 0Ω → `VDD_GPS`（1.8V） | **MAX-M10S 贴 R13；LC79H 必须 NC** |
| 8 | `VCC` | `VCC` | `VDD_GPS`（1.8V，U9 输出） | MAX-M10S 1.76–3.6V；LC79H 1.75–1.98V |
| 9 | `RESET_N` | `RESET_N` | R12 2.2kΩ 上拉 + C23 100nF | 低有效，≥1ms |
| 10 | `GND` | `GND` | `GND` | — |
| 11 | `RF_IN` | `RF_IN` | 射频前端 | 50Ω |
| 12 | `GND` | `GND` | `GND` | — |
| 13 | `LNA_EN` | `ANT_ON` | `NC` | 无源天线方案不需要 |
| 14 | `VCC_RF` | `VDD_RF` | `NC` | **无源天线：保持悬空**（若改有源天线必须供电，≤50mA） |
| 15 | `VIO_SEL` | **`RESERVED`** | 当前直接接 `GND` | **MAX-M10S：必须接 GND 才是 1.8V I/O 模式**（悬空 = 3.3V 模式，`V_IO` 需 2.7–3.6V）。**⚠️ LC79H：Quectel 手册 Table 6 明确 Pin 5/7/15/19/20/24–27 为 RESERVED，`must be left floating and cannot be connected to power or GND`** → 当前硬接 GND 使 LC79H 无法直贴。**改版必须把该脚改为经 0Ω 跳线（或焊盘桥接）到 GND：MAX-M10S 贴、LC79H 不贴** |
| 16 | `SDA`（PIO2） | `I2C_SDA` | `NC` | 不用 I2C |
| 17 | `SCL`（PIO3） | `I2C_SCL` | `NC` | 不用 I2C |
| 18 | `SAFEBOOT_N` | `WAKEUP` | R27 **NC** → `GPS_WAKE` | 默认不贴；safeboot 悬空即可（内部 1kΩ 串阻） |

**装配矩阵（默认 u-blox）**

| 装配 | R13（0Ω, Pin7） | R27（100Ω, Pin18） | R28（100Ω, Pin5） | Pin15 对地跳线（**待增补**） | 说明 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **MAX-M10S（默认）** | 贴 | NC | 贴 | **贴**（VIO_SEL=0 → 1.8V I/O） | Pin7 = V_IO 需 1.8V；Pin5 = EXTINT 唤醒；固件跑 UBX |
| **LC79H（可选，需改板）** | **NC** | 需要 WAKEUP 唤醒时贴 | **NC** | **NC（必须悬空）** | Pin5/Pin7/Pin15 均为 RESERVED，Quectel 手册禁止接电源或地；**现有硬件 Pin15 硬接 GND，不增补跳线则此项无法执行** |

**电平校验（1.9V SoC ↔ 1.8V 模组）**

| 方向 | 门限 | 结论 |
| :--- | :--- | :--- |
| 模组 TXD → SoC | MAX-M10S `VOH ≥ V_IO−0.4 = 1.40V`；LC79H `VOH ≥ 0.75×VCC = 1.35V`；SoC `VIH = 0.7×1.9 = 1.33V` | 通过，余量 **20 mV（LC79H）/ 70 mV（M10S）** → 100Ω 串阻后负载极轻，须实测波形确认 |
| SoC TX → 模组 RXD | SoC `VOH,SD ≥ 1.9−0.4 = 1.5V`；MAX-M10S `VIH = 0.68×V_IO = 1.224V` → 余量 **276mV**；LC79H `VIH = 0.65×VCC = 1.17V` → 余量 **330mV** | 通过 |
| SoC → LC79H `WAKEUP`（Pin18，经 R27） | LC79H `VIH(min) = 1.62V`；SoC `VOH,SD(min) = 1.5V` | ❌ **最坏情况 −120mV，无规格保证** → 需实测；若启用该路径应加 1.8V 上拉或电平转换 |

---

## 6. 待闭环项（截至本文档版本未在原理图/网表闭环）

| # | 事项 | 影响 |
| :---: | :--- | :--- |
| 1 | 网表未按最新原理图重新导出 | 文档与机器可读源不一致，CI/BOM 无法自动核对 |
| 2 | ✅ 已闭环：V0.2 网表已带型号（`U14=NRF54L15-QFAA-R`、`U9=SGM2045-1.80`、`U8=W25Q512NWEIQ`、`U7=BWGNSCNX25-25W4`、`L3=ZC-SL06`、`Q2/Q3/Q13=DTC143ZETL`） | 仍需在送厂 BOM 里逐行固化 |
| 3 | U9（SGM2045-1.8）输入仍为 `VDD_1V9`（1.9V） | 压差仅 100mV ≈ 额定 dropout，PSRR 失效、`V_IO` 可能跌破 1.76V；建议改接 VBAT |
| 4 | `R5/R6/R7 = 0Ω` 分离 1.9V 子域 | 与"磁珠 π 型滤波"设计意图不符；且 1.9V 轨总电容 ≈177µF，超出 SGM6040A `COUT 3–40µF` 推荐范围 |
| 5 | Flash `/WP`(3)、`/HOLD`(7) 直连 1.9V | `-IQ` 料号 QE=1 时二者为 IO2/IO3，建议改 10kΩ 上拉 |
| 6 | ✅ 已闭环：ANT 匹配为 3 串 3 并（L4 2.7nH / C13 1.5pF / L5 3.5nH / C12 2pF / L6 3.5nH / C1 0.3pF），与 Figure 180 一致 | 仅需 VNA 实测调谐与谐波 |
| 7 | GNSS 前端 DNP 残桩 | 天线节点至 U5/Q1 分支需 ≤5mm，或串 DNP 0Ω 断开 |
| 11 | ⚠️ **晶振 CL 超规格（must-fix）** | `X1 = FC-135R，CL 12.5pF`、`X2 = 32MHz，CL 12pF`；而规格书 `CL_LFXO / CL_HFXO` 均要求 **6–9pF**（p.902）。本板**无外挂负载电容**、全靠片内 INTCAP → 必须换成 **X1 CL=9pF / X2 CL=8pF** 型号，或在 X1/X2 增加 DNP 外挂电容位（用外挂时 `INTCAP=0` 且两电容相等） |
| 12 | `C46 = 0Ω` 跳线（不是电容） | 直通路径 = `U7 → C47 22pF → C46 0Ω → RF_IN`；Option B 时 C46 必须 NC，否则会把 LNA 输出直接短到天线端 |
| 8 | 天线 ESD / π 调谐位 | 无源天线馈电端缺超低结容 ESD 与并联调谐位 |
| 9 | **`U1.15 (VIO_SEL)` 直接硬接 GND** | **对默认料 u-blox MAX-M10S 正确**（VIO_SEL=0 → 1.8V I/O）；但 LC79H 明令该脚必须悬空 → **不改 0Ω 跳线则该"可选兼容位"不可用**（默认装配不受影响） |
| 10 | LC79H 底部 10 个 LGA 焊盘（Pin 19–28） | 现有 18-LCC 焊盘无对应 pad；需确认顶部区域无裸露过孔/铜皮导致误短路，且 RESERVED 焊盘不得铺铜 |
