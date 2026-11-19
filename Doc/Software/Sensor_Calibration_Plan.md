# 交接：传感器链路收尾 + 三传感器标定（2026-11-19）

本文件是给**新会话**的上下文入口。读完这一页就能继续工作，不需要翻历史对话。
规则仍以仓库根 `AGENTS.md` 为准（本地零编译、改动需逐项批准、协议只追加字段）。

## 1. 当前状态

| 项 | 值 |
|---|---|
| 分支 | `diag/hub-bmp388`（HEAD `271826e`；`main` 由另一会话推进，未见冲突） |
| CI | `firmware` success（75/75 twister）、`app` success；最近一次 FLASH 241464 B / RAM 55440 B |
| 硬件 | Main_V0.2，U12 = **LSM6DSRTR**（不是网表写的 LSM6DSV16X），U3 = LIS2MDL，U4 = BMP388 |
| **U4(BMP388) 现状** | **I2C 无 ACK**（`0x76`/`0x77` 都试过，`STATUS_MASTER(0x39)` = `nack(s0)=1`）。气压相关功能全部被它挡住，需硬件侧先量 `U4.10`(VDD)/`U4.1`(VDDIO)=1.9 V、`U4.6`(CSB) 应为高、`U4.5`(SDO) 对地导通、确认贴件 |

已落地（不要重复实现）：IMU 采样含裸片温度、重力/线性加速度拆分、磁力计温度、Status 帧 82→100 字节、
App 侧 sensor/body 坐标系分离（±1 g 抖动已修）、hub 状态位解码、诊断收进 Kconfig。

## 2. 必须知道的坑（都踩过）

1. **`numop` 只有 3 bit（0..7）**。写 `0x08` 会落到 `BATCH_EXT_SENS_0_EN`，numop 变 0 → 该从机一个字节都不读，
   且不会报错（`Firmware/src/imu/lsm6dsr.c` 的 `HUB_MAX_NUMOP`）。
2. **排在 NACK 从机后面的 hub 槽位不会被填**。所以 BMP388 必须排在最后（`hub_configure()` 注释）。
3. **LIS2MDL `TEMP_OUT` 有 +25 °C 零点**：`T = raw/8 + 25`（DS12144 §8.16 只给了灵敏度；
   零点见 ST 驱动/AN5069，2026-11-19 台架用 LSM6DSR 裸片温度交叉验证过）。
4. **Status 帧里 `acc_lsb/gyro_lsb` 是传感器系**，`gravity_mg/linear_mg` 与 Telemetry `0x02` 的 `acc_lsb` 是**体坐标系**。
   同一 UI 字段不得跨坐标系写入（这正是 −1 g/+1 g 跳动的根因）。
5. **hub 周期被 IMU data-ready 钳制在 26 Hz**（DS11976 Table 373），`SHUB_ODR` 写 52 Hz 不生效。
6. 温度/气压：BMP388 只有未补偿 24 位原值；换算成 hPa/°C 需要读它的标定系数（`0x30..0x57`，DS 第 4.3.21 节 / 第 9.1 节给了浮点参考实现），**尚未实现**。

## 3. 待做（按优先级）

### P0-A 加速度计刻度标定（小）
现象：静止时 `|acc| ≈ 1.024 g`（偏高约 2.4%），`0.122 mg/LSB` 是标称值。
做法：静止时用 `|acc|` 求一个标量缩放系数；在 firmware 里加 `LGPS_IMU_ACCEL_SCALE`（默认 1.0F），
在 `Firmware/src/imu/lsm6dsr.c` 的 `decode_sample()` 里乘上；同时把陀螺零偏也纳入同一套标定记录。
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
- 磁力计硬铁：LIS2MDL `OFFSET_X/Y/Z_REG_L/H`（`0x45..0x4A`，16 位补码）——`Firmware/src/imu/lsm6dsr.c` 的
  `shub_write_byte()` 一次写一个字节，写 6 个字节即可（注意每写一次要等 hub 周期）。
  软铁/刻度仍需软件处理（旋转采样求各轴 min/max，`offset=(min+max)/2`，`scale=2/(max-min)`）。
