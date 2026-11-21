# M1 固件 bring-up 说明（Main_V0.2 / nRF54L15-QFAA）

> 状态：**M1 骨架已落地，GitHub Actions 编译 + 单测通过**。本地零编译（见 `AGENTS.md` §2），
> 所有构建只在 `.github/workflows/firmware.yml` 中执行。

首次通过的实测记录（commit `3ef1f2b4`，workflow run 44132071171，NCS v3.4.2 官方 toolchain 容器）：

| 项 | 结果 |
| :--- | :--- |
| 固件镜像 `lifegps/nrf54l15/cpuapp` | FLASH 96,428 B / 729,088 B（6.18% of 1524 KB），RAM 15,765 B / 262,144 B（6.01%） |
| twister native_sim | 6/6 套件通过 |
| 协议契约 | `validate.py` PASS，`gen_c.py --check` 无漂移 |

最近一次复测（commit `c1f6acc`，run 44872866305，2026-11-21，含 LSM6DSV16X 驱动 + SFLP）：

| 项 | 结果 |
| :--- | :--- |
| 固件镜像 | FLASH 267,384 B / 729,088 B（36.7%），RAM 61,457 B / 262,144 B（23.4%） |
| twister native_sim | 12/12 套件通过（新增 `lifegps.sflp`） |
| app / docs workflow | success |

## 1. M1 覆盖范围

| 模块 | 文件 | M1 行为 |
| :--- | :--- | :--- |
| 串口诊断 | `src/diag/diag.c` | 上电横幅：复位原因（hwinfo）、协议版本、总线/引脚摘要、晶振偏差告警；日志 = `时间戳 级别 模块: 内容` |
| 板级 IO | `src/board/board_io.c` | CS 先置高（Flash P2.05 / IMU P1.14，均无外部上拉）、GPS_EN 低、CHG_EN 低（允许充电）、CHG_STAT 上拉输入、未用脚 P2.00/03/06/08/09/10 断开输入缓冲 |
| LED 灯语 | `src/power/led.c` | README §9.1 全部图案（开机红→蓝→绿 3 次、关机绿→蓝→红 3 次、定位双闪绿、BLE 单闪蓝、同步快闪蓝、高精蓝绿交替、PDR 黄闪、低电快闪红） |
| 按键 | `src/power/keys.c` | 10 ms 轮询 + 100 ms 软件消抖（板上无 RC，`LGPS_KEY_DEBOUNCE_MS`）、8 s 长按、短按事件；`gpio_pin_get_raw()` 采样物理电平 |
| 电源状态机 | `src/power/power_sm.c` | 长按 8 s → 关机灯语 → `sys_poweroff()`；KEY1 短按 = POI 打点请求；KEY2 短按 = 高精模式标志 |
| 看门狗 | `src/power/wdt.c` | WDT30 30 s，分层喂狗（主循环线程 + GNSS 线程，每个 5 s 窗口各签到至少一次才喂；窗口结束即清掩码） |
| 电池 | `src/power/battery.c` | SAADC AIN4（P1.11）；两段标定：分压实测 0.2861（`LGPS_BATT_CAL_PACK_UV/TAP_UV`）+ ADC 通道 +4.18 % 标度误差（`LGPS_BATT_ADC_GAIN_PPM`），2.5–4.4 V 误差 ≤ ±2 mV；**不输出百分比**（放电曲线未实测） |
| GNSS | `src/gnss/gnss.c`, `src/gnss/ubx.c` | UARTE20 38400（M10 出厂默认，与设备树 `current-speed` 一致）、**UBX-CFG-VALSET 打开 NAV-PVT（key `0x20910007`，RAM 层）+ CFG-VALGET 回读确认**、解析 NAV-PVT、冷启动 60 s → 首定 → 星历保持 35 s → PMREQ 备份 + 断电 → 退避 120/300/900 s；ACK-NAK 以 WRN 打印（class/id 取自 ACK 载荷） |
| IMU | `src/imu/lsm6dsv16x.c`, `src/imu/sflp.c` | LSM6DSV16X（= LSM6DSVTR）软复位、WHO_AM_I=0x70 校验、±4 g/±1000 dps @30 Hz 高性能（数据就绪位驱动采样）、INT1=DRDY；sensor hub 探测 LIS2MDL/BMP388 并配置 14 字节串流；SFLP 四元数 + 重力经 FIFO 读入（`CONFIG_LGPS_IMU_SFLP`） |
| 导航数学 | `src/nav/nav.c` | 机体坐标旋转、ZUPT 双判据（σ²<0.005 g²、‖ω‖<0.08 rad/s、持续 10 s）、Weinberg 步长、磁异常判据 |
| 存储 | `src/storage/flash_ring.c` | W25Q512 暂存环形：16×4 KB 扇区、16 B 扇头（magic/seq/erase_count/commit_flags/CRC16）、127 条 32 B 记录/扇区、写满轮转 + 擦除 |
| 记录编解码 | `src/storage/traj.c` | 由 `protocol/lifegps_protocol.yaml` 生成的结构体 + 逐字节小端序列化 + CRC-8 |
| 协议生成 | `tools/protocol-gen/gen_c.py` | YAML → `Firmware/include/generated/lifegps_protocol.h`；`--check` 供 CI 防漂移 |
| 单测 | `Firmware/tests/lifegps.*` | CRC/时间、UBX、导航数学、记录编解码、扇区格式（`lifegps.ring`）、生成头契约（native_sim + ztest） |

**不在 M1**：BLE/GATT、MCUboot OTA、heatshrink 归档、CRACEN 加密、气压高程解算、芯片 pedometer / wake-up 中断。
（SFLP 硬件姿态已在 M1 落地，见 §2.1。）

## 2. 硬件事实（每条都有出处）

| 事实 | 值 | 出处 |
| :--- | :--- | :--- |
| 调试串口 | UARTE30，P0.00 TX / P0.01 RX，115200 8N1 | `Doc/Hardware/Pinmap_Main_V0.2.md` §2；`sdk-zephyr` `dts/vendor/nordic/nrf54l_05_10_15.dtsi`（`uart30@104000`） |
| GNSS 串口 | UARTE20，P1.04 TX / P1.05 RX | 同上 |
| IMU SPI | SPIM22，SCK P1.03 / MOSI P1.02 / MISO P1.13 / CS P1.14（同域，无需 Constant Latency） | 同上 + pinmap §1.1.2 方案 B |
| Flash SPI | SPIM00，SCK P2.01 / MOSI P2.02 / MISO P2.04 / CS P2.05，8 MHz | pinmap §2 |
| 电池采样 | P1.11 = AIN4；网表标称 R25 = 1 MΩ / R26 = 330 kΩ（理论 0.2481），**实板实测 0.2861 ≈ R25 820 kΩ**；735 kΩ 是 SAADC 等效输入阻抗、DC 不加载 | pinmap §2/§3；`Firmware/include/lifegps/battery.h` |
| NFC 脚释放 | `&uicr { nfct-pins-as-gpios; }` | `boards/nordic/nrf54l15tag/*.dts:26`（同版本 Zephyr 4.4.2） |
| RRAM 分区 | boot 62 KB / slot0 712 KB / slot1 712 KB / storage 36 KB | `dts/vendor/nordic/nrf54l15_cpuapp_partition.dtsi` |
| Flash 深度掉电 | tDP = 3 µs、tRES1 = 30 µs | `Doc/Hardware/Datasheets/Winbond_07022021_W25Q512NW-2451091.pdf` AC characteristics |

### 2.1 六轴传感器按原始设计回到 **LSM6DSVTR**（= LSM6DSV16X 晶圆）

上一轮按装配说明把 `U12` 当作 **LSM6DSRTR** 写了一版驱动（ST DS11976 Rev 2）。本轮按你的说明
回到**原始设计**：网表 `Hardware/Schematic/Netlist_Main_V0.2_2026-11-11.tel:50` 写的是
**LSM6DSVTR**，其晶圆就是 **LSM6DSV16X**，手册为 `Doc/Hardware/Datasheets/LSM6DSV16X_Datasheet.pdf`
（ST **DS13510 Rev 4**）。两者同为 LGA-14L、引脚定义一致（2/3 = SCx/SDx 传感器中枢 I²C，13/14 = 主 SPI，
1 = SDO/SA0，4 = INT1，12 = CS），**PCB、引脚、上拉都不动**；改动全部来自寄存器映射：

