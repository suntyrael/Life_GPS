# Life_GPS BLE 线格式 v1（App ↔ 固件联调合同）

> 状态：**v1 已实现**（固件 `Firmware/src/ble/`，App `App/app/src/main/java/com/lifegps/app/ble/`）。
> 本文是 App 与固件之间的**唯一实现口径**；上层的字段/opcode/UUID 仍以
> [`protocol/lifegps_protocol.yaml`](../../protocol/lifegps_protocol.yaml) 为 SSOT，
> 本文只补充 v1 已经落地的 **载荷布局**（那部分 YAML 尚未定义，待 v0.2 回填）。
> 人读入口见 [BLE_Protocol.md](BLE_Protocol.md)。

## 1. 角色与可用性

| 项 | v1 事实 |
| :--- | :--- |
| 设备名 | `LGPS-XXXX`（XXXX = FICR DEVICEID 低 16 位大写十六进制），放在 scan response 里 |
| 广播 | flags + 128-bit Service UUID（AD 包），设备名在 scan response |
| 角色 | 仅 Peripheral；不支持 bonding / 配对（`CONFIG_BT_SMP=n`），Status 可被未绑定链路读取 |
| PHY | 1M 广播；连接后请求 2M + DLE 251 |
| MTU | App 主动 `requestMtu(247)`；固件 `L2CAP TX MTU = 247` |
| 未实现 | Data（同步）、Telemetry、Config、SMP 特征在 v1 **不出现在 GATT 表里**，App 必须按 UUID 发现并按“缺失”处理 |

## 2. GATT 表（v1）

| 特征 | UUID | 属性 | 说明 |
| :--- | :--- | :--- | :--- |
| Service | `4C475053-0001-4C47-5053-000000000001` | — | 主服务 |
| Status | `4C475053-0002-4C47-5053-000000000001` | Read + Notify + CCC | 100 字节固定状态快照（前 82 字节与 v1 完全一致）；有 central 订阅时 10 Hz 推送，无订阅时 1 Hz 仅维持缓存 |
| Control | `4C475053-0003-4C47-5053-000000000001` | Write + WriteWithoutResponse + Notify + CCC | 命令帧请求，响应走 Notify |
| Log | `4C475053-0006-4C47-5053-000000000001` | Notify + CCC | 固件日志转发（UTF-8） |
| Telemetry | `4C475053-0005-4C47-5053-000000000001` | Notify + CCC | 按传感器分组的二进制包：IMU（含姿态四元数）30 Hz、PDR 每步 |

UUID 的 128 位表示与固件 `Firmware/include/lifegps/ble_uuid.h` 中的**小端字节宏**一一对应；
两端一致性由 `Firmware/tests/lifegps.ble_frame` 的单测锁定（解析生成头里的 UUID 字符串
再与小端字节比较）。

## 3. 命令帧（Control）

```
off  size  field
0    1     magic  = 0xA5
1    1     ver    = 0x01
2    1     opcode
3    1     flags  bit0 = 1 表示这是响应；其余位保留（0）
4    2     seq    u16 LE，响应原样回填
6    2     payload_len  u16 LE
8    n     payload
8+n  2     crc16-CCITT-FALSE（poly 0x1021 / init 0xFFFF），覆盖 offset 0..8+n-1
```

- 帧长 = `10 + payload_len`；`payload_len > 235` 或帧长不匹配 → 丢弃且不响应。
- 响应 payload 首字节固定为 `status`，其后是命令数据。
- 请求用 WriteWithoutResponse 时序（不依赖 ATT 确认）；响应只能通过 Control 的 Notify 拿到，
  所以 App **必须在写之前**先订阅 Control。

### 3.1 状态码

| code | 名称 | 含义 |
| :--- | :--- | :--- |
| `0x00` | OK | 成功 |
| `0x01` | INVALID_ARGUMENT | 载荷长度/取值非法 |
| `0x02` | UNKNOWN_OPCODE | 固件不认识这个 opcode |
| `0x03` | NOT_SUPPORTED | opcode 存在于协议但 v1 未实现 |
| `0x04` | BUSY | 上一条命令还没处理完 |
| `0x05` | INTERNAL_ERROR | 内部失败（流程/CRC/存储错误） |
| `0x06` | UNAUTHORIZED | 需要先 `DEBUG_UNLOCK` |

## 4. Status 帧（100 字节，小端，固定偏移）

