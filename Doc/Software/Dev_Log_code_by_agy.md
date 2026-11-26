# Life_GPS 开发日志（`code_by_agy` 分支）

> 本文件记录 `code_by_agy` 分支上按架构重构蓝图分阶段推进（M0–M5）的设计决策、代码改动、自检与 GitHub Actions 验证结果。
> 约束基线：
> - 仅在 `code_by_agy` 分支提交，绝不修改其他分支；
> - 外部 NOR Flash 必须同时兼容 **32 MB 与 64 MB**（通过 SFDP 运行期动态探测容量与分区边界，不硬编码容量）；
> - 个人自用口径：仅做 BLE 配对/绑定 + 链路加密，不做应用层认证与 Flash 静态加密；
> - App 全链路走 BLE（移除 USB-C 串口直连），`dev`/`prod` 双 flavor 隔离；固件保留 `prod`（低功耗默认）+ `debug` overlay 双配置；
> - 所有硬件功能已在 V0.2 实板调试通过。

---

## 阶段总览

| 阶段 | 主题 | 状态 |
| :--- | :--- | :--- |
| **M0** | 协议 SSOT 补全、跨端代码生成（C + Kotlin + Golden Vectors）与高危止血修债 | ✅ 已完成（CI 全绿） |
| **M1** | 固件去轮询事件化 + 双配置骨架；App 纯 JVM 核心模块 + `LifeGpsTransport` + `dev`/`prod` 双 Flavor 隔离 | ✅ 已完成（CI 全绿） |
| **M2** | 固件超低功耗 4 态电源状态机 + IMU 运动唤醒 + 导航补盲（磁异常/Weinberg/楼层/乘车）+ 周期轨迹落盘 | 🚧 已完成编码，CI 验证中 |
| **M3** | BLE 配对绑定 + 完整 6 特征（Data/Config）+ 夜间无加密原子归档（Byte-Delta+Heatshrink+Journal）+ App 双通道同步与 Room 本地库 | 待启动 |
| **M4** | Android `prod` 产品终端（MapLibre 地图 + WGS-84/GCJ-02 渲染开关 + 时间线/统计 + POI + GPX/KML/GeoJSON/CSV 导出 + 设置/灯语帮助） | 待启动 |
| **M5** | MCUboot + SMP over BLE OTA 闭环（兼容 32 MB / 64 MB Flash 策略）+ 发布固化 | 待启动 |

---

## 2026-11-25 · Milestone 0：协议 SSOT 补全与高危止血修债

### 关键设计决策
1. **协议零破坏追加**：保持既有 `0xA5`/`0x5A` 帧、32 B `TrajectoryRecord_t`、48 B+8 B `LGD1` 日包、9 个 UUID、Status 前 116 B 与错误码 `0x00..0x06` 完全不变；追加状态码 `0x07..0x0C`、能力位 `bit12..14`、Config TLV `0x01..0x0B`、Scan-Response 10 B Manufacturer Data、`Status` 116→128 B 同步扩展及 `LGD1` 压缩参数（`heatshrink` `window_sz2=8, lookahead_sz2=4`、`traj_crc32` 覆盖解压后原始轨迹流）。
2. **双端唯一真相源**：新增 `gen_kt.py` 与 `gen_vectors.py`，`gen_c.py` 补充生成全部 9 个 `LGPS_UUID_*_LE` 字节宏，彻底消除 C 侧 `ble_uuid.h` 与 Kotlin 侧 `BleUuids.kt` / `CapsBits` / `StatusCode` 的手抄漂移。
3. **32 MB / 64 MB Flash 兼容原则**：所有存储区计算继续严格基于 SFDP 运行期探测值，并在 M5 OTA 分区设计阶段专门采用与 32 MB/64 MB 容量兼容的布局方案（避免固定写死 64 MB 尾部偏移）。

