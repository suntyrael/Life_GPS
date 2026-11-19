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
| Telemetry | `4C475053-0005-4C47-5053-000000000001` | Notify + CCC | 按传感器分组的二进制包：IMU（含姿态四元数）26 Hz、PDR 每步 |

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
| 24 | i32 | `hmsl_mm` | 海拔（椭球高）mm |
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
| 94 | i16 | `imu_temp_cc` | LSM6DSR 裸片温度，0.01 °C（256 LSB/°C，+25 °C 偏置） |
| 96 | i16 | `mag_temp_cc` | LIS2MDL 裸片温度，0.01 °C（8 LSB/°C，无偏置） |
| 98 | u8 | `sensor_flags` | bit0 重力/线性有效，bit1 IMU 温度有效，bit2 磁力计温度有效，bit3 气压计温度有效 |
| 99 | u8 | `reserved2` | 0 |

**坐标系约定（这是 2026-11-19 修掉的一个真实缺陷）**：
`acc_lsb`/`gyro_lsb` 是**传感器坐标系**的原始计数，供诊断用；
`82..93` 的 `gravity_mg`/`linear_mg` 是**体坐标系**（X 右、Y 前、Z 上），
它与 Telemetry 包 `0x02` 的 `acc_lsb`（也是体坐标系）同框。
2026-11-19 之前的 App 把 1 Hz 的 Status `acc_lsb`（传感器系，静止时 Z=+1 g）
和 26 Hz 的 Telemetry `acc_lsb`（体坐标，静止时 Z=−1 g）写进同一个 UI 字段，
表现为「上一秒 −1 g、下一秒 +1 g」。**主机不得把两种坐标系的加速度混进同一个量**。

没有的物理量（四元数、高度 hPa、单星 C/N0、TTFF、BMP388 的 °C）**不在帧里**，App 不得编造；
`baro_temp_raw` 是未补偿的 24 位原值，换算成 °C 需要读 BMP388 的 TPG 系数，固件尚未实现。

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
| `0x02` IMU | 20 B（整包 24 B） | `i16 acc_lsb[3]`、`i16 gyro_lsb[3]`、`i16 quat_q15[4]`（w,x,y,z，32767 = 1.0） | 26 Hz（仅订阅时） |
| `0x05` PDR | 12 B（整包 16 B） | `i32 x_mm`（东）、`i32 y_mm`（北）、`i16 heading_ddeg`、`u16 steps` | 每完成一步 + 1 Hz 心跳 |

**IMU 包是 body 帧**（X 右 / Y 前 / Z 前脸法向），四元数把 body 映射到 world
（X 东 / Y 北 / Z 上），所以加速度矢量可以直接和姿态一起画。
Status 帧里的 `acc_lsb`/`gyro_lsb`/`mag_lsb` 仍是 **sensor 帧**（既有口径，不在此改动）。

空口预算：26 Hz × 24 B ≈ 620 B/s，30–50 ms 连接间隔下占空比可忽略。
App 侧建议把渲染限流到 15 Hz：26 Hz 的通知照收，但 Compose 每帧重绘没有必要。

航向基准由固件决定，App 不需要复算：GNSS 有效且地速 ≥ 1.5 m/s 时用航迹角对齐
yaw（并顺带学习磁力计偏置），否则用磁力计按该偏置维持 yaw。

## 6. 命令清单（v1）

| opcode | 名称 | 请求 payload | 响应 payload（status 之后） |
| :--- | :--- | :--- | :--- |
| `0x01` | GET_CAPS | — | 见 6.1 |
| `0x02` | GET_STATUS | — | 100 字节 Status 帧 |
| `0x12` | POI_MARK | — | `u8 written`（1 = 已写入轨迹环） |
| `0x30` | LOG_SUBSCRIBE | `u8 level`（0=off 1=ERR 2=WRN 3=INF 4=DBG） | — |
| `0x52` | REBOOT | — | —（响应发出后 200 ms 冷复位，需先 `DEBUG_UNLOCK`） |
| `0x60` | FLASH_INFO | — | 见 6.2 |
| `0x7F` | DEBUG_UNLOCK | 4 字节 ASCII `LGPS` | — |

其余协议 opcode（SET_MODE / SET_TIME / SET_CONFIG / GET_CONFIG / SYNC_* / GET_DAY_INDEX /
GET_ARCHIVE / GNSS_SNAPSHOT / IMU_SNAPSHOT / BUTTON_EVENT / SELFTEST / MAG_CAL /
BARO_BASELINE / ERASE_DAY / FACTORY_RESET / FLASH_READ / FLASH_ERASE / GET_FAULT_LOG /
POWER_TEST_MARK）v1 一律返回 `0x03 NOT_SUPPORTED`；完全不认识的 opcode 返回 `0x02`。
App 必须把这两种情况显示给用户，不得伪造成功。

### 6.1 GET_CAPS 响应（24 字节）

| off | type | 字段 |
| :--- | :--- | :--- |
| 0 | u8 | `ver` = 1 |
| 1 | u8 | `proto_major` / 2 `proto_minor` / 3 `proto_patch`（来自生成头 0.1.0） |
| 4 | u8 | `fw_major` / 5 `fw_minor` / 6 `fw_patch`（v1 = 0.2.0） |
| 7 | u8 | `reserved` |
| 8 | u16 | `att_mtu`（固件配置的 L2CAP TX MTU，247） |
| 10 | u16 | `max_cmd_payload`（235） |
| 12 | u32 | `caps`：bit0 Status notify，bit1 Log notify，bit2 POI，bit3 FLASH_INFO，bit4 REBOOT，bit5 DEBUG_UNLOCK，bit6 Data 同步（v1 = 0） |
| 16 | u32 | `telemetry_mask`（v1 = 0，全部走 Status 帧） |
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

## 7. 版本策略

- `ver` 字节变更 = 不兼容改动，App 必须拒绝连接并在 UI 上说明。
- 只追加字段时提升 `fw_*`、保持 `ver=1`，并把 `status_size` 一起提高；App 按
  `status_size` 决定解析长度，短帧按缺失处理。
- YAML 回填 status/命令载荷后，本文件同步收缩为指针。