| off | type | 字段 | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | u8 | `ver` | = 1 |
| 1 | u8 | `flags` | bit0 GNSS 定位有效，bit1 充电中，bit2 ZUPT 锁死，bit3 IMU 有效，bit4 磁力计有效，bit5 气压计有效，bit6 轨迹环就绪，bit7 UTC 有效 |
| 2 | u8 | `gnss_state` | 0 OFF / 1 COLD_START / 2 FIXED / 3 EPHEMERIS_HOLD / 4 BACKOFF |
| 3 | u8 | `fix_type` | UBX fixType（0..5） |
| 4 | u8 | `satellites` | 参与定位的卫星数 |
| 5 | u8 | `key_levels` | bit0 KEY1 按下，bit1 KEY2 按下 |
| 6 | u8 | `soc_pct` | **v1 恒为 0xFF（未知）**，App 由 `bat_mv` 自行换算 |
| 7 | u8 | `reserved0` | 0 |
| 8 | u16 | `pdop_1e2` | pDOP ×100 |
| 10 | u16 | `speed_cms` | cm/s |
| 12 | u16 | `heading_ddeg` | 0.1° |
| 14 | u16 | `reserved1` | 0 |
| 16 | i32 | `lat_e7` | 1e-7°，无效时为 0 |
| 20 | i32 | `lon_e7` | 1e-7°，无效时为 0 |
| 24 | i32 | `hmsl_mm` | 海拔 mm，**平均海平面**（UBX-NAV-PVT `hMSL`，偏移 36），不是椭球高；`0x42 action=2` 用的就是它 |
| 28 | i32 | `bat_mv` | 电池 mV，无效时为 -1 |
| 32 | u32 | `utc_epoch_s` | Life_GPS 2020 epoch 秒，0 = 未知 |
| 36 | u32 | `uptime_ms` | 开机毫秒 |
| 40 | u32 | `gnss_age_ms` | 距上次 NAV-PVT 的毫秒，0 = 尚无 |
| 44 | u32 | `steps` | PDR 累计步数 |
| 48 | i16×3 | `acc_lsb[3]` | 加速度原始 LSB（±4 g，0.122 mg/LSB） |
| 54 | i16×3 | `gyro_lsb[3]` | 角速度原始 LSB（±1000 dps，35 mdps/LSB） |
| 60 | i16×3 | `mag_lsb[3]` | 磁力计原始 LSB（LIS2MDL，1.5 mG/LSB） |
| 66 | u32 | `baro_press_raw` | BMP388 未补偿 24 位原始值（**未标定，不是 hPa**） |
| 70 | u32 | `baro_temp_raw` | BMP388 未补偿原始温度 |
| 74 | u32 | `ring_sector_seq` | 轨迹环当前扇区序号 |
| 78 | u32 | `ring_records` | 轨迹环累计记录数 |
| 82 | i16×3 | `gravity_mg[3]` | **体坐标系**重力方向 ×1000（1 g = 1000 mg），由姿态四元数算出，静止时与 `acc` 同向 |
| 88 | i16×3 | `linear_mg[3]` | **体坐标系**线性加速度 = `acc(body) − gravity`，单位 mg |
| 94 | i16 | `imu_temp_cc` | IMU（LSM6DSV16X）裸片温度，0.01 °C（256 LSB/°C，+25 °C 偏置） |
| 96 | i16 | `mag_temp_cc` | LIS2MDL 裸片温度，0.01 °C（8 LSB/°C，无偏置） |
| 98 | u8 | `sensor_flags` | bit0 重力/线性有效，bit1 IMU 温度有效，bit2 磁力计温度有效，bit3 气压计温度有效 |
| 99 | u8 | `reserved2` | 0 |

**坐标系约定（这是 2026-11-19 修掉的一个真实缺陷）**：
`acc_lsb`/`gyro_lsb` 是**传感器坐标系**的原始计数，供诊断用；
`82..93` 的 `gravity_mg`/`linear_mg` 是**体坐标系**（X 右、Y 前、Z 上），
它与 Telemetry 包 `0x02` 的 `acc_lsb`（也是体坐标系）同框。
2026-11-19 之前的 App 把 1 Hz 的 Status `acc_lsb`（传感器系，静止时 Z=+1 g）
和 30 Hz 的 Telemetry `acc_lsb`（体坐标，静止时 Z=−1 g）写进同一个 UI 字段，
表现为「上一秒 −1 g、下一秒 +1 g」。**主机不得把两种坐标系的加速度混进同一个量**。

没有的物理量（四元数、高度 hPa、单星 C/N0、TTFF、BMP388 的 °C）**不在帧里**，App 不得编造；
`baro_temp_raw` / `baro_press_raw` 仍是未补偿的 24 位原值（**帧布局未变**）。固件已实现 TPG 补偿
（`Firmware/src/imu/bmp388.c`，DS BST-BMP388-DS001-07 §9.2/§9.3），但补偿后的 hPa/°C 目前只进串口遥测；
把它们放进 Status 帧是**协议改动**，需单独批准并升 `version`。

## 5. Log 通知（Log 特征）

```
u8   level  0=ERR 1=WRN 2=INF 3=DBG
u8   src_len
char source[src_len]     ASCII
char message[...]        UTF-8，超长截断（单包 ≤ 240 字节）
```

固件侧由 Zephyr log backend 写入 2 KB 环形缓冲，专用线程每 20 ms 出队并 notify，
因此 `CONFIG_LOG_MODE_IMMEDIATE=y` 的 ISR 上下文也不会直接调用 GATT。

## 5.1 Telemetry 包（Telemetry 特征）

一个包只装一组传感器，避免把 Status 撑成高频宽帧：

```
off type  field
0   u8    id     0x01 GNSS / 0x02 IMU / 0x03 MAG / 0x04 BARO / 0x05 PDR / 0x06 POWER / 0x07 SYSTEM
1   u8    ver    = 1
2   u16   seq    按 id 独立自由计数
4   ...   payload
```