### 变更清单
- **协议与工具链**：
  - 修改 [lifegps_protocol.yaml](../../protocol/lifegps_protocol.yaml)、[validate.py](../../tools/protocol-gen/validate.py)、[gen_c.py](../../tools/protocol-gen/gen_c.py)、[protocol.yml](../../.github/workflows/protocol.yml)、[Makefile](../../Makefile)
  - 新增 [schema.py](../../tools/protocol-gen/schema.py)、[gen_kt.py](../../tools/protocol-gen/gen_kt.py)、[gen_vectors.py](../../tools/protocol-gen/gen_vectors.py)
  - 生成 [lifegps_protocol.h](../../Firmware/include/generated/lifegps_protocol.h)、[GeneratedProtocol.kt](../../App/core/protocol/src/main/kotlin/com/lifegps/core/protocol/GeneratedProtocol.kt)（M1 迁入 `:core:protocol`）、9 个跨端共享向量 [manifest.json](../../protocol/vectors/manifest.json) 与 [generated_vectors.h](../../Firmware/tests/lifegps.protogen/src/generated_vectors.h)
- **固件止血修债**：
  - [ble_uuid.h](../../Firmware/include/lifegps/ble_uuid.h) 委托生成头；[ble_frame.h](../../Firmware/include/lifegps/ble_frame.h) 状态码/能力位对齐生成常量
  - [ble_gatt.c](../../Firmware/src/ble/ble_gatt.c)：`command_handler()` 响应缓冲扩容至完整 235 B 并改为 `static`（省 ~600 B 线程栈）；`disconnected()` 复零 `debug_unlocked = false`
  - [lifegps.ble_frame/src/main.c](../../Firmware/tests/lifegps.ble_frame/src/main.c) 补齐全部 9 个 UUID 校验；[lifegps.protogen/src/main.c](../../Firmware/tests/lifegps.protogen/src/main.c) 接入真实 `crc.c`/`traj.c` 校验 Golden Vectors
- **Android App 修债与交叉单测**：
  - [BleUuids.kt](../../App/app/src/main/java/com/lifegps/app/ble/BleUuids.kt) 委托 `GeneratedProtocol.Uuids` 并补齐 `DATA`/`CONFIG`/`SMP_*`
  - [BleFrame.kt](../../App/app/src/main/java/com/lifegps/app/ble/BleFrame.kt) 补全 `CapsBits`、`StatusCode (0x07..0x0C)`、`crc8()`、`crc32IsoHdlc()`，修复 `parseSatTelemetry()` 对 7 颗旧版 5 B 星（35 B）的行宽误判
  - [build.gradle.kts](../../App/app/build.gradle.kts) 挂载 `protocol/vectors` 为测试资源，新增 [GoldenVectorsTest.kt](../../App/app/src/test/java/com/lifegps/app/ble/GoldenVectorsTest.kt) 与 35 B SAT 回归测试
- **文档修正**：修复 [BLE_Wire_v1.md](BLE_Wire_v1.md) 5 处过时/笔误。

### 本地自检与 CI 状态
- 本地只读自检：`make proto-check` 通过，`make docs-check` 通过（35 Markdown 文件）。
- GitHub Actions（提交 `91636c6` + `6d88f55`）：
  - `protocol`（Run `45777917913`）：✅ 通过
  - `docs`（Run `457779207014`）：✅ 通过
  - `firmware`（Run `45777917995`）：✅ 通过（`flash=271508 B / 729088 B`，`ram=64613 B / 262144 B`，`twister` 13/13 全过）
  - `app`（Run `45779206907`）：✅ 通过（修复 `GoldenVectorsTest.kt` KDoc `/*` 嵌套注释后全绿）

---

## 2026-11-26 · Milestone 1：固件事件化双配置基线 & App 双 Flavor 隔离与核心模块