| 项 | LSM6DSR（上一版 DS11976） | **LSM6DSV16X（现行 DS13510 Rev 4）** |
| :--- | :--- | :--- |
| WHO_AM_I (0x0F) | 0x6B | **0x70**（§9.13） |
| SPI 地址帧 | bit7 = R/W，读 `0x80 \| reg` | **相同**：图 10 是 `RW AD6..AD0` MSB 先发（§5.1.3 的「bit 0」按传输顺序编号，不是字节位号，误读会把寄存器地址减半）；模式 0/3 均允许（§5.1） |
| ODR 档位 | 12.5 / 26 / 52 / 104 / 208 Hz… | **1.875 / 7.5 / 15 / 30 / 60 / 120 / 240 / 480 Hz…**（Table 52 / Table 55），无 26 Hz |
| 加速度 FS | `CTRL1_XL(0x10)[3:2]` | **`CTRL8(0x17) FS_XL[1:0]`**（Table 68），`CTRL1` 只剩 OP_MODE + ODR |
| 陀螺 FS | `CTRL2_G(0x11)[3:2]` | **`CTRL6(0x15) FS_G[3:0]`**（Table 63），`CTRL2` 只剩 OP_MODE + ODR |
| 灵敏度 | ±4 g: 0.122 mg/LSB；±1000 dps: 35 mdps/LSB | **相同**（§4.2 Table 2 `LA_So` / `G_So`） |
| 温度 | 256 LSB/°C、+25 °C 零点 | **相同**（§4.3 Table 6 `TSen` 与注 3） |
| Sensor hub 输出 | `SENSOR_HUB_1..18` | 同（§17 Table 439），仍是最多 7 字节/slave |
| Hub 关键差异 | `SHUB_ODR` 在 `MASTER_CONFIG`；`SLV1_CONFIG` 直接写 numop；状态在主页 `0x39`；辅助上拉在 `MASTER_CONFIG` bit3 | **`SHUB_ODR` 挪到 `SLV0_CONFIG[7:5]`（Table 483）；`SLV1_CONFIG` bit4 必须写 1（Table 488 注 2）；状态在主页 `STATUS_MASTER_MAINPAGE(0x48)`（Table 136）；辅助上拉在 `IF_CFG(0x03).SHUB_PU_EN`（Table 30）** |
| SFLP 硬件姿态 | **无** | **有**（§2.8）：`EMB_FUNC_EN_A(0x04).SFLP_GAME_EN` + `SFLP_ODR(0x5E)`；输出只在 FIFO（tag `0x13` 四元数 / `0x17` 重力 / `0x16` 陀螺零偏，Table 218） |
| 计步 | 需固件过零检测 | 芯片内置 pedometer / step detector / step counter（本轮**未启用**，见下） |

**采样率与 ZUPT 窗口**：`CTRL1/CTRL2` 的 ODR 写 **0100b = 30 Hz**（Table 52/55），ZUPT 窗口取
**30 点 = 1.000 s**，README §7.5 的方差门限（0.005 g² / 0.08 rad/s）仍按 1.0 s 窗口成立。
节拍照旧由 `STATUS_REG(0x1E)` 的 `XLDA|GDA` 驱动（主循环 10 ms 轮询、无睡眠漂移）。
随之换算的还有：标定面 60 点 = 2.000 s、静止窗 15 点 = 0.5 s、步态低通 α 由 0.45@26 Hz
按同一转角换算为 **0.50@30 Hz**（`1 − (1 − 0.45)^(30/26)`）。

**SFLP（本轮新增的能力）**：

1. 使能：`FUNC_CFG_ACCESS(0x01)` bit7 进 embedded functions bank → `EMB_FUNC_EN_A` bit1
   （`SFLP_GAME_EN`）、`EMB_FUNC_FIFO_EN_A(0x44)` bit1|bit4（四元数 + 重力入 FIFO）、
   `SFLP_ODR` 的 `SFLP_GAME_ODR[5:3] = 001b`（30 Hz，与采样率一致）；`FIFO_CTRL4` 置连续模式
   （`110b`，满了覆盖最旧，读端卡住只丢样本、不堵设备）。这套速率组合与 ST 官方示例
   `lsm6dsv16x_STdC/examples/lsm6dsv16x_sensor_fusion.c` 一致（XL/G 30 Hz + SFLP 30 Hz）。
2. 读取：`FIFO_STATUS1/2` 的 `DIFF_FIFO[8:0]` → 每次 7 字节（tag + 6 字节）从 `0x78` 读出，
   解码在 `Firmware/include/lifegps/sflp.h`（纯函数，`Firmware/tests/lifegps.sflp` 单测）。
3. 编码：四元数 = 3 个 IEEE 754 binary16 (x, y, z)，`w = sqrt(1 − x² − y² − z²)`；重力 = int16 ×
   0.061 mg/LSB。两条都按 ST 官方 LSM6DSV16X 驱动/示例（`lsm6dsv16x_sensor_fusion.c` 的 `sflp2q()`、
   `lsm6dsv16x_from_sflp_to_mg()`）核对，因为 DS13510 只写"四元数存在 FIFO 里"，没写编码。
4. 用途：**重力向量**参与 BLE Status 帧的 `gravity_mg` / `linear_mg` 拆分（比固件积分估计更稳）；
   **四元数**在 bring-up 打一行日志留证，**不替换** BLE 姿态四元数——SFLP 只融加速度计 + 陀螺仪，
   yaw 没有世界基准，而 BLE 契约（`Doc/Software/BLE_Wire_v1.md` §4）要求世界对齐四元数。
5. 开关：`CONFIG_LGPS_IMU_SFLP`（默认 y）。FIFO 里一直没出现 SFLP tag 时驱动只告警一次，
   固件姿态滤波器照常工作。

**本轮仍未启用**：芯片 pedometer / wake-up 中断 / FSM / MLC 等，取舍与后续计划见 §2.2。

### 2.2 IMU 功能取舍（2026-11-21 决定）

按产品需求把 LSM6DSV16X 的能力过了一遍。三类结论，理由写在表里，避免后续会话重复提。

**不需要（不做，也不实现）**

| 能力 | 不做的理由 |
| :--- | :--- |
| Qvar / analog hub（`CTRL7(0x16).AH_QVAR_EN`、`AH_QVAR_OUT`） | 无触摸/手势需求；Qvar 要接金属感应面才有效，本机外壳没有 |
| OIS / EIS 通道（辅助 SPI、`UI_CTRL*_OIS`、FIFO 的 EIS 通道） | 无摄像头，OIS/EIS 只服务成像防抖 |
| Sensor hub `PASS_THROUGH_MODE`（`MASTER_CONFIG` bit4） | 主接口是 SPI；aux I2C 的调试由 `lgps_imu_hub_diag()` 覆盖，不需要把 aux 总线透传到主控 |
| pedometer / step detector / step counter（`EMB_FUNC_EN_A.PEDO_EN`、`STEP_COUNTER_L/H(0x62/63)`） | 无步数统计需求；PDR 的步数来自固件过零检测，Weinberg 步长需要每步的峰谷差（`delta_a`），芯片只给累计计数 |
| free-fall / tap / double-tap / 6D-4D / tilt（`WAKE_UP_THS.SINGLE_DOUBLE_TAP`、`FREE_FALL(0x5D)`、`TAP_*`、`D6D_SRC(0x47)`、`EMB_FUNC_EN_A.TILT_EN`） | 与交互无关：打点已有 KEY1，姿态与倾角由固件姿态滤波器给出；跌落检测只在保修取证时才有意义 |
| FSM / MLC（第 14/15 章的 page 编程、`FSM_ENABLE(0x46)`、`MLC1..4_SRC(0x70..0x73)`） | 目的是把判断逻辑搬进传感器、让 MCU 睡；本项目 PDR 要求 MCU 常驻 30 Hz，收益不足 |
| 自检 `ST_XL/ST_G`（`CTRL10(0x19)`） | 只在产测/RMA 有用，留到 F11 产测决定；App 的 `SELFTEST(0x40)` 目前仍回 `NOT_SUPPORTED` |

