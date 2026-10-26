# LIFE GPS App 架构

## 当前实现（骨架）

```text
App/app/src/main/java/com/lifegps/app/
├── MainActivity.kt
├── LifeGpsApp.kt          # NavHost + FakeDebugRepository
├── navigation/Routes.kt
├── debug/                 # 硬件调试页面 + 假数据
└── ui/theme + ui/components
```

第一阶段是**单模块** `:app`，优先把硬件调试界面和假数据流程跑通。协议/BLE 稳定后再拆 `core:protocol`、`core:ble`、`core:sync`、`core:data`、`feature:*`。

## 目标分层

```text
feature/* (Compose UI + ViewModel)
        │
        ▼
core/sync (同步状态机) ── core/data (Room + 日包文件)
        │
        ▼
core/ble (Transport 接口)
        │
        ├── BleTransport     真实设备
        ├── UsbSerialTransport 7P 调试串口
        ├── FakeTransport    JVM/CI 假设备
        └── ReplayTransport  会话回放
        │
        ▼
core/protocol (纯 JVM：帧/CRC/记录/日包)
```

## 关键决策

| 主题 | 方案 |
| :--- | :--- |
| UI | Kotlin + Jetpack Compose + Material 3，单 Activity |
| BLE | Nordic Android-BLE-Library；GATT 队列串行 |
| 同步 | App 主动 pull；日包 + 当日暂存双通道；断点续传 |
| 本地存储 | 日包文件为真相源 + Room 元数据/bbox/简化折线 |
| 加密 | 日包 AES-256-GCM；Room SQLCipher；密钥在 Keystore |
| 地图 | MapLibre；WGS-84 内部，渲染层转 GCJ-02 |
| 后台 | Foreground Service（connectedDevice）+ WorkManager |
| OTA | MCUboot + SMP（mcumgr）；内部 slot0 + 外部 Flash slot1 |

## Transport 接口（示意）

```kotlin
interface Transport {
    val events: Flow<TransportEvent>
    suspend fun connect(deviceId: String)
    suspend fun write(characteristic: Uuid, data: ByteArray)
    suspend fun read(characteristic: Uuid): ByteArray
    suspend fun subscribe(characteristic: Uuid): Flow<ByteArray>
    suspend fun disconnect()
}
```

## 同步引擎

状态：`IDLE → SCAN → CONNECT → BOND → HANDSHAKE → PLAN → TRANSFER → VERIFY → COMMIT → DONE`。

- 先 `GET_STATUS` 拿 `archive_index_ver` / `last_seq`。
- 历史：`GET_DAY_INDEX` → `GET_ARCHIVE`，按 `(day_id, crc32)` 幂等。
- 当天：`SYNC_BEGIN/SYNC_READ` 按 seq 拉暂存。
- 每块 CRC16、每日 CRC32 校验后才提交；断线从最后确认游标重试。

## 存储

- `files/devices/<id>/days/<dayId>.lgd`：AES-GCM 日包（真相源）。
- Room：设备、日索引（bbox/简化折线）、Beacon、POI、同步记录。
- 地图概览用简化折线；选中某日/放大时解码原始日包（LRU）。

详细设计见 [Android_App_Plan.md](../../Doc/Software/Android_App_Plan.md) §5～§6。