### 关键设计决策
1. **固件 `prod` / `debug` 双配置**：`Firmware/prj.conf` 切换为低功耗产品基线（`LOG_MODE_DEFERRED`、`LOG_DEFAULT_LEVEL=2`、关闭周期 UART 文本遥测与 BLE Log 线程、开启 `GPIO_NRFX_INTERRUPT`），原 M1 bring-up 调试配置独立沉淀至 [debug.conf](../../Firmware/conf/debug.conf)，CI 同时构建并体积门禁两种 profile。
2. **彻底去轮询与并发修复**：
   - 按键驱动 [keys.c](../../Firmware/src/power/keys.c) 移除 10 ms 轮询线程（省 2048 B 栈），改为 `GPIO_INT_EDGE_BOTH` 中断 + `k_work_delayable` 去抖/3 s 配对/8 s 关机定时，[power_sm.c](../../Firmware/src/power/power_sm.c) 关机流程改为非阻塞 delayable work；
   - BLE 日志 [ble_log.c](../../Firmware/src/ble/ble_log.c) 改为 `CONFIG_LGPS_BLE_LOG` 门控、信号量事件驱动唤醒（无 20 ms 轮询）且 `k_sem_give` 移出 spinlock 临界区；
   - GNSS [gnss.c](../../Firmware/src/gnss/gnss.c) 以 `fix_lock` 保护 `current_fix`、UART ISR 触发 `gnss_wake_sem`；新增纯 C 6 态功耗策略状态机 [gnss_fsm.c](../../Firmware/src/gnss/gnss_fsm.c) 与 `lifegps.gnss_fsm` 单测；
   - 校准模块 [calib.c](../../Firmware/src/calib/calib.c) 加入 Zephyr `k_mutex` 并发保护；[main.c](../../Firmware/src/main.c) 将 Sensor Hub 读盘与 10 Hz BLE Status 解耦为 1 Hz 缓存刷新；[ble_gatt.c](../../Firmware/src/ble/ble_gatt.c) + [time.c](../../Firmware/src/util/time.c) 补齐 `SET_TIME (0x11)` 外部对时。
3. **Android 生命周期 + 模块化 + `prod`/`dev` 物理隔离**：
   - 新增纯 JVM 模块 `:core:protocol`（含 `GeneratedProtocol`、`ProtocolCrc`、`TrajectoryRecord`、`DataFrameCodec`、`DayPackageCodec` + Heatshrink W=8/L=4 解码器，直接跑通全部 Golden Vectors）与 `:core:model`（含展示层专用 `CoordTransform` WGS-84↔GCJ-02 转换）；
   - 新增 [LifeGpsApplication.kt](../../App/app/src/main/java/com/lifegps/app/LifeGpsApplication.kt) 与 `AppContainer` 持有全局 BLE 会话（旋转屏幕不断连），彻底移除 USB-C OTG 依赖（`android.hardware.usb.host` 与 `LogSource.USB`）；
   - 抽象 [LifeGpsTransport.kt](../../App/app/src/main/java/com/lifegps/app/ble/LifeGpsTransport.kt)，升级 [BleTransport.kt](../../App/app/src/main/java/com/lifegps/app/ble/BleTransport.kt)（API 33+ 非弃用 GATT API、2M PHY 请求、`DATA`/`CONFIG` 特征发现、`BondState` 广播监听、按需启停 Telemetry/Log CCCD）；
   - 将全部 13 个硬件调试页迁入 `App/app/src/dev/java/com/lifegps/app/debug/`，使 `prod` APK 编译期物理隔离调试代码；新增产品外壳 [ProdAppHost.kt](../../App/app/src/main/java/com/lifegps/app/prod/ProdAppHost.kt)（首页/地图预览/时间轴/设置 + 按键灯语速查 + `SET_TIME` 对时），`dev` flavor 可一键在「硬件调试」与「产品模式」间无缝切换并共享同一 BLE 连接。
4. **CI 门禁扩容**：[firmware.yml](../../.github/workflows/firmware.yml) 双构建 `prod` + `debug`；[app.yml](../../.github/workflows/app.yml) 运行纯 JVM 核心单测 + `devDebug` 与 `prodDebug` 双构建/单测/Lint 并上传双 APK。