**为什么 PDR 不搬进 FSM/MLC**

PDR 需要四个量，缺一不可：姿态/航向（四元数，且要用地磁与 GNSS 航迹角对齐）、每一步的峰谷差
`delta_a`、步长（Weinberg `L = K * delta_a^0.25`，K 还要用 GNSS 在线标定并跨掉电保存）、以及把
它们积分成 x/y 并写进轨迹。FSM/MLC 的指令集是整数阈值、计数器和决策树：没有浮点、没有四元数、
拿不到 GNSS 航迹角、也没有位置与存储；SFLP 能给的姿态没有磁力计 yaw，pedometer 只给累计步数
（没有 `delta_a`）。所以"用 FSM/MLC 做 PDR"不是省电优化，而是做不到——`Firmware/src/nav/nav.c`
与 `attitude.c` 实现的正是这四步。

能搬进传感器的是**"要不要叫醒 MCU"** 这个判断：静止 → 唤醒门限（`WAKE_UP_THS`/`WAKE_UP_DUR`）
+ 连续越阈去抖（`INACT_DUR`），这就是下面的 M2 项。若要更聪明的"走 / 不走 / 甩动"分类，MLC 的
活动识别才是它真正的位置，但它省的是唤醒次数，不是 PDR 运算。

反过来，如果以后要在手机上显示**步数统计**，芯片 pedometer（`PEDO_EN` + `STEP_COUNTER_L/H`）
是最省的一条路（3 个寄存器、零 MCU 运算），到那时再开；它替代不了 PDR 要的 `delta_a`。
真要"把运算搬出 M33"，本板对应的硬件是 FLPR（RISC-V 协处理器），不是 FSM/MLC；当前
`Kconfig.lifegps` 明确写了本版未使用 FLPR，属于另一次架构决定。

**要做：长时间静止后的唤醒（M2）**

1. 阈值：`WAKE_UP_THS(0x5B)` 的 `WK_THS[5:0]`；1 LSB 的重量由 `INACTIVITY_DUR(0x54)` 的
   `WU_INACT_THS_W[2:0]` 选（默认 7.8125 mg/LSB，最大 250 mg/LSB）。
2. 持续时间：`WAKE_UP_DUR(0x5C)` 的 `WAKE_DUR[1:0]`，1 LSB = 1/ODR_XL。
3. 防误唤醒：`INACTIVITY_DUR.INACT_DUR[1:0]` 要求 2~4 次连续越阈才算"活动"（硬件去抖），
   比再启用 sig-motion 更省事；sig-motion 只在实测误唤醒仍偏高时作为第二道门。
4. 使能与路由：`FUNCTIONS_ENABLE(0x50)` bit7 `INTERRUPTS_ENABLE=1`，`MD1_CFG(0x5E)` bit5
   `INT1_WU=1`；唤醒后读 `WAKE_UP_SRC(0x45)` 的 `WU_IA`（bit3）清标志。
5. 静止降功耗：`FUNCTIONS_ENABLE.INACT_EN[1:0]=10b` 让加速度计在静止后自动降到
   `INACTIVITY_DUR.XL_INACT_ODR[1:0]`（1.875/15/30/60 Hz）、陀螺进 sleep；唤醒后回 30 Hz 高性能。
6. MCU 侧：INT1 = P0.02 走 GPIOTE 中断（dts 里已声明 `imu-int1-gpios`，**目前完全没用**，
   采样靠轮询 `STATUS_REG`）；唤醒后重开 ZUPT 窗口并喂狗。
7. 初始参数（两个场景共用，台架再调）：

   | 项 | 初始值 | 依据 |
   | :--- | :--- | :--- |
   | `INACTIVITY_DUR.WU_INACT_THS_W[2:0]` | `001` = 15.625 mg/LSB | Table 153；比默认 7.8125 mg/LSB 留出量程（63 LSB = 0.98 g），又比 62.5 mg/LSB 分辨率好 |
   | `WAKE_UP_THS.WK_THS[5:0]` | `16` → **0.25 g** | 正常徒步躯干垂直峰值约 0.2–0.5 g（1.2–1.5 g 绝对），0.25 g 落在下沿，能抓住休息后的第一步；静止噪声 <5 mg，余量 50 倍 |
   | `WAKE_UP_DUR.WAKE_DUR[1:0]` | `01` = 1/ODR_XL | 滤掉单点毛刺，又不在 15 Hz 睡眠态引入 100 ms 以上的延迟 |
   | 睡眠态 `CTRL1` | 15 Hz + LPM1 | 1.875 Hz 时一步只有 1 个样本，峰值不可靠；60 Hz LPM1 是 17 µA 的可选升级（Table 4：LPM1 17 µA@60 Hz、4.0 µA@1.875 Hz） |
   | 睡眠态 `CTRL2` | 陀螺 power-down | `IddPD` 2.6 µA（Table 4），走起来再开陀螺（`Ton` 30 ms） |
   | 固件确认门 | 3 s 内 ≥2 步才留在活动态；PDR 解冻要 ≥3 步 / 5 s | 单次颠簸最多凑出 1 个假步（`lgps_step_update` 要求峰→谷→过零的完整周期，单极性冲击不计步） |
   | 回睡 | ZUPT 锁定 ≥10 s 后降 ODR | 不用 `INACT_EN` 的硬件自动降功耗：`SLEEP_DUR` 粒度是 512/ODR（15 Hz 时 34 s），而且它会在固件不知情时改采样率，ZUPT 窗口会跟着变 |

8. 两个场景的差别不在阈值，在确认逻辑：

   | 场景 | 现象 | 处置 |
   | :--- | :--- | :--- |
   | 徒步（走一段→休息→再走） | 休息时陀螺/加速度计都低，0.25 g 门限抓住第一步 | 醒了就 2 步内确认，误唤醒次数 ≈ 真实起步次数 |
   | 自驾 / 火车 / 飞机 | 路面颠簸、轨道接缝、气流颠簸都是 0.1–0.5 g 的 1–20 Hz 瞬态，**与步行幅值重叠，阈值分不开**；火车加减速、飞机起降的持续加速度（>1 s 缓变）被 wake-up 的高通路径压掉 | 传感器只负责"有动静"；靠上面的确认门把影响压到"一次误唤醒 + 3 s 后回睡"。实测仍偏多再把 `WK_THS` 提到 24–26（0.35–0.40 g），代价是慢起步多走 1–2 步 |

9. 唤醒后前 1 s 不要用 SFLP 的 yaw：算法标定时间 0.8 s、姿态稳定 0.7 s（Table 1），建议先写
   `EMB_FUNC_INIT_A(0x66).SFLP_GAME_INIT=1` 重初始化，这 1 s 内航向沿用上一段的估计 + 磁力计校正。

10. 验收（先测后定）：徒步起步 2 步内醒；桌面静置 1 h 误唤醒 ≤3 次；车/火车/飞机各 1 h 的误唤醒
    次数先记录再定门限。日志里加 `WAKE_UP_SRC(0x45)` 的 `WU_IA`（bit3）与 `x/y/z_wu`（bit2..0）统计，
    哪个轴在触发直接指向是垂直颠簸还是水平加减速。

**待评估：FIFO 批量（低功耗第二阶段）**

- 目的：把"每 30 Hz 一次 SPI 事务"换成"每 1~5 s 一次突发读"，静止时段 MCU 才能真正睡下去。
  寄存器：`FIFO_CTRL3(0x09)` 的 `BDR_XL/BDR_GY`、`FIFO_CTRL1(0x07)` 水位、`FIFO_CTRL4(0x0A)` 模式、
  `INT1_CTRL(0x0D)` 的 `INT1_FIFO_TH`。
- 现状：只有 SFLP 走 FIFO；accel/gyro 与 hub 数据仍是寄存器直读（每样本 3 次 SPI）。
- 顺序：先上 wake-up（单点收益明确），再按静止功耗实测决定是否值得做批量。

**结论**：M2 只补 wake-up；FIFO 批量视实测；其余不做。

