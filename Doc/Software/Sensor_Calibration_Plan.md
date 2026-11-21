# 交接：传感器链路收尾 + 三传感器标定（2026-11-19）

本文件是给**新会话**的上下文入口。读完这一页就能继续工作，不需要翻历史对话。
规则仍以仓库根 `AGENTS.md` 为准（本地零编译、改动需逐项批准、协议只追加字段）。

## 1. 当前状态

| 项 | 值 |
|---|---|
| 分支 | `diag/hub-bmp388`（HEAD `271826e`；`main` 由另一会话推进，未见冲突） |
| CI | `firmware` success（75/75 twister）、`app` success；最近一次 FLASH 241464 B / RAM 55440 B |
| 硬件 | Main_V0.2，U12 = **LSM6DSVTR**（网表原名，晶圆即 LSM6DSV16X，手册 DS13510 Rev 4），U3 = LIS2MDL，U4 = BMP388 |
| **U4(BMP388) 现状** | **已恢复**：重新焊接后 `0x76` 有 ACK（`CHIP_ID=0x50`），TPG 系数已能通过 hub 读出。首轮数据：气压原值 ~7.27e6、温度原值 ~8.53e6，低 5/7 位恒为 0 —— 这是 `osr_p=x8`/`osr_t=x2` 的有效位（16+log2(osr)），不是驱动 bug |

已落地（不要重复实现）：IMU 采样含裸片温度、重力/线性加速度拆分、磁力计温度、Status 帧 82→100 字节、
App 侧 sensor/body 坐标系分离（±1 g 抖动已修）、hub 状态位解码、诊断收进 Kconfig。

## 2. 必须知道的坑（都踩过）

1. **`numop` 只有 3 bit（0..7）**。写 `0x08` 会落到 `BATCH_EXT_SENS_0_EN`，numop 变 0 → 该从机一个字节都不读，
   且不会报错（`Firmware/src/imu/lsm6dsv16x.c` 的 `HUB_MAX_NUMOP`）。
2. **排在 NACK 从机后面的 hub 槽位不会被填**。所以 BMP388 必须排在最后（`hub_configure()` 注释）。
3. **LIS2MDL `TEMP_OUT` 有 +25 °C 零点**：`T = raw/8 + 25`（DS12144 §8.16 只给了灵敏度；
   零点见 ST 驱动/AN5069，2026-11-19 台架用 IMU 裸片温度交叉验证过）。
4. **Status 帧里 `acc_lsb/gyro_lsb` 是传感器系**，`gravity_mg/linear_mg` 与 Telemetry `0x02` 的 `acc_lsb` 是**体坐标系**。
   同一 UI 字段不得跨坐标系写入（这正是 −1 g/+1 g 跳动的根因）。
5. **hub 周期被 IMU data-ready 钳制在 30 Hz**（DS13510 Table 477：`START_CONFIG=0` 时触发源是加速度计/陀螺 data-ready），
   `SHUB_ODR`（`SLV0_CONFIG[7:5]`，Table 483）写更大值不生效。
6. 温度/气压：BMP388 的 TPG 补偿**已实现**（`Firmware/src/imu/bmp388.c`：读 `0x31..0x45` 的 21 字节系数，
   再按 DS §9.2/§9.3 的参考实现算 Pa/°C）；单测 `Firmware/tests/lifegps.bmp388`。
   原值不是 20 位、不能右移 4 位（§3.10 是 24 位）；`0x800000` 也**不是** 0 °C（那是寄存器复位值，不是刻度）。

## 3. 待做（按优先级）

### P0-A 加速度计刻度标定（小）
现象：静止时 `|acc| ≈ 1.024 g`（偏高约 2.4%），`0.122 mg/LSB` 是标称值。
做法：静止时用 `|acc|` 求一个标量缩放系数；在 firmware 里加 `LGPS_IMU_ACCEL_SCALE`（默认 1.0F），
在 `Firmware/src/imu/lsm6dsv16x.c` 的 `decode_sample()` 里乘上；同时把陀螺零偏也纳入同一套标定记录。
验收：静止 10 s，`|acc|` 落在 1.000 ± 0.005 g。

### P0-B 三传感器标定功能（App + 固件 + 协议，主任务）

**协议**（`protocol/lifegps_protocol.yaml`，只追加字段，`version` 升 minor）：
已有但固件返回 `NOT_SUPPORTED` 的 opcode：`0x40 SELFTEST`、`0x41 MAG_CAL`、`0x42 BARO_BASELINE`。
需要新增：`0x43 ACCEL_CAL`（或复用 SELFTEST 子命令，二选一，先定方案再改）。

请求/响应负载建议（小端）：