### M1 GitHub Actions 验证结果（`b4d563c` + `81a0d21` + `0ba8701`）
- `protocol`（Run `45783044531`）：✅ 通过
- `docs`（Run `45783044520`）：✅ 通过
- `firmware`（Run `45783044530`）：✅ 通过（`[prod] flash=271288 B / 729088 B, ram=60602 B / 262144 B`；`[debug] flash=276292 B / 729088 B, ram=63185 B / 262144 B`；`twister` 14/14 全过）
- `app`（Run `45784768861`）：✅ 通过（`:core:protocol:test` + `:core:model:test` + `devDebug` & `prodDebug` 构建/单测/Lint + 双 APK 上传）

---

## 2026-11-26 · Milestone 2：超低功耗 4 态电源管理 + IMU 运动唤醒 + 完整导航与自动落盘

### 关键设计决策
1. **4 态电源状态机与运动确认门**：新增纯 C [power_sm_logic.c](../../Firmware/src/power/power_sm_logic.c)（`DEEP_SLEEP` / `ACTIVE` / `HIGH_PRECISION` / `SYSTEM_OFF`），实现静止 30 s 自动进 `DEEP_SLEEP`（BLE 未连且非调试常开）、IMU INT1 唤醒后 3 s ≥ 2 步确认门、低电 ≤10% 降级及 ≤3% 保护关机；[power_sm.c](../../Firmware/src/power/power_sm.c) 关机前依次关闭 GNSS 供电、IMU power-down、LED 并配置 `KEY1_POI`（P1.06）`GPIO_INT_LEVEL_LOW` 唤醒后调用 `sys_poweroff()`。
2. **IMU 运动唤醒与 GNSS 联动**：[lsm6dsv16x.c](../../Firmware/src/imu/lsm6dsv16x.c) 接入 `imu-int1-gpios`（P0.02, GPIOTE30）上升沿中断，依据 `M1_BRINGUP.md` 已核实寄存器进入 15 Hz XL + 陀螺掉电 + 0.25 g 唤醒阈值（`WAKE_UP_THS=32`）；[gnss.c](../../Firmware/src/gnss/gnss.c) 新增 `lgps_gnss_set_policy()` 与 `lgps_gnss_force_off()`，响应 `DEEP_SLEEP` 断电与 `HIGH_PRECISION` 保持供电。
3. **导航补盲**：
   - [attitude.c](../../Firmware/src/nav/attitude.c) 新增 `lgps_attitude_correct_from_mag_guarded()`，磁异常（`|B|` 偏离地磁参考 >30%）时抑制磁力计航向拉偏并置位 `LGPS_FLAG_MAG_ANOMALY`；
   - [nav.c](../../Firmware/src/nav/nav.c) 新增 Weinberg `K` 户外在线标定（`[0.30, 0.55]` EMA）与 PDR 局部切平面 WGS-84 投影（室内断星连续输出坐标）；
   - 新增纯 C 气压楼层检测 [floor.c](../../Firmware/src/nav/floor.c)（3.0 m 层高 + 0.8 m 滞回 + 楼梯/电梯速度分类）与活动分类 [activity.c](../../Firmware/src/nav/activity.c)（静止/步行/乘车/室内 + `status_flags` 位映射）。
4. **跨重启单调 `seq_id` 与自动轨迹记录**：
   - [flash_ring.c](../../Firmware/src/storage/flash_ring.c) 保持 SFDP 动态 32 MB / 64 MB 兼容，新增开机恢复最高 `seq_id`（`lgps_ring_last_seq_id()`）与按 `seq_id` 顺序读游标 `lgps_ring_read_from_seq()`（为 M3 同步铺路）；
   - 新增纯 C 落盘策略 [recorder.c](../../Firmware/src/storage/recorder.c)（高精度 1 s / 移动 10 s / 静止 60 s / 转向 ≥30° / 换层 / KEY1 POI 即刻落盘），在 [main.c](../../Firmware/src/main.c) 自动编码并写入 Flash Ring；[ble_gatt.c](../../Firmware/src/ble/ble_gatt.c) 补齐 `SET_MODE (0x10)` 与 `BUTTON_EVENT (0x33)`。
