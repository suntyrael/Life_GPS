# M1 固件 bring-up 说明（Main_V0.2 / nRF54L15-QFAA）

> 状态：**M1 骨架已落地，GitHub Actions 编译 + 单测通过**。本地零编译（见 `AGENTS.md` §2），
> 所有构建只在 `.github/workflows/firmware.yml` 中执行。

首次通过的实测记录（commit `3ef1f2b4`，workflow run 44132071171，NCS v3.4.2 官方 toolchain 容器）：

| 项 | 结果 |
| :--- | :--- |
| 固件镜像 `lifegps/nrf54l15/cpuapp` | FLASH 96,428 B / 729,088 B（6.18% of 1524 KB），RAM 15,765 B / 262,144 B（6.01%） |
| twister native_sim | 6/6 套件通过 |
| 协议契约 | `validate.py` PASS，`gen_c.py --check` 无漂移 |

## 1. M1 覆盖范围

| 模块 | 文件 | M1 行为 |
| :--- | :--- | :--- |
| 串口诊断 | `src/diag/diag.c` | 上电横幅：复位原因（hwinfo）、协议版本、总线/引脚摘要、晶振偏差告警；日志 = `时间戳 级别 模块: 内容` |
| 板级 IO | `src/board/board_io.c` | CS 先置高（Flash P2.05 / IMU P1.14，均无外部上拉）、GPS_EN 低、CHG_EN 低（允许充电）、CHG_STAT 上拉输入、未用脚 P2.00/03/06/08/09/10 断开输入缓冲 |
| LED 灯语 | `src/power/led.c` | README §9.1 全部图案（开机红→蓝→绿 3 次、关机绿→蓝→红 3 次、定位双闪绿、BLE 单闪蓝、同步快闪蓝、高精蓝绿交替、PDR 黄闪、低电快闪红） |
| 按键 | `src/power/keys.c` | 10 ms 轮询 + 20 ms 软件消抖（板上无 RC）、8 s 长按、短按事件 |
| 电源状态机 | `src/power/power_sm.c` | 长按 8 s → 关机灯语 → `sys_poweroff()`；KEY1 短按 = POI 打点请求；KEY2 短按 = 高精模式标志 |
| 看门狗 | `src/power/wdt.c` | WDT30 30 s，分层喂狗（main / gnss / imu 各任务签到齐才喂） |
| 电池 | `src/power/battery.c` | SAADC AIN4（P1.11），按 R25/R26∥R_in 修正分压比；**不输出百分比**（放电曲线未实测） |
| GNSS | `src/gnss/gnss.c`, `src/gnss/ubx.c` | UARTE20 115200、UBX-CFG-MSG 打开 NAV-PVT、解析 NAV-PVT、冷启动 60 s → 首定 → 星历保持 35 s → PMREQ 备份 + 断电 → 退避 120/300/900 s |
| IMU | `src/imu/lsm6dsr.c` | LSM6DSR 软复位、WHO_AM_I=0x6B 校验、±4 g/±1000 dps @26 Hz 高性能（数据就绪位驱动采样）、INT1=DRDY；sensor hub 探测 LIS2MDL/BMP388 并配置 12 字节串流 |
| 导航数学 | `src/nav/nav.c` | 机体坐标旋转、ZUPT 双判据（σ²<0.005 g²、‖ω‖<0.08 rad/s、持续 10 s）、Weinberg 步长、磁异常判据 |
| 存储 | `src/storage/flash_ring.c` | W25Q512 暂存环形：16×4 KB 扇区、16 B 扇头（magic/seq/erase_count/commit_flags/CRC16）、127 条 32 B 记录/扇区、写满轮转 + 擦除 |
| 记录编解码 | `src/storage/traj.c` | 由 `protocol/lifegps_protocol.yaml` 生成的结构体 + 逐字节小端序列化 + CRC-8 |
| 协议生成 | `tools/protocol-gen/gen_c.py` | YAML → `Firmware/include/generated/lifegps_protocol.h`；`--check` 供 CI 防漂移 |
| 单测 | `Firmware/tests/lifegps.*` | CRC/时间、UBX、导航数学、记录编解码、生成头契约（native_sim + ztest） |