```
MAG_CAL       req  {u8 action}            action: 0=开始采样 1=结束并保存 2=清除
              rsp  {u8 status, u16 progress_permille, i16 x/y/z_offset_lsb[3]}
ACCEL_CAL     req  {u8 action, u8 position}  position: 0..5 六面法，或 6=单面快速
              rsp  {u8 status, u16 progress_permille, i16 x/y/z_offset_usrofs[3]}
BARO_BASELINE req  {u8 action, u32 baseline_pa_q4}  action: 0=取当前为基准 1=手动设置
              rsp  {u8 status, u32 baseline_pa_q4, i32 rel_alt_mm}
```

**固件落点**：
- 磁力计硬铁：LIS2MDL `OFFSET_X/Y/Z_REG_L/H`（`0x45..0x4A`，16 位补码）——`Firmware/src/imu/lsm6dsv16x.c` 的
  `shub_write_byte()` 一次写一个字节，写 6 个字节即可（注意每写一次要等 hub 周期）。
  软铁/刻度仍需软件处理（旋转采样求各轴 min/max，`offset=(min+max)/2`，`scale=2/(max-min)`）。
- 加速度计零偏：LSM6DSV16X `X/Y/Z_OFS_USR`（`0x73/0x74/0x75`，主页面直接可写，Table 213/215），
  `CTRL9(0x18).USR_OFF_W`（bit1）选择权重（0 = 2⁻¹⁰ g/LSB、1 = 2⁻⁶ g/LSB，Table 71），
  `CTRL9(0x18).USR_OFF_ON_OUT`（bit0）=1 打开补偿。
  六面法每个姿态采样得到 `offset_axis = -(measured) / 2`，注意符号与 LSB 权重。
- 气压基准：`BARO_BASELINE` 的前置 TPG 补偿**已完成**；剩下的是「取当前压力为基准 + 相对高程」，
  并且要把 Pa 放进帧（协议改动）。

**系数持久化（新的一个子系统）**：目前 `CONFIG_SETTINGS` 未启用，`prj.conf` 只有 `CONFIG_FLASH_MAP=y`。
需要决定：启用 `CONFIG_SETTINGS` + `CONFIG_SETTINGS_NVS`（占用一块 flash 分区，注意 `Firmware/docs/PARTITIONS.md`
的布局与 OTA slot 预算），或复用轨迹环之外的保留扇区。**这是需要单独批准的一步**（涉及分区表与 RAM/flash 预算）。

**App 落点**：`App/app/src/main/java/com/lifegps/app/debug/SensorStatusScreen.kt` 已有 `BARO_BASELINE` 按钮（目前是空壳）。
需要：校准向导（磁力计画 8 字/旋转、加速度计六面、气压基准一键）、进度、结果回读校验、失败提示；
解析侧在 `App/app/src/main/java/com/lifegps/app/ble/BleFrame.kt` 增加对应的请求/响应编解码。

### P1 显示与文档细节
- 磁力计温度显示精度：物理分辨率 8 LSB/°C = 0.125 °C，现在按 `%.1f` 显示已足够，若要改上限说明。
- `Doc/Software/BLE_Wire_v1.md` §4/§6 随协议改动同步。
- `Firmware/docs/M1_BRINGUP.md` 与 `Doc/Hardware/Pinmap_Main_V0.2.md` 仍把 U12 写成 LSM6DSV16X，需另开一次"文档校正"（要单独批准）。

### P2 BMP388 复活后的收尾
- [x] `IF_CONF(0x1A)` 的 I²C 看门狗超时显式配置（默认 NVM 1.25 ms short；现在读系数前先写 40 ms）。
- [x] TPG 补偿（系数 `0x31..0x45`）+ 串口遥测的 Pa/°C 上报。
- [x] `BARO_BASELINE`（0x42）：手动输入 / 取当前，以及 GNSS 条件自动基准（`fixType≥3`、
      `numSV≥8`、pDOP≤2.0、地速<0.3 m/s，连续 30 s、窗口气压 σ<0.3 hPa，>1 hPa 才重设）。
- [x] 相对高程：ISA 公式（`Firmware/src/nav/baro.c`），`0x42` 响应与 `0x04` 遥测包都有 `rel_alt_mm`。
- [x] **GNSS 海拔校准**（0x42 action=2）：由平均气压 + NAV-PVT `hMSL` 反算海平面气压 QNH，之后
      `alt_msl_mm` 为海拔、GNSS 丢失后仍可用；条件 `fixType≥3 / numSV≥8 / pDOP≤2 / 地速<0.3 m/s / vAcc≤20 m`，
      自动基准在 vAcc 可用时也走这条路。