5. **单元测试扩展**：新增 `lifegps.power_sm`、`lifegps.floor`、`lifegps.activity`、`lifegps.recorder` 4 套 `twister` 用例（总数 14 → 18）并在 `lifegps.nav` 追加 3 项导航回归测试。

### M2 GitHub Actions 验证结果（`032d04c` + `a339335`）
- `docs`（Run `45786395147`）：✅ 通过
- `firmware`（Run `45786990014`）：✅ 通过（`[prod] flash=277040 B / 729088 B, ram=60789 B / 262144 B`；`[debug] flash=281968 B / 729088 B, ram=63368 B / 262144 B`；`twister` 18/18 全过）

---

## 2026-11-26 · Milestone 3 (Part A 固件侧) + 实板串口日志缺陷修复

### 实板调试缺陷定位与修复（`UBX checksum error`）
- **现象**：用户烧录 `debug` 固件后，串口 1 Hz 遥测打印期间持续触发 `<wrn> lgps_gnss: UBX checksum error, frame dropped (total 16..17)`，且 `rx_dropped=0`（软件环形缓冲未溢出，说明丢失发生在 UARTE20 硬件 RX FIFO）。
- **根因**：M1 在 [ble_log.c](../../Firmware/src/ble/ble_log.c) 中为保护静态 `line_buf`/`output_buffer`，对整段 `log_output_msg_process()`（含完整浮点 `cbprintf` 格式化，耗时约 150–400 µs）加了 `k_spin_lock(&format_lock)`；在 460800 baud（21.7 µs/字节）高速 GNSS 链路上屏蔽全局中断导致 UARTE20 硬件 FIFO 溢出丢字节。
- **修复**：将 `format_lock` 改为 `K_MUTEX_DEFINE(format_mutex)`（不屏蔽硬件中断），ISR 上下文直接跳过 BLE 日志转发，并将 `k_sem_give(&log_avail_sem)` 留在 `slot_lock` 临界区外。

### M3-A 固件核心实现（32 MB & 64 MB Flash 通用）
1. **明文 LGD1 归档编解码与 SFDP 兼容日志索引**：新增 [archive.h](../../Firmware/include/lifegps/archive.h)、[archive_codec.c](../../Firmware/src/storage/archive_codec.c)（零 `malloc` 32B 字节平面差分 + Heatshrink W=8/L=4 LZSS + LGD1 封装/验签，与 Golden Vectors 逐字节一致）和 [journal_store.c](../../Firmware/src/storage/journal_store.c)（外部 NOR 扇区 20..21 乒乓索引，动态绑定 `lgps_ring_get_capacity().traj_bytes`，同时兼容 32 MB 与 64 MB 闪存）。
2. **配置 TLV 与 NOR 持久化**：新增 [app_config.h](../../Firmware/include/lifegps/app_config.h)、[app_config.c](../../Firmware/src/storage/app_config.c)、[app_config_store.c](../../Firmware/src/storage/app_config_store.c)，支持协议全部 11 个配置键（`0x01..0x0B`）原子校验回滚，持久化至外部 NOR 扇区 18（`0x12000`）。
3. **BLE SMP 配对加密 + `DATA (0004)` / `CONFIG (0007)` 完整命令闭环**：
   - [prj.conf](../../Firmware/prj.conf) 启用 `CONFIG_BT_SMP=y`、`CONFIG_BT_BONDABLE=y`、`CONFIG_BT_MAX_PAIRED=2`（`CONFIG_BT_SETTINGS=n` 待 M5 分区表落地后开启持久化），`CONFIG_BT_BUF_ACL_TX_COUNT` 扩至 10；
   - [ble_frame.h](../../Firmware/include/lifegps/ble_frame.h) / [ble_frame.c](../../Firmware/src/ble/ble_frame.c) 实现 `0x5A` Data 帧编解码；
   - [ble_gatt.c](../../Firmware/src/ble/ble_gatt.c) 补齐 `DATA (0004)`、`CONFIG (0007)` 特征、10 B 扫描响应厂商广播（`LGPS_ADV_*`）、KEY1 3 s 触发 60 s 配对窗口 `lgps_ble_open_pairing_window()`，以及全部 11 条产品命令：`SET_CONFIG (0x13)`、`GET_CONFIG (0x14)`、`SYNC_BEGIN (0x20)`、`SYNC_READ (0x21)`、`SYNC_END (0x22)`、`GET_DAY_INDEX (0x23)`、`GET_ARCHIVE (0x24)`、`SELFTEST (0x40)`、`ERASE_DAY (0x50)`、`FACTORY_RESET (0x51)`、`GET_FAULT_LOG (0x70)`。