## 3. 串口日志格式（固定契约）

### 3.1 三类行

| 类别 | 标签 | 何时出现 | 用途 |
| :--- | :--- | :--- | :--- |
| 事件 | 各模块自己的标签（`lgps_keys`/`lgps_power`/`lgps_gnss`…） | 状态变化时 | 一次性事实：上电、按键、状态机迁移、错误 |
| 周期遥测 | **`lgps_tele`** | 每 1 s，固定 5 行 | 机器可解析的全量快照 |
| 诊断横幅 | `lgps_diag` | 仅启动一次 | 构建/复位/时钟等静态信息 |

### 3.2 周期遥测字段（`lgps_tele`，固定顺序、单位写在键名里）

| 行 | 字段 | 来源 | 说明 |
| :--- | :--- | :--- | :--- |
| `GNSS` | `st` | `lgps_gnss_get_state()` | `OFF/COLD_START/FIXED/EPHEMERIS_HOLD/BACKOFF` |
| | `fix` `sats` | NAV-PVT | `fixType` 0..5、参与解算卫星数 |
| | `pdop_e2` | NAV-PVT pDOP | 观测的 ×100（1.23 → 123） |
| | `lat_e7` `lon_e7` `hmsl_mm` `spd_cms` `hdg_ddeg` | NAV-PVT | 与 `TrajectoryRecord_t` 同一量纲，无浮点损失 |
| | `utc_s` | NAV-PVT UTC + 2020 epoch | 秒；未解析时 `---` |
| | `age_ms` | 上一条 NAV-PVT 距今 | 判断接收机是否还在吐数据 |
| `IMU` | `id` | `WHO_AM_I(0x0F)` | LSM6DSV16X = `0x70`（LSM6DSR 版为 0x6B） |
| | `acc_lsb` `gyro_lsb` | 原始 16 位计数 | 精确值，便于换算/复核 |
| | `acc_g` `gyro_dps` | `LSB × 0.122 mg` / `× 35 mdps` | 灵敏度取自 DS13510 §4.2 Table 2 |
| `MAG` | `lsb` / `ut` | LIS2MDL 0x68..0x6D | `1 LSB = 1.5 mG = 0.15 µT`（LIS2MDL Table 3 `So`） |
| `BARO` | `press_pa` `temp_C` `rel_alt_mm` `alt_msl_mm` | BMP388 0x04..0x09 + TPG 系数 0x31..0x45 + 基准扇区 17 | 已做补偿（DS BST-BMP388-DS001-07 §9.2/§9.3）：Pa / °C 是物理值。`rel_alt_mm` = 相对基准（校准点为 0）；`alt_msl_mm` = 海拔，只有基准来自 GNSS 高度（`0x42 action=2` 或自动）时才有，否则 `---`。同一行保留 `press_raw` `temp_raw` 便于复核 |
| `SYS` | `bat_mv` | SAADC AIN4 + 分压实测标定 + ADC 标度修正 | 每 10 s 采样一次，期间沿用缓存 |
| | `chg` | SGM4056 `/CHG` | `1` = 正在充电 |
| | `keys` | 消抖后的电平 | `K1,K2`，`1` = 按下（用来看按键是否卡死） |
| | `zupt` `var_mg2` | ZUPT 状态机 | `locked/move` 与加速度模长方差（mg²） |
| | `steps` `ring_seq` `ring_rec` | PDR / 暂存环形 | 步数、当前扇区序号、累计写入条数 |

不可用的值统一打印 `---`，字段个数与顺序永不变动，便于 `awk`/`grep` 取用：

```bash
grep 'lgps_tele: GNSS' log.txt | sed -n 's/.*fix=\([0-9]*\).*/\1/p'
```

### 3.3 样例

```
[00:00:08.210] <inf> lgps_tele: GNSS st=COLD_START fix=0 sats=0 pdop_e2=--- lat_e7=--- lon_e7=--- hmsl_mm=--- spd_cms=--- hdg_ddeg=--- utc_s=--- age_ms=0
[00:00:08.225] <inf> lgps_tele: IMU id=0x6b acc_lsb=115,-25,9016 gyro_lsb=19,-13,-151 acc_g=0.014,-0.003,1.100 gyro_dps=0.67,-0.46,-5.29
[00:00:08.240] <inf> lgps_tele: MAG lsb=1234,-567,890 ut=185.1,-85.0,133.5
[00:00:08.255] <inf> lgps_tele: BARO press_pa=101177 temp_C=23.00 rel_alt_mm=--- alt_msl_mm=--- press_raw=7277088 temp_raw=8535808 (TPG compensated, BMP388 9.2/9.3)
[00:00:08.270] <inf> lgps_tele: SYS bat_mv=4021 chg=1 keys=0,0 zupt=move var_mg2=30 steps=0 ring_seq=1 ring_rec=0
```

> 样例中的数值仅示意字段位置；真实值必须由板子产生。`BARO` 一行的 `press_raw`/`temp_raw` 取自
> 2026-11-21 实测 burst（`60 09 6f 80 3b 82`），`press_pa`/`temp_C` 是用系数算出后的结果。

### 3.4 M1 首轮实测暴露的三个问题（已修，附根因）

| 现象 | 根因（源码级证据） | 修复 |
| :--- | :--- | :--- |
| `UARTE20 callback registration failed (-134)` + `GNSS init failed (-134)` | Zephyr 最小 libc 的 `ENOTSUP = 134`；nrfx UARTE 的 `.irq_callback_set` 只在 `CONFIG_UART_INTERRUPT_DRIVEN` 打开时才编进 API 表，否则 `uart_irq_*` 一律返回 `-ENOTSUP`（`drivers/serial/uart_nrfx_uarte.c` 的 `#ifdef UARTE_INTERRUPT_DRIVEN` 段） | `prj.conf` 增加 `CONFIG_UART_INTERRUPT_DRIVEN=y`；错误日志直接点明缺失的 Kconfig |
| GNSS 初始化失败后仍打印 `GNSS 1V8 rail -> on`（顺序看起来反了） | 上电由 `gnss` 线程执行，而 `init` 只注册回调；线程没有等 init 成功，于是"回调失败"与"上电"同时发生 | 新增 `gnss_ready_sem`：`init` 只有在 UART + IRQ 回调都就绪后才 `k_sem_give`，线程 `k_sem_take(K_FOREVER)` 之后才允许拉 `GPS_EN` |
| 只按了 KEY2，约 8 s 后自动 `System OFF` | KEY1（P1.06）上电即为低电平：`init` 把它当"已按下"，`press_start_ms` 从启动时刻起算，8 s 后长按条件满足 → 关机灯语 → `sys_poweroff()`。日志里没有 `KEY1_POI pressed` 只有 `long press` 正是这个特征 | 新增 **arm-on-release**：上电即为低的按键必须先被释放一次才允许产生 press/long-press；并在启动时 `WRN` 打印"某键上电即低，已解除武装，请检查网络/按键"，`SYS keys=` 字段可实时观察 |

补充说明：

* 启动横幅里的 `crystal CL out of spec` **是编码里写死的静态配置检查，不是硬件测量结果**（数值来自 BOM/设备树的 X1 = 12.5 pF、X2 = 12 pF，对比 nRF54L15 手册 6..9 pF 的 trim 窗口）。只有频率计或 BLE 载波测试才能给出真实 ppm，日志文案已改为 `static config check (NOT measured)`。
* KEY2 的短按检测本身没有问题：日志里的 `held 167/187/197 ms` 就是三次短按，每次 100 ms 消抖通过后触发一次"高精度模式"切换（本轮把原始 press/release 降到 DBG，只在语义事件上留 INF，避免刷屏）。真正的异常是上面的 KEY1 卡低。

### 3.5 第二轮实测暴露的问题（已修）+ 待确认的硬件事实

