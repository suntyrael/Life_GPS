# LIFE GPS App（Android 硬件调试工具 + 产品终端）

> 状态：**Android 骨架已生成**。首版是单模块 Compose App，硬件调试界面使用假数据跑通；所有编译在 GitHub Actions 完成。

## 打开方式

- Android Studio 打开 **`App/` 目录**，不要打开仓库根目录。
- 仓库根是 Monorepo，同时有 `Firmware/`、`Hardware/`；Android Studio 只认 `App/` 这套 Gradle 工程。
- 不想本地编译：不点 Run/Sync，直接 push，等 `app.yml` 产出 APK artifact。

## 已包含的硬件调试页面

| 页面 | 内容 |
| :--- | :--- |
| 硬件调试首页 | 设备状态、电量、GNSS 星数/HDOP、步数、快捷操作、最近日志 |
| 设备扫描/连接 | BLE 扫描列表、RSSI、连接/断开（当前为假设备） |
| 串口日志 | BLE / USB 双来源、级别过滤、搜索、暂停、清空、导出 |
| 传感器状态 | IMU 加速度/角速度/四元数、磁力计、气压计、ZUPT |
| GPS 状态 | 六状态机、星数、HDOP、经纬度、海拔、速度、航向、C/N0、TTFF |
| 硬件自检 | MCU/Flash/IMU/磁/气压/GNSS/天线/ADC/充电/按键/LED/BLE |
| Flash 检查 | 63MB 轨迹区 + 1MB OTA slot1、16 扇区图、Journal |
| 命令终端 | opcode 选择、hex payload、响应历史 |
| 空间轨迹 | IMU/PDR 局部坐标 3D 轨迹、距离/采样点/ZUPT |
| 地图实时轨迹 | GNSS 实时画线、当前位置、点击打点、POI 列表 |

当前数据来自 `FakeDebugRepository`。真实 BLE / USB 串口接入后，替换数据源即可，页面不用重写。

## 当前结构（第一阶段单模块）

```text
App/
├── app/src/main/java/com/lifegps/app/
│   ├── MainActivity.kt
│   ├── LifeGpsApp.kt              # NavHost + 仓库实例
│   ├── navigation/Routes.kt
│   ├── debug/                     # 硬件调试页面 + FakeDebugRepository
│   └── ui/                        # theme + 通用组件
├── app/src/test/                  # JVM 单测
├── docs/                          # 入门/架构
├── core/README.md                 # 后续拆分目标（protocol/ble/sync/data/model）
├── feature/README.md              # 后续拆分目标（debug/map/sync/devices/timeline/settings）
├── gradle/libs.versions.toml
└── gradlew
```

> 先单模块把 UI 和调试流程跑通；等协议/BLE 稳定后再拆 `core:protocol`、`core:ble`、`feature:*`。对新手来说，单模块更容易维护。

## 编译（只在 GitHub）

[![app](https://github.com/suntyrael/Life_GPS/actions/workflows/app.yml/badge.svg)](https://github.com/suntyrael/Life_GPS/actions/workflows/app.yml)


```text
push → GitHub Actions → app.yml
  → ./gradlew :app:assembleDevDebug :app:testDevDebugUnitTest :app:lintDevDebug
  → Artifacts: app-dev-debug
```

安装：

```bash
adb install -r app-dev-debug.apk
```

本地编译是可选的，仅用于 Compose Preview；最终以 CI 为准。

## 文档

- [Android 零基础开发指南](../Doc/Software/Android_Dev_Guide.md)
- [App 入门](docs/GETTING_STARTED.md)
- [App 架构](docs/ARCHITECTURE.md)
- [App 规划](../Doc/Software/Android_App_Plan.md)
- [BLE 协议草案](../Doc/Software/BLE_Protocol.md)