- 加速度计零偏：LSM6DSR `X/Y/Z_OFS_USR`（`0x73/0x74/0x75`，主页面直接可写），
  `CTRL6_C(0x15).USR_OFF_W` 选择权重（0 = 2⁻¹⁰ g/LSB），`CTRL9_XL(0x18).USR_OFF_ON_OUT=1` 打开补偿。
  六面法每个姿态采样得到 `offset_axis = -(measured) / 2`，注意符号与 LSB 权重。
- 气压基准：`BARO_BASELINE` 只在 U4 复活后才有意义；先实现"取当前压力为基准 + 相对高度"，
  且必须先把 TPG 补偿（读 `0x30..0x57` 系数、按 BMP388 §3.11 / §9.1 的参考实现）做完。

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
- `IF_CONF(0x1A)` 的 I²C 看门狗超时显式配置（默认 NVM 1.25 ms short）。
- TPG 补偿（`0x30..0x57`）+ 真实 hPa/°C 上报 + `BARO_BASELINE`。
- 把 `BARO raw: press=---` 那行恢复成数值。

## 4. 验收方式

- 单测：`Firmware/tests/**`（native_sim）。标定算法（min/max、六面法、LSB 权重换算）应写成纯函数并加测试，
  参考 `Firmware/tests/lifegps.nav` 的做法。
- CI：`firmware.yml`（west build + twister + size gate）、`app.yml`（assembleDevDebug + 单测 + lint）、
  `protocol.yml`（`python3 tools/protocol-gen/validate.py` + 生成头 drift）。
- 台架：串口 1 Hz 遥测（`IMU/BODY/TEMP/MAG/BARO/SYS/GNSS` 七行）+ App 传感器页；标定后回读寄存器核对。

## 5. 文件地图

| 关注点 | 文件 |
|---|---|
| IMU/磁力计/气压计驱动 | `Firmware/src/imu/lsm6dsr.c`、`Firmware/include/lifegps/imu.h` |
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
| §3 P0-B「固件落点」 | `CTRL9_XL(0x18).USR_OFF_ON_OUT=1` | `USR_OFF_ON_OUT` 在 **`CTRL7_G`(0x16) bit1**（DS11976 Rev2 §9.21 Table 66）。`CTRL9_XL` 只有 DEN_X/Y/Z、DEN_XL_G、DEN_XL_EN、DEN_LH、I3C_disable（§9.23 Table 69，复位值 0xE0）。照原文写会**静默失效**。另：`USR_OFF_W` 在 `CTRL6_C`(0x15) bit3（§9.20 Table 62） |
| §1 现状表 | 「U12 = LSM6DSRTR（不是网表写的 LSM6DSV16X）」 | 网表 `Hardware/Schematic/Netlist_Main_V0.2_2026-11-11.tel:50` 实际写 **`LSM6DSVTR`**；装配件是 LSM6DSRTR。同一颗 IMU 出现三个名字，因此 `AGENTS.md` §3 已改为「网表与实测/装配记录冲突时先问，不按网表静默写代码」 |
| §3 P0-B | 六面法由 App 指定 `position` | 改为**固件按重力向量自动识别姿态**：请求 `position = 0xFF`，响应新增 `detected_face`（`0xFF` = 姿态不够平，不许猜）。字段表见 `Doc/Software/BLE_Wire_v1.md` §6.4 |
| §3 P0-B 持久化 | 「启用 `CONFIG_SETTINGS`+NVS 或复用保留扇区（需单独批准）」 | **仍未决**，待批准；App 侧已按「持久化在设备侧 + `last_calib_unix_s` 时间戳」实现 |
| §3 P0-A / P0-B | 「待做」 | **App 侧已落地**：`CalibCodec.kt`（字节布局）、`CalibModels.kt`（判定/几何）、`CalibrationScreen.kt`（向导）+ 30 个 JVM 用例。固件侧（采集、数学、持久化）仍未开始 |