| id | payload | 字段 | 速率 |
| :--- | :--- | :--- | :--- |
| `0x01` GNSS | 42 B（整包 46 B；0.2.0 为 34 B） | 见下方字段表 | 1 Hz |
| `0x02` IMU | 20 B（整包 24 B） | `i16 acc_lsb[3]`、`i16 gyro_lsb[3]`、`i16 quat_q15[4]`（w,x,y,z，32767 = 1.0） | 30 Hz（仅订阅时；LSM6DSV16X ODR，DS13510 Table 52/55） |
| `0x04` BARO | 20 B（整包 24 B） | 见下方字段表 | 1 Hz（仅订阅时） |
| `0x05` PDR | 12 B（整包 16 B） | `i32 x_mm`（东）、`i32 y_mm`（北）、`i16 heading_ddeg`、`u16 steps` | 每完成一步 + 1 Hz 心跳 |
| `0x08` NMEA | 1 B + 文本（整包 ≤ 133 B） | `u8 len`、`char text[len]`（ASCII，`$...*hh`，不含 CR/LF） | 每句一包，仅调试会话开启 NMEA 直通时 |
| `0x09` SAT | 4 B + 5 B×N（整包 ≤ 168 B） | 见下方字段表 | 1 Hz，接收机上电即采集 |

**`0x04` BARO 字段表**（`LGPS_BLE_TELEM_BARO_PAYLOAD` / `BaroTelemetryOffset`，小端）：

| off | type | 字段 | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | u32 | `press_pa` | BMP388 TPG 补偿后的气压，Pa；`press_valid` 为 0 时字段为 0 |
| 4 | i32 | `rel_alt_mm` | 相对基准的高度，mm（ISA：`h = 44330·(1−(p/p0)^0.1902632)`）；无基准时为 0 |
| 8 | i16 | `temp_cc` | 裸片温度，0.01 °C；`temp_valid` 为 0 时字段为 0 |
| 10 | u8 | `flags` | bit0 `press_valid`、bit1 `temp_valid`、bit2 `baseline_valid`、bit3 `baseline_auto`、**bit4 `msl_valid`** |
| 11 | u8 | 保留 | 0 |
| 12 | u32 | `baseline_pa_q4` | 基准气压，Pa × 16；无基准时为 0 |
| 16 | i32 | `alt_msl_mm` | **0.2.0 追加**：海拔（平均海平面），mm；只有 `msl_valid` = 1 时有效，否则为 0 |

`rel_alt_mm` 是**相对基准**的高度（校准那一刻为 0），不是椭球高也不是海拔；没有基准
（`baseline_valid` = 0）时它是 0，主机不得把它当成“海拔 0 m”。

`alt_msl_mm` 才是海拔：当基准是**海平面气压 QNH**（`msl_valid` = 1，来自 §6.5 的 `action=2`
或固件自动校准）时，同一套公式直接给出海拔，GNSS 丢失后仍然可用。主机必须按 `msl_valid` 判断，
不得用 `rel_alt_mm` 冒充海拔。

**兼容性**：`0x04` 的载荷从 16 字节追加到 20 字节，前 16 字节偏移不变；只解 16 字节的旧主机
照常工作（拿不到海拔）。

**`0x01` GNSS 是接收链路诊断，不是定位结果**：定位结果仍在 Status 帧（§4），这里只回答
「模块到底有没有在说话」。payload 布局（小端，固件侧 `LGPS_BLE_TELEM_GNSS_PAYLOAD` 与
App 侧 `GnssTelemetryOffset` 必须一致）：

| off | type | 字段 | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | u8 | `gnss_state` | 0 OFF / 1 COLD_START / 2 FIXED / 3 EPHEMERIS_HOLD / 4 BACKOFF |
| 1 | u8 | `fix_type` | UBX fixType（0..5） |
| 2 | u8 | `satellites` | NAV-PVT `numSV` |
| 3 | u8 | `flags` | bit0 定位有效，bit1 UTC 有效 |
| 4 | u16 | `pdop_1e2` | pDOP ×100 |
| 6 | u32 | `age_ms` | 距上次 NAV-PVT 的毫秒，0 = 尚无 |
| 10 | u32 | `rx_bytes` | UARTE20 收到的字节数（含 NMEA） |
| 14 | u32 | `ubx_frames` | 校验正确的完整 UBX 帧数 |
| 18 | u32 | `ubx_bad_checksum` | 校验错误的帧数 |
| 22 | u32 | `nmea_starts` | 收到的 `'$'` 个数（>0 且 `ubx_frames` 不上涨 = 只吐 NMEA） |
| 26 | u32 | `rx_dropped` | ISR 因环形缓冲满而丢弃的字节 |
| 30 | u32 | `baud` | UART 当前实际波特率（38400 起步，快链路下 460800） |
| 34 | u32 | `ttff_ms` | **0.3.0 追加**：本次上电到首个 3D 定位的毫秒数，0 = 还没定上 |
| 38 | u32 | `power_off_ms` | **0.3.0 追加**：本次上电前轨道断电多久；`0xFFFFFFFF` = 未知（复位后首次） |

**兼容性**：`0x01` 的载荷从 34 字节追加到 42 字节，前 34 字节偏移不变；只解 34 字节的旧主机
照常工作（拿不到 TTFF）。App 侧以 34 为最小长度、42 才读新字段。

TTFF 从 `GPS_EN` 拉高那一刻起算，不是从收到第一帧 UBX 起算；`power_off_ms` 是把一个裸数字变成
诊断信息的关键：断电 3 分钟后的 4 s 是热启动（V_BCKP 保住了历书），断电两小时后的 30 s 等于冷启动。

**`0x09` SAT 字段表**（`u8 count`、`u8 total`、`u16 reserved`，随后每星 5 字节）：