4. **单元测试扩展**：新增 `lifegps.archive`、`lifegps.config` 两套 `twister` 用例（验证 32 MB / 64 MB 边界与全量 TLV 回滚），并扩展 `lifegps.protogen` 直接以 C 真实解码器校验 `data_chunk_trajectory.bin` 与 `day_package_hs.bin`。
5. **CI 构建修复**：`CONFIG_BT_BUF_ACL_TX_COUNT=10` 触发 Zephyr `buf.h` 静态断言要求 `CONFIG_BT_BUF_EVT_RX_COUNT > CONFIG_BT_BUF_ACL_TX_COUNT`，在 [prj.conf](../../Firmware/prj.conf) 补齐 `CONFIG_BT_BUF_EVT_RX_COUNT=12`。

### M3-B Android 本地存储 + 断点续传同步引擎 + 导出核心
1. **纯 JVM 模块 `:core:data`**：
   - [ConfigTlvCodec.kt](../../App/core/data/src/main/kotlin/com/lifegps/core/data/ConfigTlvCodec.kt)：11 项配置 TLV 全量与增量编解码；
   - [LocalDayStore.kt](../../App/core/data/src/main/kotlin/com/lifegps/core/data/LocalDayStore.kt)：`.lgd` 归档包原子写盘（`.tmp` -> rename）与损坏隔离区 `quarantine/`、`.lgs` 实时流 `seqId` 去重追加、Haversine 里程与日统计；
   - [TrackSimplifier.kt](../../App/core/data/src/main/kotlin/com/lifegps/core/data/TrackSimplifier.kt)：保留起终点与 POI 的 Douglas-Peucker 轨迹抽稀；
   - [TrackExporter.kt](../../App/core/data/src/main/kotlin/com/lifegps/core/data/TrackExporter.kt)：严格 WGS-84 的 GPX 1.1 / KML 2.2 / GeoJSON / CSV 流式导出；
   - [SyncProtocol.kt](../../App/core/data/src/main/kotlin/com/lifegps/core/data/SyncProtocol.kt) + [SyncEngine.kt](../../App/core/data/src/main/kotlin/com/lifegps/core/data/SyncEngine.kt)：双通道断点续传同步（实时环 `0x20/0x21/0x22` + 归档日包 `0x23/0x24`），并补齐 4 套纯 JVM 单元测试接入 [app.yml](../../.github/workflows/app.yml)。
2. **Android 索引与仓层**：新增 WAL 模式 [TrackIndexDb.kt](../../App/app/src/main/java/com/lifegps/app/data/TrackIndexDb.kt) 与 [ProductTrackRepository.kt](../../App/app/src/main/java/com/lifegps/app/data/ProductTrackRepository.kt)，经 [AppContainer](../../App/app/src/main/java/com/lifegps/app/LifeGpsApplication.kt) 注入全应用。