| 现象 | 根因 | 处理 |
| :--- | :--- | :--- |
| `WDT feed withheld, missing: 0x04 / 0x06` 每 10 ms 刷屏 | **上一轮引入的缺陷**：`lgps_wdt_feed()` 在主循环每 10 ms 评估一次"自上次喂狗以来所有任务都签到过"，而 IMU 生产者只有 26 Hz（38 ms 一次），这个与条件在任何 10 ms 瞬间几乎永不成立，于是永远不喂也不清零 → 既刷屏又会在 30 s 后复位 | 改为**监督窗口**模型：`lgps_wdt_feed()` 每 `LGPS_WDT_FEED_WINDOW_MS = 5 s` 才评估一次，窗口内每个任务至少签到一次即喂狗并清零；成功喂狗后清零掩码，所以死掉的任务无法满足下一个窗口（30 s 超时 = 6 次评估机会）。日志也随之从 100 Hz 降到最多 0.2 Hz |
| `keys=1,1`，两个按键上电都报低 | 需要区分"外部被拉低"与"内部上拉没生效" | 启动自检：对读低的按键先配 `PULL_UP` 读一次、再临时配 `PULL_DOWN` 读一次，四种组合分别给出结论（`up=0 down=0` = 外部拉低：按键被按/短路/网络错；`up=0 down=1` = 上拉配置无效；`up=1 down=0` = 正常释放）。日志会直接写明结论，无需示波器 |
| `sensor hub probe: LIS2MDL=0x00 BMP388=0x00 (MISMATCH)`，但 MAG 数据随后正常 | 探测等待时间太短：hub 的触发源是加速度计/陀螺 data-ready，26 Hz 时两个周期 = 77 ms，原来只等 30 ms，第一轮 hub 周期被错过；BMP388 还可能是 `SDO=1` 的 0x77 地址 | 等待改为 150 ms；探测重试 4 次；BMP388 在 `0x76`/`0x77` 之间自动尝试并记录实际响应的地址；配置完成后回读一次 12 字节 hub 原始数据并打印；压力全 0 时明确告警"检查 PWR_CTRL 写入路径 / SDO strapping / U4 是否贴件" |
| `bat_mv=---` 持续 10 s | 首次采样发生在 10 s 周期之后 | 启动时立即读一次并打印，事件日志也会给出失败原因 |
| `reset: none (0x00000000)` | `hwinfo_get_reset_cause()` 返回空 | 文案改为 `unreported`，不再声称"没有复位原因" |

**待确认的硬件事实（日志直接暴露出来的）**：

* 启动时 `spi_nor` 打印 **`flash@0: 32 MiBy flash`**。W25Q512NW 应为 **64 MB**，SFDP 报 32 MiB 说明实际贴件可能是 **W25Q256（32 MB）** 或同类 256 Mbit 料。当前暂存环只用前 64 KB，功能不受影响，但 `Firmware/docs/PARTITIONS.md` 与 README §8.3 的"63 MB 轨迹 + 1 MB OTA slot1 / 7~14 年归档"算术是按 64 MB 写的。请核对料号丝印后再决定是否改分区方案（D/E 项你说暂缓，这里只记录）。
* `keys=1,1` 与启动自检结论，用来判定是按键被按下、网络短地，还是焊接问题。

### 3.6 第三轮实测：软件缺陷与硬件问题分界

| 现象 | 判定 | 依据 / 处理 |
| :--- | :--- | :--- |
| WDT 不再刷屏 | ✅ 已修 | 监督窗口生效：`WDT30 armed: ... 5000 ms supervision window` 之后无 `withheld` |
| `keys=1,1`；自检 `up=0 down=0` | ⚠️ 硬件（见下） | 网表：`SW7.1` 走 R20 到 P1.06，而 **`SW7.2`/`SW7.3` 接地**；`SW1.1` 走 R21 到 P1.08，**`SW1.2`/`SW1.3` 接地**。4 脚轻触开关内部 1-2 与 3-4 各自短接，因此信号脚 1 与接地脚 2 是**同一侧，长期短地** → 按键永远读 0，按与不按没有区别。这是原理图/BOM 层面的接线错误，固件无法修 |
| 自检结论已细化 | ✅ 改进 | 现在打印 `dt_flags` 与两种**显式**上下拉读数，可区分"外部拉低 / 设备树标志丢失 / 驱动无效"三种情况 |
| GNSS 全程 `age_ms=0` | ✅ 已修（根因） | `UBX-CFG-MSG (0x06 0x01)` **不在 M10 的 UBX-CFG 章节里**（UBX-21035062 R03 §3.10 只有 CFG-CFG / CFG-RST / CFG-VALDEL / CFG-VALGET / CFG-VALSET；§4.10 表 58 注明部分 legacy CFG 报文在某些产品上不可用），所以 NAV-PVT 从未被打开，模块只按出厂配置吐 NMEA。现改为 `CFG-VALSET`（key `0x20910007` = NAV-PVT on UART1，RAM 层）+ `CFG-VALGET` 回读，并新增 `ACK-ACK/ACK-NAK` 分级日志与新增 `rx_dropped` / `baud` 计数（新增遥测组 `0x01 GNSS`，见 `BLE_Wire_v1.md` §5.1） |
| `bat_mv=134…161`，缓慢上升 | ⚠️ 待确认（大概率无电池） | 140 mV 反算：引脚 ≈26 mV、raw ≈89 计数，符合"分压端没有电池、只剩漏电给 C14 充电"。已在日志中加入 `raw/vref/vpin` 与"低于 2500 mV 视为无电池/分压开路"的一次性告警 |
| `sensor hub probe 0x00/0x00` 但 MAG 有数据 | ✅ 观察正确，待定位 | 12 字节 burst `35 ff 07 ff 27 00 | 00 00 00 00 00 00` 说明 **LIS2MDL 确实在出数（-203/-249/39 LSB ≈ -30 µT）**，BMP388 三个压力字节全 0。1 字节探测路径仍返回 0，需下一版日志确认是否为读时序问题；BMP388 更像 U4 未贴件或地址/焊接问题 |
| `reset: software (0x00000002)` | ℹ️ 正常 | 上一轮 `sys_poweroff()` 之后的复位路径 |

### 3.7 按键上拉：配置在哪里、如何自证

| 环节 | 内容 |
| :--- | :--- |
| 设备树 | `gpios = <&gpio1 6 (GPIO_ACTIVE_LOW \| GPIO_PULL_UP)>;`（KEY1 P1.06）、`<&gpio1 8 (...)>`（KEY2 P1.08） |
| 驱动调用 | `gpio_pin_configure_dt(spec, GPIO_INPUT)` → `gpio_pin_configure(port, pin, spec->dt_flags \| GPIO_INPUT)`，即 `dt_flags` 里的 `GPIO_PULL_UP` 会原样进驱动 |
| 编译期自证 | `BUILD_ASSERT((DT_GPIO_FLAGS(DT_NODELABEL(key_poi), gpios) & GPIO_PULL_UP) != 0, ...)`，两个按键各一条。若设备树丢了这个标志，**CI 直接编译失败**，不会带着浮空输入出厂 |
| 运行期自证 | 启动自检打印 `dt_flags=0x11`（= `GPIO_ACTIVE_LOW \| GPIO_PULL_UP`），并分别用**显式**上拉、显式下拉各读一次：`explicit_pull_up=1` 说明驱动确实能上拉；若 `explicit_pull_up=0 且 explicit_pull_down=0` 则是外部被硬拉低（与网表的"信号脚与地同一组触点"一致） |

> 结论：上拉**已配置**，并且从"注释里的承诺"升级为"编译期强制 + 运行期可观测"。
>
> **实测修正（2026-11-18）**：万用表测得引脚空闲 1.9 V、按下 0.3 V，即上拉与按键电气上完全正常，
> 但固件读到 0。问题在**软件读引脚路径**，不是硬件。为此新增三项可判定的证据：
> 1. `CONFIG_GPIO_NRFX_INTERRUPT=n`：M1 不需要 GPIO 中断，关掉后 P1 输入走朴素 `PIN_CNF` 写入，
>    不再经 `nrfx_gpiote` 配置（GPIO 中断路径是当时唯一与"早期可用版本"不同的系统级差异）；
> 2. `P1 read path OK (GPS_WAKE driven 1 -> read 1, driven 0 -> read 0)`：用**自己驱动的** P1.09
>    验证"读到的值 == 引脚实际电平"，读到 1/0 正确才说明读路径本身没问题；
> 3. 按键自检升级为打印 `effective=0x??`（驱动侧实际生效的 input/pull 位）与
>    `port IN=0x???????? -> bit6=?`（整端口原始寄存器 + 该位），可区分"配置没落地"与"外部拉低"。
> 但 100 Ω 串阻 + 开关内部短接到地的情况下，12–16 kΩ 的片内上拉在电气上赢不了：这两条同时成立时读数只能是 0。