| off | type | 字段 | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | u8 | `count` | 本包内的卫星行数（≤ 32） |
| 1 | u8 | `total` | 接收机报告的卫星总数，与 `count` 不同表示被截断 |
| 2 | u16 | `reserved` | 0 |
| 4 + n·5 | u8 | `gnss_id` | 0 GPS / 1 SBAS / 2 GAL / 3 BDS / 5 QZSS / 6 GLO |
| +1 | u8 | `sv_id` | 卫星号 |
| +2 | u8 | `cno` | C/N0，dBHz |
| +3 | i8 | `elev` | 仰角，度 |
| +4 | u8 | `state` | bit2..0 = UBX `qualityInd`（0 无信号 / 1 搜索 / 2 已捕获 / 3 不可用 / 4..7 跟踪），bit3 = `svUsed` |

方位角、星历可用位、健康位**不在本版本**，App 显示 “—”，不补零。
`0x08` / `0x09` 只在调试会话期间有流量；会话关闭后接收机不再输出 NAV-SAT，也没有 NMEA 直通。

`GET_CAPS.telemetry_mask`（§6.1 off 16）的 bit n 表示 **id = n 的遥测组已实现**：
bit1 GNSS、bit2 IMU、bit3 MAG、bit4 BARO、bit5 PDR、bit6 POWER、bit7 SYSTEM、bit8 NMEA、bit9 SAT（bit0 未用）。
当前固件实现 bit1|bit2|bit4|bit5|bit8|bit9（`0x336`），主机必须按位判断，不得按协议版本号推测。

**IMU 包是 body 帧**（X 右 / Y 前 / Z 前脸法向），四元数把 body 映射到 world
（X 东 / Y 北 / Z 上），所以加速度矢量可以直接和姿态一起画。
Status 帧里的 `acc_lsb`/`gyro_lsb`/`mag_lsb` 仍是 **sensor 帧**（既有口径，不在此改动）。

空口预算：30 Hz × 24 B ≈ 720 B/s，30–50 ms 连接间隔下占空比可忽略。
App 侧建议把渲染限流到 15 Hz：30 Hz 的通知照收，但 Compose 每帧重绘没有必要。

航向基准由固件决定，App 不需要复算：GNSS 有效且地速 ≥ 1.5 m/s 时用航迹角对齐
yaw（并顺带学习磁力计偏置），否则用磁力计按该偏置维持 yaw。

## 6. 命令清单（v1）

| opcode | 名称 | 请求 payload | 响应 payload（status 之后） |
| :--- | :--- | :--- | :--- |
| `0x01` | GET_CAPS | — | 见 6.1 |
| `0x02` | GET_STATUS | — | 100 字节 Status 帧 |
| `0x12` | POI_MARK | — | `u8 written`（1 = 已写入轨迹环） |
| `0x30` | LOG_SUBSCRIBE | `u8 level`（0=off 1=ERR 2=WRN 3=INF 4=DBG） | — |
| `0x41` | MAG_CAL | `u8 action`（6.3） | 见 6.3 |
| `0x42` | BARO_BASELINE | `u8 action` + `u32 baseline_pa_q4`（6.5） | 见 6.5（action 0 取当前 / 1 手动 / 2 GNSS 海拔；BMP388 无气压时返 `0x03`） |
| `0x43` | ACCEL_CAL | `u8 action` + `u8 position`（6.4） | 见 6.4 |
| `0x35` | GNSS_CFG | `u8 action` + `u32 key` (+ value)（6.6） | 见 6.6 |
| `0x36` | GNSS_DEBUG | `u8 flags` + `u16 ttl_s`（6.7） | 见 6.7 |
| `0x52` | REBOOT | — | —（响应发出后 200 ms 冷复位，需先 `DEBUG_UNLOCK`） |
| `0x60` | FLASH_INFO | — | 见 6.2 |
| `0x7F` | DEBUG_UNLOCK | 4 字节 ASCII `LGPS` | — |

其余协议 opcode（SET_MODE / SET_TIME / SET_CONFIG / GET_CONFIG / SYNC_* / GET_DAY_INDEX /
GET_ARCHIVE / GNSS_SNAPSHOT / IMU_SNAPSHOT / BUTTON_EVENT / SELFTEST / ERASE_DAY /
FACTORY_RESET / FLASH_READ / FLASH_ERASE / GET_FAULT_LOG / POWER_TEST_MARK）一律返回
`0x03 NOT_SUPPORTED`；完全不认识的 opcode 返回 `0x02`。App 必须把这两种情况显示给用户，不得伪造成功。

三个校准 opcode（`0x41` / `0x42` / `0x43`）的载荷已经冻结（6.3~6.5），但**固件侧的采集与
持久化尚未实现**：固件返回 `0x03` 之前 App 只显示“固件未提供”，不发假数据。能力一律从
GET_CAPS 的 `caps` 位读（6.1 的 bit7~bit9），**不按协议版本号猜测** —— 一个固件可以会说
这个 opcode 但拒绕保存，两者是两件事。