**不在 M1**：BLE/GATT、MCUboot OTA、heatshrink 归档、CRACEN 加密、气压高程解算、SFLP 姿态融合。

## 2. 硬件事实（每条都有出处）

| 事实 | 值 | 出处 |
| :--- | :--- | :--- |
| 调试串口 | UARTE30，P0.00 TX / P0.01 RX，115200 8N1 | `Doc/Hardware/Pinmap_Main_V0.2.md` §2；`sdk-zephyr` `dts/vendor/nordic/nrf54l_05_10_15.dtsi`（`uart30@104000`） |
| GNSS 串口 | UARTE20，P1.04 TX / P1.05 RX | 同上 |
| IMU SPI | SPIM22，SCK P1.03 / MOSI P1.02 / MISO P1.13 / CS P1.14（同域，无需 Constant Latency） | 同上 + pinmap §1.1.2 方案 B |
| Flash SPI | SPIM00，SCK P2.01 / MOSI P2.02 / MISO P2.04 / CS P2.05，8 MHz | pinmap §2 |
| 电池采样 | P1.11 = AIN4，R25 = 1 MΩ、R26 = 330 kΩ、SAADC R_in ≈ 735 kΩ | pinmap §2/§3 |
| NFC 脚释放 | `&uicr { nfct-pins-as-gpios; }` | `boards/nordic/nrf54l15tag/*.dts:26`（同版本 Zephyr 4.4.2） |
| RRAM 分区 | boot 62 KB / slot0 712 KB / slot1 712 KB / storage 36 KB | `dts/vendor/nordic/nrf54l15_cpuapp_partition.dtsi` |
| Flash 深度掉电 | tDP = 3 µs、tRES1 = 30 µs | `Doc/Hardware/Datasheets/Winbond_07022021_W25Q512NW-2451091.pdf` AC characteristics |

### 2.1 六轴传感器已更换为 **LSM6DSRTR**

`Hardware/Schematic` 的 V0.2 网表（`U12`）与 DC 手册当前按 **LSM6DSV16X** 记录，实际装配件按你的
说明改为 **LSM6DSRTR**。两者同为 LGA-14L、引脚定义兼容（2/3 = SCx/SDx 传感器中枢 I²C，13/14 = 主 SPI，
1 = SDO/SA0，4 = INT1，12 = CS），**PCB 无需改动**，但固件侧的差异必须按 LSM6DSR 手册
（ST **DS11976 Rev 2**）重写：

| 项 | LSM6DSV16X（旧文档） | **LSM6DSR（现行）** |
| :--- | :--- | :--- |
| WHO_AM_I (0x0F) | 0x70 | **0x6B**（§9.14） |
| 加速度 ODR 档位 | 60 Hz 等 | **12.5 / 26 / 52 / 104 / 208 / 416 / 833 / 1.66k…**（§9.15 Table 50），无 60 Hz |
| 陀螺 ODR 档位 | 60 Hz 等 | 同表（§9.16 Table 53），无 60 Hz |
| 加速度 FS 位 | CTRL1 中 OP_MODE+FS | `CTRL1_XL(0x10)`：ODR[7:4]、FS[3:2]（01=±16 g、**10=±4 g**）、LPF2 bit1 |
| 陀螺 FS | `CTRL6` | `CTRL2_G(0x11)`：ODR[7:4]、FS[3:2]（**10=±1000 dps**）、FS_125 bit1、FS_4000 bit0 |
| 灵敏度 | ±4 g: 0.122 mg/LSB；±1000 dps: 35 mdps/LSB | **相同**（§2 Table 3 `LA_So` / `G_So`） |
| 控制寄存器 | CTRL1..CTRL10 | **CTRL1_XL / CTRL2_G / CTRL3_C / CTRL6_C / CTRL7_G / CTRL8_XL / CTRL9_XL / CTRL10_C** |
| Sensor hub 输出 | `SENSOR_HUB_1..6`（6 字节） | **`SENSOR_HUB_1..18`（0x02..0x13，18 字节）**（§14） |
| Hub 主控寄存器 | `MASTER_CONFIG(0x14)`、`SLV0_ADD(0x15)`… | 地址相同；`MASTER_ON`=bit2、`AUX_SENS_ON`=bit1:0、`SHUB_PU_EN`=bit3；`SLVx_CONFIG` 的 `SHUB_ODR[1:0]`=bit7:6（00=104 Hz、01=52、10=26、11=12.5） |
| SFLP 硬件四元数 | 有（README §2 的 14 µA 论据） | **没有**（DS11976 特性表无 SFLP，只有 FSM / 计步器 / 显著运动检测） |
| 计步 | 需固件过零检测 | **芯片内置 pedometer / step detector / step counter** |