### 3.8 第四轮实测：两个根因（都不是硬件）

| 现象 | 根因 | 处理 |
| :--- | :--- | :--- |
| 按键空闲时被当成"按下" | 自检数据自证：`dt_flags=0x11`、`effective=0x10010`（input+pull_up 都在）、`port IN` 的 bit6/bit8=1、`explicit_up=1 explicit_down=0`。也就是说**配置与硬件都正常**，只有"配置完成后立刻读"的那一次是 0 → 初始化用了一个settle 前的样本，把状态锁成"按下" | 配置后等 `KEY_SETTLE_MS=5ms` 再采样；自检后重新采样并以最终值初始化；轮询线程用 `keys_ready_sem` 门控，避免在初始化前读裸引脚。同时修掉自检里那条**误报**：之前把这种 settle 现象打印成"devicetree 丢了 GPIO_PULL_UP" |
| GNSS `rx` 一直在涨（≈400 B/s）但 `nmea=0 ubx=0` | u-blox M10 出厂默认 **38400 8N1**（接口说明书 `CFG-UART1-BAUDRATE 0x40520001 = 38400`），而 MCU 在 115200 上监听 → 收到的是帧错误产生的乱码，既没有 `$` 也没有 UBX 同步字 | `uart20` 改为 38400；另加一次 9600 回退（>200 字节仍无 `$`/UBX 时触发），并把当前波特率打进日志与遥测 |

### 3.9 32 MB / 64 MB Flash 兼容性与 WDT 语义修正

**Flash 兼容性（结论：同一份固件同时支持，靠 SFDP 运行时探测）**

| 项 | 做法 |
| :--- | :--- |
| 容量来源 | `flash_get_page_count()` × 页大小（驱动按 SFDP 读回真实密度），**不写死 64 MB** |
| 轨迹区 | `容量 − 1 MiB`（OTA slot1 置顶），32 MB 料 → 31 MiB，64 MB 料 → 63 MiB |
| 暂存环 | 固定占用前 64 KiB（16×4 KiB），两种料都够；容量不足时 `-ENOSPC` 明确报错 |
| 告警 | 不是 64 MB 时打印 `WRN`，提示归档年限预算按真实密度缩放 |
| 设备树 `size` | 单位是 **bit**（`dts/bindings/mtd/jedec,jesd216.yaml`："flash capacity in bits"）；W25Q512NW = 512 Mbit = `536870912`。`CONFIG_SPI_NOR_SFDP_RUNTIME=y` 时实际容量由 SFDP 决定，本行只是声明（旧值 `67108864` 声明的是 8 MiB 的器件） |

也就是说：**32 MB 与 64 MB 的差别只体现在"轨迹区容量"上，固件、分区逻辑、暂存环、掉电原子提交全部通用**；
需要改的只是 README 里"7~14 年归档"的推算基数（文档改动待批准）。

**WDT 语义修正（这是"又报 WDT"的根因）**

旧实现把 IMU 当成被监督任务，但签到只发生在"读取成功"分支里 —— 于是**任何一次 SPI 读失败或数据就绪位不满足，就等于"该任务没签到"**，
30 s 后复位，把"传感器故障"变成"复位循环"。而 IMU 根本没有自己的线程（采样跑在主循环里）。

修正后：监督对象 = **主循环线程** + **GNSS 线程**；签到含义 = "该线程仍在运行"；传感器故障走日志与遥测（`IMU` 行显示 `---`），不再触发复位。
日志也改为点名：`WDT feed withheld: gnss did not check in during the last 5000 ms window`。

### 3.10 按键"卡住/响应滞后"的最终根因：用了逻辑电平 API

`gpio_pin_get()` / `gpio_pin_get_dt()` 返回的是**逻辑电平**：驱动把端口值 XOR 上由 `GPIO_ACTIVE_LOW`
生成的 invert 掩码（见 `include/zephyr/drivers/gpio.h` 中 `z_impl_gpio_port_get()` 与
`z_impl_gpio_pin_get()`）。我们的按键在设备树里正是 `GPIO_ACTIVE_LOW`，于是：

| 实际状态 | 引脚 | `gpio_pin_get()` | 旧代码判定 |
| :--- | :--- | :--- | :--- |
| 松开 | 1.9 V | **0** | 按下 ❌ |
| 按下 | 0.3 V | **1** | 松开 ❌ |

这解释了现场日志的全部现象：开机"读到低"（其实是松开）、首次真实按下时打印
`released after boot, key armed`（50 s / 88 s 正是用户第一次按各键的时刻）、POI/模式切换要等到**松手**才打印（滞后一个动作）。
自检数据也一致：`effective=0x10010`（input+pull_up）、`port IN` bit6=1（物理高）、
显式上拉（未带 ACTIVE_LOW）读到 1。

修正：

* 全部采样改用 `gpio_pin_get_raw()`（物理电平，忽略 ACTIVE_LOW），0 恒等于"被拉到地"；
* 消抖后的**按下瞬间**立即 `LOG_INF`，用户按下就能看到反馈；
* POI 改为**按下即打点**（打的就是按下时所在位置）；KEY2 的模式切换仍在松手时执行，
  避免"按着走向 8 s 关机"途中误翻转模式。

**需要你在硬件侧确认/修订的两项**：

1. **按键接线**（这是真 bug，必须改板或改 BOM）：信号脚与接地脚落在轻触开关的同一侧触点对上，等于常闭到地。
   正确做法：信号接 1 脚、地接 **3 或 4 脚**（另一侧触点对），或直接换 2 脚开关。
2. **BMP388 是否贴件**：`0x76`/`0x77` 两个地址都读不到 CHIP_ID（期望 0x50），压力/温度寄存器全 0。如果 U4 是 DNP，固件这条告警应视为正常，需要在配置里显式排除。

### 3.11 第五轮：只读审计发现的 P0/P1 缺陷与修复（未在硬件上复测）

审计依据（三条外部事实，先核对后动手）：

1. `gpio_pin_get()` / `gpio_pin_get_dt()` 返回**逻辑电平**（`include/zephyr/drivers/gpio.h`：`z_impl_gpio_pin_get()` 把端口值 XOR 上由 `GPIO_ACTIVE_LOW` 生成的 invert 掩码）；
2. `SPI_CONFIG_DT` 的 CPOL/CPHA **只**来自设备树（`spi-cpol` / `spi-cpha`），没有 DT 属性就是模式 0；
3. `snprintk()` 返回“本该写入的长度”，不是实际写入的字节数。

#### P0（影响功能/数据）