### 6.1 GET_CAPS 响应（24 字节）

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u8 | `ver` = 1 |
| 1 | u8 | `proto_major` / 2 `proto_minor` / 3 `proto_patch`（来自生成头；0.2.0 只追加了 `0x43` 与校准响应字段） |
| 4 | u8 | `fw_major` / 5 `fw_minor` / 6 `fw_patch`（v1 = 0.3.0） |
| 7 | u8 | `reserved` |
| 8 | u16 | `att_mtu`（固件配置的 L2CAP TX MTU，247） |
| 10 | u16 | `max_cmd_payload`（235） |
| 12 | u32 | `caps`：bit0 Status notify，bit1 Log notify，bit2 POI，bit3 FLASH_INFO，bit4 REBOOT，bit5 DEBUG_UNLOCK，bit6 Data 同步（v1 = 0），bit7 MAG_CAL，bit8 ACCEL_CAL，bit9 BARO_BASELINE（三位置位条件见 6.3~6.5），bit10 GNSS_DEBUG，bit11 GNSS_CFG |
| 16 | u32 | `telemetry_mask`（v1 = `0x26`：bit1 GNSS、bit2 IMU、bit5 PDR，语义见 §5.1） |
| 20 | u16 | `status_size` = 100 |
| 22 | u16 | `reserved` |

### 6.2 FLASH_INFO 响应（20 字节）

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u32 | `flash_bytes`（SFDP 实测） |
| 4 | u32 | `ota_slot1_bytes` |
| 8 | u32 | `traj_bytes` |
| 12 | u32 | `sector_seq` |
| 16 | u32 | `records_written` |
| 20 | u8 | `records_in_sector` / 21 `erase_count_max` / 22 `sector_count` / 23 `reserved`（共 24 字节） |

### 6.3 MAG_CAL 请求 / 响应（`0x41`）

请求 1 字节：

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u8 | `action`：0 START，1 FINISH_SAVE（计算并写入记录），2 CLEAR（删除记录），3 QUERY（无副作用，用于进度与重连后重同步），4 CANCEL（中止采集，不动已存记录） |

响应体（status 之后，30 字节，小端）：

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u16 | `progress_permille`（0..1000） |
| 2 | u8 | `sample_count`（饱和到 255） |
| 3 | u8 | `coverage_pct_x` / 4 `coverage_pct_y` / 5 `coverage_pct_z`（0..100） |
| 6 | i16 | `bias_x_lsb` / 8 `bias_y_lsb` / 10 `bias_z_lsb`（LIS2MDL 原始 LSB） |
| 12 | i32 | `scale_x_ppm` / 16 `scale_y_ppm` / 20 `scale_z_ppm` |
| 24 | u8 | `calib_state`：0 IDLE，1 COLLECTING，2 RESULT_PENDING |
| 25 | u8 | `calib_flags`：bit0 系数来自记录，bit1 用的是编译期默认值，bit2 单面快速，bit3 已生效但落盘失败 |
| 26 | u32 | `last_calib_unix_s`（0 = 设备尚未同步 UTC，不代表没校准过） |

**系数在固件里算**：`(raw - bias) × (1 + ppm/1e6)`。App 只做显示口径的换算（LSB → µT，
0.15 µT/LSB），不得自己积分 min/max。`coverage_pct_*` 表示该轴范围被走了多少，供三轴进度条用；
不足 80 % 时 App 不提供“完成并计算”，固件也应以 `0x01 INVALID_ARGUMENT` 拒绝覆盖不足的采集。

### 6.4 ACCEL_CAL 请求 / 响应（`0x43`）

请求 2 字节：

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u8 | `action`：同 6.3 的 0..4 |
| 1 | u8 | `position`：`0xFF` = **自动识别**（默认，App 永远发这个）；0..5 = 强制指定某面（仅台架）；6 = 单面快速 |

**姿态由固件识别，不由用户选。** 定义：`position p` 表示“哪条 body 轴朝上”——
`p = 2×axis + (分量 > 0 ? 0 : 1)`，axis 0/1/2 = X/Y/Z；静止时加速度计在该轴读 +1 g。
判据与 App 的预测实现一致：最大轴 |g| > 0.7 g 且另两轴 |g| < 0.45 g；不满足就**不许猜**，
`detected_face` 回 `0xFF`，本次不置 `face_mask` 位。

响应体（status 之后，48 字节，小端）：

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u16 | `progress_permille` |
| 2 | u8 | `face_mask`：bit n = 第 n 面已采到有效样本 |
| 3 | u8 | `face_state`：0 WAIT_REST，1 COLLECTING，2 FACE_DONE，3 JITTER_TIMEOUT |
| 4 | u8 | `rest_pct`（0..100，当前有多稳） |
| 5 | u8 | `detected_face`（0..5；`0xFF` = 没识别出来） |
| 6 | i32 | `bias_x_ug` / 10 `bias_y_ug` / 14 `bias_z_ug`（1 g = 1 000 000 µg） |
| 18 | i32 | `scale_x_ppm` / 22 `scale_y_ppm` / 26 `scale_z_ppm` |
| 30 | i32 | `gyro_bias_x_mdps` / 34 / 38 |
| 42 | u8 | `calib_state` |
| 43 | u8 | `calib_flags`（同 6.3；bit2 = 单面快速） |
| 44 | u32 | `last_calib_unix_s` |

零偏存 µg 而不存传感器寄存器单位是故意的：`X/Y/Z_OFS_USR`（0x73~0x75，DS11976 §9.62~9.64）
是 int8、单位 2⁻¹⁰ g，量程仅 ±124 mg、分辨率 0.98 mg，装不下回读校验所需的精度。是否把它
折算并写进传感器寄存器是固件内部的实现选择，不影响本协议。

