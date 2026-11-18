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

## 3. 串口日志长这样

```
[00:00:00.014] <inf> lgps_diag: =========================================================
[00:00:00.015] <inf> lgps_diag:  Life_GPS firmware M1 (bring-up skeleton)
[00:00:00.016] <inf> lgps_diag:  board  : Main_V0.2, nRF54L15-QFAA (QFN48), 1.9 V rails
[00:00:00.017] <inf> lgps_diag:  reset  : power-on (0x00000008), uptime 17 ms
[00:00:00.019] <wrn> lgps_diag: crystal CL out of spec: X1 12.5 pF / X2 12 pF vs 6..9 pF, ...
[00:00:00.031] <inf> lgps_board_io: chip selects: Flash /CS=P2.05 IMU /CS=P1.14 driven high ...
[00:00:00.045] <inf> lgps_led: LED ready: RED=P1.10 GREEN=P2.07 BLUE=P1.12, active high
[00:00:00.052] <inf> lgps_keys: keys ready: KEY1_POI=P1.06 KEY2_MODE=P1.08 (pull-up, 20 ms debounce, 8000 ms long press)
[00:00:00.060] <inf> lgps_wdt: WDT30 armed: timeout 30000 ms, 3 supervised tasks (layered feed)
[00:00:00.075] <inf> lgps_gnss: GNSS UART ready: UARTE20 P1.04(TX)/P1.05(RX) 115200 8N1, default part MAX-M10S (UBX)
[00:00:00.120] <inf> lgps_imu: LSM6DSR detected on SPIM22 (WHO_AM_I=0x6b, CS=P1.14, 8 MHz)
[00:00:00.140] <inf> lgps_imu: LSM6DSR configured: +/-4 g (0.122 mg/LSB), +/-1000 dps (35 mdps/LSB), ODR 104 Hz
[00:00:00.310] <inf> lgps_imu: sensor hub probe: LIS2MDL WHO_AM_I=0x4f (ok) BMP388 CHIP_ID=0x50 (ok)
[00:00:00.360] <inf> lgps_ring: staging ring: 16 sectors, 127 records/sector, record 32 bytes
[00:00:01.020] <inf> lgps_main: status: uptime=1020 ms gnss=COLD_START fix=0 sats=0 ...
[00:00:01.021] <inf> lgps_main: imu: acc=[0.001 0.002 1.001] g gyro=[0.12 -0.04 0.02] dps var=0.00001 g^2
```

> 上面是**格式示意**（字段来自代码路径）。真实数值必须由板子跑出来，M1 不写入任何“看起来正常”的假数据。

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

经 3 个独立来源复核（手算 + Hy4 + MiniMax M3.1 Flash，后两者互不可见、结论一致）：

| 结论 | 依据 |
| :--- | :--- |
| `det(R) = -1` | 按第一行余子式展开：`0 − (−1)(−1) + 0 = −1`；README:709 把它算成 `+1` 是双重符号错误 |
| `R` 正交但 **improper** | `RᵀR = RRᵀ = I`，`det = −1` ⇒ 含一次镜像，不是 `SO(3)` 旋转 |
| 三条式子自相矛盾 | `X_body × Y_body = (−Y)×(+X) = +Z_IMU`，而文中 `Z_body = −Z_IMU` ⇒ 定义出左手系 |
| 任何刚体贴装都无法实现 | 刚体安装必为 `det = +1` |

保持"芯片 Z 轴与机体 Z 轴反向"这一物理前提时，右手系的解只有 4 组（`det` 均为 +1）：

`(+X,−Y,−Z)`、`(−X,+Y,−Z)`、`(+Y,+X,−Z)`、`(−Y,−X,−Z)`

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