| # | 位置 | 根因 | 修复 |
| :--- | :--- | :--- | :--- |
| 1 | `board_io.c:lgps_charger_charging()` | `CHG_STAT` 在 DT 里是 `GPIO_ACTIVE_LOW`，用 `gpio_pin_get_dt()` 取逻辑电平后又按 `value == 0` 判“充电中”，结论正好颠倒（与第四轮按键 bug 同一类） | 改用 `gpio_pin_get_raw()`：0 恒等于“/CHG 拉到地”，与 DT 标志无关 |
| 2 | DTS `imu@0` | 审计认为器件要求 SPI 模式 3 | **不成立，未改**：DS11976 §5.1 原文 “the device is compatible with SPI modes 0 and 3”（LSM6DSV16X 同结论：DS13510 §5.1），模式 0 是台架已验证的模式（WHO_AM_I=0x6B、hub 有真实数据）；已在 DTS 写明出处，避免下一轮重复报 |
| 3 | `flash_ring.c:lgps_ring_init()` | `records_in_sector` 只在 RAM，复位后为 0 → 第一条 append 直接写进已编程区（NOR 只能 1→0，旧记录被静默破坏） | 挂载时扫描扇区记录区，第一个“全 0xFF”槽位即写游标（`count_used_records()`，256 B 静态块 × 16 次读） |
| 4 | `flash_ring.c:sector_rotate()` | 封存扇区时就地重算 header CRC；NOR 无法把 0 位改回 1 → 下次挂载 CRC 不匹配 → 整个环被重新格式化 | CRC 覆盖范围改为 `magic/sector_seq/erase_count`（前 12 字节），`commit_flags` 移出 CRC；封存变成纯 1→0 更新（0xFFFF→0x55AA） |
| 5 | `gnss.c:gnss_uart_isr()` | ISR 里 `LOG_WRN`；`LOG_MODE_IMMEDIATE` 会逐字符 `poll_out` 并持锁（毫秒级），正好在 RX 溢出时丢数据 | ISR 只累加 `isr_rx_dropped`（atomic），由 GNSS 线程打印 |
| 6 | `gnss.c:handle_nav_pvt()` | 航向平移量写成 `+1800000`（1e-5° 单位下只有 18°），每个航向差 162° | `+18000000`（180°），单位换算仍是 `/1000`（1e-5°→1e-2°） |
| 7 | `gnss.c:handle_nav_pvt()` | `fix_type >= 3` 把 **5（time-only，无位置）** 也当成有效定位 | 只接受 3（3D）/ 4（GNSS+DR）且 `gnssFixOK` |
| 8 | `gnss.h` 字段名 | `hdop_1e2` 里装的其实是 NAV-PVT 的 **pDOP**（NAV-PVT 没有 hDOP） | 更名 `pdop_1e2`（gnss.h / gnss.c / telemetry.c），与遥测键 `pdop_e2` 一致 |
| 9 | `gnss.c:lgps_gnss_init()` | 启动日志写死 `115200`，链路实际 38400 | 用 `uart_config_get()` 读实际速率写入 `current_baud`，日志打印真实值（同时消除“常量假设”导致的 `set_baud()` 短路） |
| 10 | `wdt.c:lgps_wdt_feed()` | `snprintk()` 返回值累加进 `used` 后可超出缓冲 → `sizeof - used` 下溢为巨大 `size_t` → 越界写；另缺 `<zephyr/kernel.h>` | `format_missing()` 截断即返回，不再做减法下溢；补 `kernel.h` |
| 11 | `wdt.c:lgps_wdt_feed()` | 掩码只在“喂成功”后清零 → 死掉的任务可凭上一窗口的陈旧位蒙混过关（与 `wdt.h` 的设计自述矛盾） | 每个窗口结束即 `atomic_clear()`（返回旧值 = 原子 test-and-clear），喂不喂都消费本窗口签到 |
| 12 | DTS `flash@0: size` | `jedec,jesd216.yaml` 写明 `size` 是 **bits**；`<67108864>` 声明的是 8 MiB 器件 | 改为 `536870912`（512 Mbit = 64 MiB），并注明 SFDP 才是运行时容量来源 |

#### P1（语义/健壮性，已一并修）

| 模块 | 修复 |
| :--- | :--- |
| keys | 删除重复的 `read_pin()`；`gpio_pin_get_config()` / `gpio_port_get_raw()` 返回值判错；自检恢复后补注释说明 `dt_flags` 会重新 OR 回 invert 位；线程栈 1 KiB → 2 KiB（立即模式日志在调用者栈上格式化） |
| GNSS | `uart_irq_update` / `uart_irq_rx_ready` 按三值返回处理（1 / 0 / 负值含 `-ENOTSUP`）；`uart_irq_rx_enable` / `rx_disable` 在 Zephyr 里是 **void**（第一次 CI 就红在这：把 void 赋给 int）；`uart_fifo_read()` 改为 drain 循环（契约要求读到返回 0 为止）；上电 `ring_buf_reset()`；计数器按“ISR 写 / 线程写”拆分并原子化，去掉线程回写 ISR 计数器；`uart_irq_rx_disable()` 不会把引脚置高阻（注释按事实改写）；波特率回退改用基线差值 |
| LED/电源 | `lgps_led_off_all()` 现在真的取消运行中的序列并清空队列（生成号 + 50 ms 切片打断）；关机前先 `off_all()`，关机灯语不会再排在 9 s 开机灯语后面；`lgps_led_set()` 先转 `unsigned int` 再判越界（负值曾可越界） |
| diag/telemetry | `reset_text[48]` → `[128]`（十二个名字全拼是 119 字符）；`hwinfo_clear_reset_cause()` 读完即清；`var_mg2` 的 float→int32 加 NaN/上下限钳位；复位原因表注明 nRF 驱动只产生其中一部分标志 |
| battery | 改用 `adc_raw_to_millivolts_dt()`（增益/参考/分辨率全从 DT 取），去掉硬编码 `2 * vref` 与 `resolution == 0` 除零隐患 |
| board_io | P1 读路径自检改用 `gpio_pin_get_raw()`（旧版驱动的是 ACTIVE_HIGH 引脚，理论上发现不了刚犯的那类颠倒；注释已写清它能证什么/不能证什么）；CS 注释与日志改为事实描述——CS 由 SPI 驱动在 `main()` 之前配置，此处只补 DT 未要求的内上拉 |
| DTS/CMake | 删掉在 `CONFIG_INPUT` 未开启时无效的 `debounce-interval-ms`；`oversampling` 8 → 2（该属性是 log2：8 = 256 次平均）；CMake 注释改为“单镜像免 `-DBOARD_ROOT`，sysbuild 子镜像仍需显式传” |

#### 覆盖缺口（已补）

`lsm6dsv16x.c` / `flash_ring.c` / `telemetry.c` 原先只被板级构建编译，没有单测，所以第 3、4 号缺陷 twister 发现不了。本轮把扇区格式（表头编解码 + 记录区扫描）抽成无驱动依赖的 `lifegps/sector_format.h`，新增 `Firmware/tests/lifegps.ring`（native_sim）：表头往返/CRC 覆盖范围、封存后 CRC 仍有效、封存只会清位、记录区扫描的 0 / 5 / 126 / 127 条与分块边界。

> ⚠️ 环表头格式已变（CRC 覆盖范围）。旧固件写下的扇区会在下一次挂载时判为无效 → **一次性重新格式化前 64 KiB**。暂存环本身可丢数据，但归档内容会被清掉。

> RAM 增量（HW-04）：固件侧新增 256 B 扫描缓冲 + keys 栈 +1 KiB + `reset_text` +80 B ≈ **+1.3 KiB**；flash 增量仅注释与少量判断逻辑（<1 KB），以 CI size gate 为准。

## 4. 构建与测试（只在 GitHub）

```text
push -> firmware.yml
  job contract: validate.py + gen_c.py --check
  job build   : NCS v3.4.2 toolchain 容器
                west init/update (narrow: nrf zephyr cmsis hal_nordic picolibc nrfxlib)
                west build -b lifegps/nrf54l15/cpuapp Firmware
                size gate: text+data <= 712 KB, data+bss <= 256 KB
                twister -p native_sim -T Firmware/tests
                artifact: zephyr.hex / .elf / .map
```

## 4.1 已闭环：机体坐标系旋转矩阵（实测定案）

`README.md:708/725/731` 冻结的矩阵

```
        [ 0 -1  0 ]
R_IMU = [ 1  0  0 ]     X_body = -Y_sensor, Y_body = +X_sensor, Z_body = -Z_sensor
        [ 0  0 -1 ]
```

经 4 个独立来源复核（手算 + Hy4 + MiniMax M3.1 Flash + MiniMax M2.7，四者互不可见、结论一致）：

| 结论 | 依据 |
| :--- | :--- |
| `det(R) = -1` | 按第一行余子式展开：`0 − [(−1)·(−1)] + 0 = −1`。注意 README:709 的前半段其实是对的，错在最后一步把它又写了一次 `= −1 × (−1) = +1`——把一个已经算出的 `−1` 多乘了一个不属于余子式和的 `(−1)` |
| `R` 正交但 **improper** | `RᵀR = RRᵀ = I`，`det = −1` ⇒ 含一次镜像，不是 `SO(3)` 旋转 |
| 三条式子自相矛盾 | `X_body × Y_body = (−Y)×(+X) = +Z_IMU`，而文中 `Z_body = −Z_IMU` ⇒ 定义出左手系 |
| 任何刚体贴装都无法实现 | 刚体安装必为 `det = +1` |