### 6.5 BARO_BASELINE 请求 / 响应（`0x42`）

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u8 | `action`：0 = 取当前气压为基准，1 = 手动设置，**2 = 用当前 GNSS 海拔校准**（`u32` 忽略，填 0） |
| 1 | u32 | `baseline_pa_q4`（Pa × 16，仅 `action=1` 时有效） |

响应 8 字节：`u32 baseline_pa_q4`、`i32 rel_alt_mm`。

**前置条件**：BMP388 必须应答，且固件已完成 TPG 补偿（系数 `0x31..0x45`）。没有 Pa 就没有
相对高度 —— 在此之前该 opcode 返回 `0x03`，App 只展示只读说明，不给可点按钮。数据范围之外的
`baseline_pa_q4`（30000..125000 Pa）返 `0x01`。

**基准的三种来法**（都是同一个 `u32 baseline_pa_q4`）：

1. `action=0`：把**当前**补偿后的气压写成基准（App 上“取当前为基准”按钮）；
2. `action=1`：手动输入（App 上输入 hPa，发 `Pa × 16`）；
3. **自动**：GNSS 条件满足时由固件自己设（不需要 App 参与）：`fixType ≥ 3`、`numSV ≥ 8`、
   已知 pDOP ≤ 2.0、地速 < 0.3 m/s，连续 30 s 且该窗口气压标准差 < 0.3 hPa；已有基准时
   只有差 > 1 hPa 才重设（防抖）。

**基准有两种含义**（这决定了 `0x04` 的 `msl_valid`）：

| 方式 | 存进去的压力 | 高度含义 |
| :--- | :--- | :--- |
| `action=0/1` | 你站的地方的气压 | **相对**：校准点是 0，只有 `rel_alt_mm` |
| `action=2` | 由气压 + GNSS 高度反算的**海平面气压 QNH** | **绝对**：`alt_msl_mm` 是海拔，`rel_alt_mm` 是相对校准点 |

`action=2` 的条件（固件判定，不满足返 `0x01` INVALID_ARGUMENT）：`fixType ≥ 3`、`numSV ≥ 8`、
已知 pDOP ≤ 2.0、地速 < 0.3 m/s，且 NAV-PVT `vAcc` ≤ 20 m —— 一条 ±30 m 的高度没法校准一台
分辨率 0.8 m 的气压计，固件宁可拒绝。计算用最近约 10 s 的平均气压（单点噪声 1 Pa ≈ 8 cm）。

基准**掉电不丢**：存在外部 NOR 的独立扇区（`CONFIG_LGPS_BARO_SECTOR_INDEX`，默认 17，
`Firmware/src/nav/baro_store.c`，20 字节记录 + CRC-16，含记录版本与 `kind` 位），与标定记录
扇区 16 分开，所以“清除校准”不会连带丢掉气压基准。`0x04` 遥测的 `baseline_auto` 位告诉 App
这个基准是自动还是手设置；没有“清除基准”命令，要清就是重新设一个新基准。

### 6.6 GNSS_CFG（`0x35`）请求 / 响应

```
请求  u8 action (0=GET 1=SET 2=SET+FLASH) · u32 key · value[len]（GET 时无 value）
响应  u8 status · u32 key · u8 len · value[len]      ← 永远是接收机回读到的值
```

- `action=1` 写入接收机 **RAM + BBR** 层（`CFG-VALSET layers = ram|bbr`）：跨 `RXM-PMREQ`
  断电保留（集成手册 §3.6.3.2 说明软件待机清 RAM），但整机断 `V_BCKP` 后回到出厂值。
- `action=2` 额外写 **Flash** 层，消耗一次擦写，必须由用户显式选择。
- 状态码：`0x01` 键不在白名单或值超范围，`0x03` 接收机 NAK 该键，`0x04` 模组当前未上电 /
  未应答（等一个电源周期后重试），`0x00` 成功。
- 一次写是**四步**，不是一帧：`CFG-VALSET` → 等 `UBX-ACK` → 若 key 属于 `CFG-SIGNAL`
  （组 id `0x31`）再等 **0.5 s** → `CFG-VALGET` 回读。集成手册 §2.1.3 与接口说明书 §4.9.21：
  "Any change to the signal configuration items triggers a restart of the GNSS subsystem …
  wait for message acknowledgement and a margin of 0.5 seconds prior to sending any further
  commands"。旧实现把 VALSET 和 VALGET 背靠背发出，VALGET 落在重启窗口里，回读要么收不到
  要么被 NAK——App 因此把健康的写入显示成「不支持」。
- 因此命令最坏会阻塞到 ~2.4 s（`GNSS_CFG_SET_WAIT_MS`），App 侧对应 4 s 预算。

白名单（固件 `Firmware/src/gnss/gnss.c` 与 App `GnssCfgKeys` 必须一致）：