- [x] 基准持久化：独立扇区 17（`Firmware/src/nav/baro_store.c`，20 B 记录 + CRC-16），与标定扇区 16 分开。
- [x] App：`0x04` BARO 遥测解析 + 气压基准卡片（取当前 / 手动输入 / rel_alt 显示）。
- [x] 把 `BARO raw: press=---` 那行换成了 `BARO press_pa=… temp_C=… rel_alt_mm=… press_raw=… temp_raw=…`。

## 4. 验收方式

- 单测：`Firmware/tests/**`（native_sim）。标定算法（min/max、六面法、LSB 权重换算）应写成纯函数并加测试，
  参考 `Firmware/tests/lifegps.nav` 的做法。
- CI：`firmware.yml`（west build + twister + size gate）、`app.yml`（assembleDevDebug + 单测 + lint）、
  `protocol.yml`（`python3 tools/protocol-gen/validate.py` + 生成头 drift）。
- 台架：串口 1 Hz 遥测（`IMU/BODY/TEMP/MAG/BARO/SYS/GNSS` 七行）+ App 传感器页；标定后回读寄存器核对。

## 5. 文件地图

| 关注点 | 文件 |
|---|---|
| IMU/磁力计/气压计驱动 | `Firmware/src/imu/lsm6dsv16x.c`、`Firmware/src/imu/sflp.c`、`Firmware/include/lifegps/imu.h`、`Firmware/include/lifegps/sflp.h` |
| 姿态/重力拆分 | `Firmware/src/nav/attitude.c`、`Firmware/include/lifegps/nav.h` |
| BLE 帧编解码 | `Firmware/src/ble/ble_frame.c`、`Firmware/src/ble/ble_gatt.c`、`Firmware/include/lifegps/ble_frame.h` |
| 命令分发（校准命令要接进这里） | `Firmware/src/ble/ble_gatt.c`（`reply_not_supported()` 一带） |
| 协议契约 | `protocol/lifegps_protocol.yaml`、`Doc/Software/BLE_Wire_v1.md` |
| App 解析/仓库/UI | `App/app/src/main/java/com/lifegps/app/ble/BleFrame.kt`、`.../ble/BleDebugRepository.kt`、`.../debug/SensorStatusScreen.kt` |

## 6. 新会话的第一句话建议

> 读 `Doc/Software/Sensor_Calibration_Plan.md` 与 `AGENTS.md`，
> 先给 P0-A 与 P0-B 的实施方案（含协议负载、寄存器写入序列、持久化分区决策），我逐项批准后再改代码。

## 7. 勘误（2026-11-19 核对后追加）

| 位置 | 原文/原文暗示 | 更正 |
| :--- | :--- | :--- |
| §3 P0-B「固件落点」 | `CTRL9_XL(0x18).USR_OFF_ON_OUT=1` | 寄存器名要按当前器件写：**LSM6DSV16X** 的 `USR_OFF_ON_OUT` = `CTRL9(0x18)` **bit0**、`USR_OFF_W` = 同寄存器 **bit1**（DS13510 §9.22 Table 71）。曾经的 LSM6DSR 版把 `USR_OFF_ON_OUT` 写在 `CTRL7_G(0x16)` bit1、`USR_OFF_W` 写在 `CTRL6_C(0x15)` bit3（DS11976 §9.21/§9.20），**两套位号不能混用**，照抄会静默失效 |
| §1 现状表 | 「U12 = LSM6DSRTR（不是网表写的 LSM6DSV16X）」 | 网表 `Hardware/Schematic/Netlist_Main_V0.2_2026-11-11.tel:50` 写 **`LSM6DSVTR`**；`LSM6DSV16X` 是它的晶圆名。**2026-11 本轮已按装配说明回到 LSM6DSVTR**，驱动重写为 `Firmware/src/imu/lsm6dsv16x.c`（WHO_AM_I 0x70、SPI 读 `0x80 \| reg`、FS 在 CTRL6/CTRL8、30 Hz 采样、SFLP 经 FIFO）。仍保留 `AGENTS.md` §3 的规则：料号有冲突先问 |
| §3 P0-B | 六面法由 App 指定 `position` | 改为**固件按重力向量自动识别姿态**：请求 `position = 0xFF`，响应新增 `detected_face`（`0xFF` = 姿态不够平，不许猜）。字段表见 `Doc/Software/BLE_Wire_v1.md` §6.4 |
| §3 P0-B 持久化 | 「启用 `CONFIG_SETTINGS`+NVS 或复用保留扇区（需单独批准）」 | **仍未决**，待批准；App 侧已按「持久化在设备侧 + `last_calib_unix_s` 时间戳」实现 |
| §3 P0-A / P0-B | 「待做」 | **App 侧已落地**：`CalibCodec.kt`（字节布局）、`CalibModels.kt`（判定/几何）、`CalibrationScreen.kt`（向导）+ 30 个 JVM 用例。固件侧（采集、数学、持久化）仍未开始 |
