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
| **M0** | 协议 SSOT 补全、跨端代码生成（C + Kotlin + Golden Vectors）与高危止血修债 | 已完成编码，待 CI 验证 |
| **M1** | 固件去轮询事件化 + 双配置骨架；App 5 模块化 + Nordic BLE + 双 Flavor 隔离 | 待启动 |
| **M2** | 固件超低功耗 4 态电源状态机 + IMU 运动唤醒 + 导航补盲（磁异常/Weinberg/楼层/乘车）+ 周期轨迹落盘 | 待启动 |
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
  - 生成 [lifegps_protocol.h](../../Firmware/include/generated/lifegps_protocol.h)、[GeneratedProtocol.kt](../../App/app/src/main/java/com/lifegps/app/ble/generated/GeneratedProtocol.kt)、9 个跨端共享向量 [manifest.json](../../protocol/vectors/manifest.json) 与 [generated_vectors.h](../../Firmware/tests/lifegps.protogen/src/generated_vectors.h)
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
- GitHub Actions：推送 `code_by_agy` 后追踪 `protocol` / `firmware` / `app` / `docs` 四条流水线结果。