### 4.1.1 定案（Main_V0.2 台架静态实测）

判据：静止时加速度计读的是**指向天顶的那根轴为正 +1 g**。四组手扶（≤12° 倾角）静态数据，
`acc_g` 是传感器系（遥测 `IMU` 行原样输出，未经 R 变换）：

| 放置 | `acc_g` 实测 | 传感器系里"上"方向 |
| :--- | :--- | :--- |
| 正面平放 | `(-0.010, 0.187, -0.954)` | `(0, 0, -1)` ⇒ 传感器 **+Z 朝下** |
| 顶部/前端抬 ~33° | `(-0.528, -0.147, -0.811)` | 前方朝上且 x 为负 ⇒ **前方 = -X_s** |
| 垂直（顶部朝上） | `(-0.995, -0.023, 0.048)` | `(-1, 0, 0)` ⇒ 传感器 **+X 朝下** |
| 横向（左侧朝上） | `(-0.008, 0.990, -0.021)` | `(0, +1, 0)` ⇒ 传感器 **+Y 朝上** |

三重积检验（反射会把符号翻掉）：传感器系 `u1·(u2×u3) = +1`，而用旧矩阵换算出的 body 向量
三重积为 `−1` ⇒ **旧矩阵在实测面前被证伪为镜像**，不再需要靠文档推理。

定案矩阵（`det = +1`，与 `README.md:660-682` 的机体定义 X 右 / Y 机头 / Z 正面朝外一致）：

```
        [ 0 -1  0 ]   X_body = -Y_sensor   (机身向右)
R_IMU = [-1  0  0 ]   Y_body = -X_sensor   (机头/行进方向)
        [ 0  0 -1 ]   Z_body = -Z_sensor   (正面法线向外)
```

与 README 的三条映射逐条比对：**Z 行、Y 行与实测一致，只有"传感器 +X 指向 PCB 背面向上"
这一条反了**（实际指向背面下方 / USB 端），而正是这一条把 `det` 从 `+1` 变成 `−1`。
根因基本可定位为 `README.md:701` 对 U12 Pin1 圆点与长/短边对应关系的判读，而不是芯片贴反。

陀螺仪：R 为 proper 后**共用同一矩阵**（轴上/伪矢量在 `det = +1` 下不需要额外 −1 因子），
`src/main.c` 的调用点不变。待台架验收（右手指向）：俯视顺时针转 90° ⇒ `gyro_body.z < 0`；
抬头 ⇒ `gyro_body.x > 0`。

改动范围（已执行）：`src/nav/nav.c` 一行（`out[1] = -in[0]`）+ `nav.h` 坐标系契约注释 +
`tests/lifegps.nav` 四处（basis 期望、`det = +1` 回归、`R² = I`、**四组实测数据回归**）。

> 影响面修正：M1 的 PDR 航向**并不受影响** ——`lgps_pdr_step()` 的 `heading_rad` 由外部传入，
> `gyro_body` 只用于 ZUPT 的 `‖ω‖`（模长对镜像不敏感）。本次是 M2（陀螺偏航积分、倾角补偿、
> App 姿态显示）的前置契约修正，不改变任何现有遥测输出。
> 遗留：`README.md` §1.3/1.4 的矩阵与推导文字仍是旧的 improper 版本（需批准后同步，见 §5）。

两种自洽的修法（互斥，取决于哪条前提是错的）：

* 若"芯片 Z 与机体 Z 反向"成立 → 前两行必须换，右手系解只有 4 组（`det` 均为 +1）：
  `(+X,−Y,−Z)`、`(−X,+Y,−Z)`、`(+Y,+X,−Z)`、`(−Y,−X,−Z)`
* 若"`X_body = −Y_sensor`、`Y_body = +X_sensor`"成立 → 第三行改为 `Z_body = +Z_sensor`，
  即 `R = [[0,−1,0],[1,0,0],[0,0,+1]]`（`det = +1`，`R² = diag(−1,−1,+1)`，`R⁴ = I`）

> 上表保留作为历史推理记录；实测给出的答案是候选组中的 `(−Y,−X,−Z)`，
> 既不是"第三行改为 `+Z`"，也不是任何"前两行成立"的解 —— 可见只靠数学确实无法定夺。


## 5. 需要你批准的既有文档同步（我未擅自修改）

1. ~~`README.md` §1.3/§2/§7.2：LSM6DSVTR → LSM6DSRTR；删除 SFLP 段落~~ **已撤销**：装配件回到
   LSM6DSVTR，README 的 LSM6DSVTR / SFLP 描述重新成立（本轮只把采样率与 SFLP 的实际实现写进
   `Firmware/README.md` 与 §2.1，不改设计叙述）。
2. ~~`Doc/Hardware/Pinmap_Main_V0.2.md`：`U12` 型号同步为 LSM6DSR~~ **无需修改**：pinmap 只写引脚与
   网络名，不含料号；引脚未变。
3. `Hardware/Schematic` 网表/BOM：`U12` 料号保持 **LSM6DSVTR**（与原始设计一致，无需动作）。
4. `.github/workflows/README.md`：把 `firmware.yml` 从「待加」移到「已启用」。
5. `Firmware/docs/PARTITIONS.md`：DTS 参考段里 `size = <0x4000000>` 按 binding 是 **bit**（= 8 MiB），按注释意图是 64 MiB，二者矛盾；且 `slot1` 写死 `0x3F00000` 只对 64 MB 料成立，32 MB 料不存在该地址。建议改为“不写 `size`、slot1 偏移 = `容量 − 1 MiB` 运行时计算”，需要你确认后我再改这份契约文档（硬件/App 都在引用它）。
6. `README.md` §1.3/1.4：旋转矩阵改为 `[[0,−1,0],[−1,0,0],[0,0,−1]]`（det=+1），并把
   “X_IMU 在 PCB 背面垂直向上”改为“垂直**向下**（USB 端）”——只这一条判读反了，它正是 det=−1 的来源。
   依据见 §4.1.1 的四组实测数据。

## 6. GNSS 调试链路（0.2.0 追加）

现场记录：室内无定位时，模块在 60 s 冷启动超时后被断电，退避 120→300→900 s，
手机上什么也看不到。为此增加三条互相独立的能力，全部按需开启、有寿命上限：

| 能力 | 触发 | 固件行为 | 界面 |
| :--- | :--- | :--- | :--- |
| 调试会话 | `0x36 GNSS_DEBUG` bit0 | 会话期间跳过冷启动 60 s 与星历保持 35 s 的断电判断，到期自动恢复 | GPS 页「调试会话」卡片 |
| 单星 | `0x36` bit1 | `CFG-VALSET` 打开 NAV-SAT（1 Hz，**RAM 层**，断电即失效），解析后走遥测组 `0x09` | GPS 页卫星表：系统 / SVID / C/N0 / 仰角 / 状态 |
| NMEA 直通 | `0x36` bit2 | GNSS 线程按 `$…\r\n` 切句，落 8 槽环（96 B/槽），主线程 50 ms 节拍出队走遥测组 `0x08` | 「NMEA 原始流」页，可按 talker 过滤 |

参数读写走 `0x35 GNSS_CFG`，键表见 `Doc/Software/BLE_Wire_v1.md` §6.6：默认写
**RAM + BBR**（跨 `RXM-PMREQ` 断电保留），显式选择才写 Flash；`CFG-UART1-BAUDRATE`
一类会把链路写死的键**不在白名单**。

同批修掉的两个缺陷：

1. `pDOP = 99.99`（`0x270F`）是接收机的"无解"哨兵值，之前被当成真实 DOP 写进 Status
   帧，App 显示 `HDOP 99.99`；现在只在定位有效且 `pdop < 9999` 时透传。
2. 退避序列步进早了一步：`backoff_ms` 初值是 120 s，首次断电时却取"120 s 的下一档"，
   所以第一次退避实际是 **300 s**，文档里的 120→300→900 从未从 120 开始。现在
   "等当前档、再升档"，且一次成功定位把序列退回 120 s。