因此 M1 的结论与待办：

1. 固件**不再假设** 60 Hz ODR 与 SFLP。LSM6DSR 的 ODR 只有 12.5 / 26 / 52 / 104 / 208 Hz，**没有 20 Hz**；
   因此取最接近的 **26 Hz** 原生档，ZUPT 窗口取 **N = 26 点**，恰好 **1.000 s**，不需要抽取、不存在窗口畸变，
   README §7.5 的方差门限（0.005 g² / 0.08 rad/s）按 1.0 s 窗口仍然成立。
   采样节拍由 `STATUS_REG`（0x1E）的 `XLDA|GDA`（bit0|bit1）驱动，主循环 10 ms 轮询、无睡眠漂移。
2. README §2「SFLP 硬件 6 轴姿态 60 Hz、增量 14 µA」对本机型**不成立**，需要改写为「固件姿态融合」或改用 LSM6DSR 的 FSM/计步器路径；**这属于既有文档改动，需要你批准后我才动 `README.md` / `Doc/Hardware/Pinmap_Main_V0.2.md` / `AGENTS.md`**。
3. Sensor hub 现在有 18 字节输出：M1 用 6 字节 LIS2MDL（0x68 起，50 Hz）+ 6 字节 BMP388（0x04 起，25 Hz）。
4. BMP388 补偿算法（Bosch 官方补偿系数 + 温度补偿）**尚未实现**，M1 只搬运原始 24 位压力/温度，不输出海拔——避免出现编造的高度值。

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
| `IMU` | `id` | `WHO_AM_I(0x0F)` | LSM6DSR = `0x6B` |
| | `acc_lsb` `gyro_lsb` | 原始 16 位计数 | 精确值，便于换算/复核 |
| | `acc_g` `gyro_dps` | `LSB × 0.122 mg` / `× 35 mdps` | 灵敏度取自 DS11976 Table 3 |
| `MAG` | `lsb` / `ut` | LIS2MDL 0x68..0x6D | `1 LSB = 1.5 mG = 0.15 µT`（LIS2MDL Table 3 `So`） |
| `BARO` | `press_raw` `temp_raw` | BMP388 0x04..0x09 | 原始 24 位；**未做补偿**，所以不打印 hPa/°C |
| `SYS` | `bat_mv` | SAADC AIN4 + 分压修正 | 每 10 s 采样一次，期间沿用缓存 |
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
[00:00:08.255] <inf> lgps_tele: BARO press_raw=8012345 temp_raw=6412345 (uncompensated)
[00:00:08.270] <inf> lgps_tele: SYS bat_mv=4021 chg=1 keys=0,0 zupt=move var_mg2=30 steps=0 ring_seq=1 ring_rec=0
```

> 样例中的数值仅示意字段位置；真实值必须由板子产生。

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
| GNSS 全程 `age_ms=0` | ✅ 已加诊断 | 新增 `rx/ubx/ckerr/nmea` 计数：`rx=0` = 模块完全没说话；`nmea>0 ubx=0` = 只吐 NMEA、CFG-MSG 未被接受；`ckerr>0` = 波特率/连线问题。同时把 CFG-MSG 改为**每 2 s 重发、收到第一个 UBX 帧或 10 s 后停止**（模块上电 >100 ms 才启动，首次配置容易被忽略） |
| `bat_mv=134…161`，缓慢上升 | ⚠️ 待确认（大概率无电池） | 140 mV 反算：引脚 ≈26 mV、raw ≈89 计数，符合"分压端没有电池、只剩漏电给 C14 充电"。已在日志中加入 `raw/vref/vpin` 与"低于 2500 mV 视为无电池/分压开路"的一次性告警 |
| `sensor hub probe 0x00/0x00` 但 MAG 有数据 | ✅ 观察正确，待定位 | 12 字节 burst `35 ff 07 ff 27 00 | 00 00 00 00 00 00` 说明 **LIS2MDL 确实在出数（-203/-249/39 LSB ≈ -30 µT）**，BMP388 三个压力字节全 0。1 字节探测路径仍返回 0，需下一版日志确认是否为读时序问题；BMP388 更像 U4 未贴件或地址/焊接问题 |
| `reset: software (0x00000002)` | ℹ️ 正常 | 上一轮 `sys_poweroff()` 之后的复位路径 |

**需要你在硬件侧确认/修订的两项**：

1. **按键接线**（这是真 bug，必须改板或改 BOM）：信号脚与接地脚落在轻触开关的同一侧触点对上，等于常闭到地。
   正确做法：信号接 1 脚、地接 **3 或 4 脚**（另一侧触点对），或直接换 2 脚开关。
2. **BMP388 是否贴件**：`0x76`/`0x77` 两个地址都读不到 CHIP_ID（期望 0x50），压力/温度寄存器全 0。如果 U4 是 DNP，固件这条告警应视为正常，需要在配置里显式排除。

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

## 4.1 待闭环：机体坐标系旋转矩阵（M2 阻塞项）

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

两种自洽的修法（互斥，取决于哪条前提是错的）：

* 若"芯片 Z 与机体 Z 反向"成立 → 前两行必须换，右手系解只有 4 组（`det` 均为 +1）：
  `(+X,−Y,−Z)`、`(−X,+Y,−Z)`、`(+Y,+X,−Z)`、`(−Y,−X,−Z)`
* 若"`X_body = −Y_sensor`、`Y_body = +X_sensor`"成立 → 第三行改为 `Z_body = +Z_sensor`，
  即 `R = [[0,−1,0],[1,0,0],[0,0,+1]]`（`det = +1`，`R² = diag(−1,−1,+1)`，`R⁴ = I`）

究竟哪一组正确，取决于 `U12` 在 PCB 上的 Pin1 实际朝向（DS11976 Figure 3 轴向图 + 装配/坐标文件），
或台架实测（绕各轴转动看 gyro 符号），**无法由数学单独定夺**。

> 现状：`src/nav/nav.c` 仍按 README 的矩阵实现（`out = {-in[1], in[0], -in[2]}`），
> 因此 PDR 的航向符号在这一项闭环前是错的；ZUPT、步长、磁异常判据不受影响
> （IMU 与磁力计共用同一矩阵，§1.5 的"1:1 同向"结论仍然成立）。
> 定案后需改 `nav.c` 一处 + `Firmware/tests/lifegps.nav` 的期望向量。

## 5. 需要你批准的既有文档同步（我未擅自修改）

1. `README.md` §1.3/§2/§7.2：LSM6DSVTR → **LSM6DSRTR**；删除「SFLP 硬件四元数 60 Hz / 14 µA」，改为固件融合或 LSM6DSR FSM/计步器路径。
2. `Doc/Hardware/Pinmap_Main_V0.2.md`：`U12` 型号与 INT1/CS 描述同步为 LSM6DSR；IMU 采样率由 20 Hz 改为「104 Hz 采样 → 20 Hz 抽取」。
3. `Hardware/Schematic` 网表/BOM：`U12` 料号改为 LSM6DSRTR（硬件侧动作）。
4. `.github/workflows/README.md`：把 `firmware.yml` 从「待加」移到「已启用」。