| key | 名称 | 类型 | 允许范围 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| `0x10230001` | CFG-ANA-USE_ANA | U1 | 0..1 | AssistNow Autonomous（预测星历存 BBR） |
| `0x30230002` | CFG-ANA-ORBMAXERR | U2 | 1..65535 | 预测星历最大误差，m |
| `0x20110021` | CFG-NAVSPG-DYNMODEL | E1 | 0..10 | 动态模型 0 便携 / 2 静止 / 3 步行 / 4 车载 … |
| `0x1031001f` | CFG-SIGNAL-GPS_ENA | L | 0..1 | GPS 星座 |
| `0x10310021` | CFG-SIGNAL-GAL_ENA | L | 0..1 | Galileo |
| `0x10310022` | CFG-SIGNAL-BDS_ENA | L | 0..1 | 北斗星座 |
| `0x1031000d` | CFG-SIGNAL-BDS_B1_ENA | L | 0..1 | 北斗 B1I 信号 |
| `0x1031000f` | CFG-SIGNAL-BDS_B1C_ENA | L | 0..1 | 北斗 B1C 信号 |
| `0x10310025` | CFG-SIGNAL-GLO_ENA | L | 0..1 | GLONASS 星座 |
| `0x10310018` | CFG-SIGNAL-GLO_L1_ENA | L | 0..1 | GLONASS L1OF 信号 |
| `0x201100a4` | CFG-NAVSPG-INFIL_MINELEV | I1 | 0..90 | 最小仰角，度 |
| `0x201100a3` | CFG-NAVSPG-INFIL_MINCNO | U1 | 0..60 | 参与解算的最小 C/N0，dBHz |
| `0x301100b1` | CFG-NAVSPG-OUTFIL_PDOP | U2 | 1..9999 | pDOP 门限 ×0.1 |
| `0x30210001` | CFG-RATE-MEAS | U2 | 25..1000 | 导航频率，ms |
| `0x30210002` | CFG-RATE-NAV | U2 | 1..127 | 测量次数 / 解算次数 |
| `0x20d00001` | CFG-PM-OPERATEMODE | E1 | 0..2 | 0 FULL / 1 PSMOO / 2 PSMCT |
| `0x40d00002` | CFG-PM-POSUPDATEPERIOD | U4 | 5..604800 | PSMOO 定位周期，s |
| `0x40d00003` | CFG-PM-ACQPERIOD | U4 | 0..604800 | 搜星周期，s |
| `0x30d00005` | CFG-PM-ONTIME | U2 | 0..65535 | 跟踪时长，s |
| `0x10d00008` | CFG-PM-DONOTENTEROFF | L | 0..1 | 搜星失败时不进入 Inactive |
| `0x20250038` | CFG-MOT-GNSSSPEED_THRS | U1 | 0..255 | 静止判定速度 ×0.01 m/s（0 = 出厂默认） |
| `0x3025003b` | CFG-MOT-GNSSDIST_THRS | U2 | 0..65535 | 静止判定距离，m（0 = 出厂默认） |

**星座组合是有约束的**，不是七个独立开关（数据手册 §1.3、集成手册 §2.1.3）：
MAX-M10S 出厂为 GPS + Galileo + 北斗 B1I；`B1I` 不能与 `B1C` 或 `GLONASS L1OF` 同时开启，
非法组合直接回 `UBX-ACK-NAK`（同时发 `UBX-INF` / NMEA-TXT 警告 `inv sig cfg`）。
所以开 GLONASS 必须按序：`BDS_B1_ENA=0` → `BDS_B1C_ENA=1` → `GLO_ENA=1` / `GLO_L1_ENA=1`。
合法的四星座组合是 GPS + GAL + 北斗 B1C + GLONASS（B1C 与 GPS L1 同频）。

`CFG-UART1-BAUDRATE`、`CFG-UART1OUTPROT-*` 等**不在白名单**：写错会让链路失联，
调试功能不允许具备这种能力。固件自己在 `CONFIG_LGPS_GNSS_FAST_LINK` 下写这两项（见 §6.8），
并且波特率只写 RAM 层，所以下一个电源周期仍然从 38400 起步。

### 6.7 GNSS_DEBUG（`0x36`）请求 / 响应

```
请求  （空）= 查询 ；或 u8 flags · u16 ttl_s（小端，0 = 默认 300 s）
响应  u8 status · u8 flags · u16 remaining_s
```

| flags 位 | 含义 |
| :--- | :--- |
| bit0 | `HOLD_POWER`：会话期间跳过冷启动 60 s 超时断电与星历保持 35 s 断电 |
| bit1 | `NAV_SAT`：**0.3.0 起无作用**。NAV-SAT 现在是接收机上电后的常开输出（1 Hz，RAM 层，
每次上电重新打开），不再是会话专属。位仍接受，以便旧版 App 不改也能用 |
| bit2 | `NMEA`：把原始 NMEA 语句通过 `0x08` 遥测转发（若 `CONFIG_LGPS_GNSS_FAST_LINK` 已把
NMEA 输出关掉，会话期间会临时重新打开，会话结束再关掉） |

- 会话一定有寿命上限（默认 300 s，最大 3600 s）：没有任何 BLE 命令能让 GNSS 轨道无限期上电。
  唯一的例外是编译期开关 `CONFIG_LGPS_GNSS_ALWAYS_ON`（`boards/lifegps/lifegps/Kconfig.lifegps`，
  默认 `n`），开了之后整条超时断电策略被整段跳过——那是台架调试用的构建选项，蓝牙侧无法触及。
- 卫星表不再需要会话：`publish_sat()` 一直在发 `0x09`，只要接收机上电就有数据；表为空只可能是
  模组未上电、周视无星，或接收机 NAK 了 NAV-SAT 输出率。
- `NMEA` 只写 RAM 层，断电即失效；`flags=0` 立即恢复常规省电策略。
- 写入响应回显**请求的** flags/ttl（GNSS 线程在一个循环周期内应用），
  约 100 ms 后再查询一次即可读到真实剩余时间。

### 6.8 链路速率、10 Hz 画像与失联自救（固件侧，无报文）

每次上电两侧都从 **38400 8N1** 起步（设备树 `current-speed` = `LGPS_GNSS_BAUD_DEFAULT`），
并且 **NAV-SAT 一直开着**（1 Hz，RAM 层，每次上电重写），卫星表不再依赖调试会话。
`CONFIG_LGPS_GNSS_FAST_LINK=y` 时，链路在 NAV-PVT 回读确认之后按画像抬升，`CFG-RATE-MEAS`
与 `CFG-UART1OUTPROT-NMEA` 两条都等 ACK，**只有波特率那一条不等**：

1. `CFG-UART1OUTPROT-NMEA = 0`（RAM）：出厂 NMEA 集每历元约 1.25 kB，10 Hz 下 12 kB/s（等 ACK）
2. `CFG-RATE-MEAS = CONFIG_LGPS_GNSS_RATE_MS`（RAM+BBR，默认 100 ms，等 ACK）
3. `CFG-UART1-BAUDRATE = 460800`（仅 RAM）→ **不等 ACK** → 等 100 ms → MCU 侧切到 460800

第 3 条不等 ACK 是故意的：接收机把这条设置在生成答复的同时就生效了，那个 ACK 可能已经是按
新速率发出来的，对一个还在 38400 上监听的主机是永远等不到的。集成手册 §2.1.4 只要求“发完
改速率的消息后等约 100 ms 再按新速率发数据”。旧写法把这一条也放在 ACK 等待里，结果就是
**接收机已经在 460800、主机还停在 38400**——两边各说各话。现在改成发完即走，切换后用
NAV-PVT 回读验证新速率。

失败处理只有一条路：**给接收机断电重启**。新速率在它的 RAM 层，主机单方面退回 38400 仍然
还是对着一个不应答的接收机；而 `RXM-PMREQ` 待机会清 RAM，所以断电重上电后它必然回到 38400。
触发条件有两个：主机一侧 `uart_configure(460800)` 失败（此时接收机已经切过去了），或切换后
10 s 内收不到 NAV-PVT 回读。修完之后本次开机不再尝试快链路（`fast_link_broken` 锁存），
以免在 38400/460800 之间反复抖动。

依据：MAX-M10S 集成手册表 16 列出 460800；nRF54L15 UARTE 数据手册 §11.24 给出普通实例
`fUARTE` 上限 1000 kbps，`BAUDRATE=0x07400000` 在 16 MHz PCLK 下实际 457143 bit/s（-0.8 %）。

带宽预算（8N1 = 10 bit/字节，460800 ≈ 46 kB/s）：

| 10 Hz 输出 | 每历元 | 每秒 |
| :--- | ---: | ---: |
| NAV-PVT | 100 B | 1.0 kB/s |
| + NAV-SAT(32) | 400 B | 5.0 kB/s |
| + NAV-SIG(40 信号) | 656 B | 11.6 kB/s |
| + 出厂 NMEA 集 | 1250 B | 24 kB/s（**只在 460800 下可行**；38400 上限 3.84 kB/s）|


#### 6.8.1 接收机失联自救

有些设置会让接收机「安静」下来，而安静的形态很多：功耗模式在自己的休眠期关掉 UART、星座组合让
GNSS 子系统反复重启、速率被接受但撑不住。它们的共同点就是**没有完整 UBX 帧**，所以固件看的就是这个：

- 在 `CONFIG_LGPS_GNSS_ALWAYS_ON`（省电策略关闭，没有别的机制会把接收机拉回来）下，接收机应在上电
  状态却连续 **20 s** 没有任何完整 UBX 帧 → 触发自救，最多 **3 次**；拿到一次有效 3D 定位就清零计数。
- 自救的顺序是：打印最近 4 次 GNSS_CFG 写入（key / value / 返回值）→ 发 `UBX-CFG-VALDEL`
  删除 7 个可能「存进 BBR 把模组锁死」的键（`CFG-PM-OPERATEMODE`、`CFG-PM-POSUPDATEPERIOD`、
  `CFG-SIGNAL-GLO_ENA`、`GLO_L1_ENA`、`BDS_B1_ENA`、`BDS_B1C_ENA`、`CFG-RATE-MEAS`）→ 给接收机断电。
- 为什么要 VALDEL：这些写入落在 **RAM + BBR** 层，而 BBR 由常供的 `V_BCKP` 保持，
  **断电清不掉**。只有删除保存值，下一次配置加载才会回到出厂组合（接口说明书 §3.10.3.1：
  CFG-VALDEL 不校验结果是否合法）。
- 3 次仍不响 → 打一条 ERRO 后不再折腾，避免电池设备每 20 s 断一次电。
- 已知功耗模式（`CFG-PM-OPERATEMODE != 0`）在本次开机内会关闭这条看门狗：PSMOO/PSMCT 的休眠期
  本来就不发数据，不能当死机处理。

## 7. 版本策略

- `ver` 字节变更 = 不兼容改动，App 必须拒绝连接并在 UI 上说明。
- **追加 opcode 不升 `ver`**：0.2.0 只追加了 `0x43` 与三组校准载荷，`ver` 保持 1。
- 只追加字段时提升 `proto_*` / `fw_*`，并把 `status_size` 一起提高；App 按
  `status_size` 决定解析长度，短帧按缺失处理。
- YAML 回填 status/命令载荷后，本文件同步收缩为指针。
