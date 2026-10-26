# LIFE GPS Android 零基础开发指南

> 目标读者：没有 Android 开发经验、但要独立完成 LIFE GPS 手机端的人。  
> 结论先说：**不需要先学完 Kotlin 再动手**。按“环境 → 最小 Compose 页面 → 协程 → BLE → 本项目调试工具”的顺序，2～4 周可以进入有效开发。

---

## 0. 先理解四个事实

1. **Android 模拟器没有真实蓝牙**。UI 可以用模拟器，BLE 必须用真机。
2. **本项目有两个 flavor**：`dev` 是调试工具（GATT、终端、遥测），`prod` 是产品终端（同步、地图、导出）。先做 `dev`。
3. **不要先做产品 UI**。协议和固件还在变，先做“扫描 → 连接 → 读状态 → 看日志”，这套工具会反过来加速固件开发。
4. **本项目的所有编译都在 GitHub Actions 完成**。本地只改代码/文档，不要求安装 Android SDK、Gradle 或 JDK；手机直接安装 CI 产出的 APK。

---

## 1. 环境（按“只编辑”与“想本地预览”区分）

### 1.1 必装

| 软件 | 说明 |
| :--- | :--- |
| **Git** | 提交/拉取代码 |
| **文本编辑器 / IDE** | VS Code 或 Android Studio；只编辑的话 VS Code 就够 |
| **一台 Android 真机** | Android 8.0+，BLE 必须真机 |
| **USB 数据线** | 能传数据，不是纯充电线 |

### 1.2 只用 GitHub 编译（推荐）

不需要安装 Android SDK、Gradle、JDK。流程：

1. 本地改代码 → commit → push。
2. GitHub Actions 自动跑 `app.yml`。
3. 构建成功后下载 `app-dev-debug` artifact。
4. `adb install -r app-dev-debug.apk`。

### 1.3 想本地预览（可选）

Android Studio 最新稳定版 + Android SDK 35 + JDK 17。  
本地编译只用于 Compose Preview / 快速看 UI；**最终构建与 APK 以 GitHub Actions 为准**。如果严格不希望本地编译，跳过 Gradle Sync，直接用 CI APK。

> 首版骨架使用 `compileSdk/targetSdk 35`（AGP 8.7.3）。CI 稳定后再升级到 36，避免一上来被 AGP/SDK 版本卡住。

---

## 2. 第一次打开项目

仓库已经生成 `App/` Gradle 工程：

1. Android Studio → **Open** → 选择 **`App/` 目录**，不要选仓库根目录。
2. 如果只想看代码、不想本地编译：不要点 Run/Sync，直接读代码或用 CI APK。
3. 如果要本地预览：等待 Gradle Sync，选择 `devDebug` 构建变体，再 Run 到模拟器/真机。
4. 如果提示 SDK 路径错误：`File → Project Structure → SDK Location` 指向 Android Studio 的 SDK。

> 仓库根目录是 Monorepo，同时有 `Firmware/`、`Hardware/`；Android Studio 只认 `App/` 这套 Gradle 工程。

---

## 2.1 从 GitHub Actions 下载并安装 APK

1. 打开 GitHub 仓库 → **Actions** → **app**。
2. 点最新一次绿色 run。
3. 页面底部 **Artifacts** → 下载 `app-dev-debug`。
4. 解压得到 `app-dev-debug.apk`。
5. 手机连接电脑：
   ```bash
   adb install -r app-dev-debug.apk
   ```
   或者把 APK 传到手机，允许“未知来源”后手动安装。

> 每次 push 都会重新构建；不用在本地装 Android SDK。

---

## 2.2 第一次打开会看到什么

当前骨架是**硬件调试优先**，用 `FakeDebugRepository` 提供假数据：

- **硬件调试首页**：设备连接状态、电量、GNSS 星数/HDOP、步数、快捷操作、最近日志。
- **串口日志**：BLE / USB 双来源，级别过滤、搜索、暂停、清空、导出（模拟）。
- **传感器状态**：IMU 加速度/角速度/四元数、磁力计模长/异常、气压/温度/高度/楼层、ZUPT。
- **GPS 状态**：六状态机、星数、HDOP、经纬度、海拔、速度、航向、C/N0 柱状图、TTFF。
- **硬件自检**：MCU / Flash / IMU / 磁力计 / 气压计 / GNSS / 天线 / ADC / 充电 / 按键 / LED / BLE。
- **Flash 检查**：63MB 轨迹区 + 1MB OTA slot1、16 扇区图、Journal 状态。
- **命令终端**：opcode 选择、hex payload、响应历史。
- **空间轨迹**：IMU/PDR 局部坐标 3D 轨迹、距离/采样点/ZUPT。
- **地图实时轨迹**：GNSS 实时画线、当前位置、点击地图打点、POI 列表。

真实 BLE / USB 串口接入后，只要替换 `FakeDebugRepository` 的数据源，界面不用重写。

---

## 3. 真机调试（BLE 必需）

1. 手机 → 设置 → 关于手机 → 连续点“版本号”7 次，开启开发者选项。
2. 开发者选项 → 打开 **USB 调试**；小米/OPPO 还要打开“USB 安装”“后台弹出界面”等。
3. 数据线连接电脑，手机弹窗选“允许 USB 调试”。
4. 命令行确认：
   ```bash
   adb devices
   ```
   能看到设备序列号即成功。
5. 安装 CI APK（见 2.1），或 Android Studio 运行 `devDebug`。
6. 打开 App，按提示授予附近设备/通知权限。

### 3.1 BLE 权限（Android 12+）

App 需要按系统版本申请：

| 系统 | 权限 |
| :--- | :--- |
| Android 12+ | `BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT` |
| Android 11 及以下 | `ACCESS_FINE_LOCATION`（BLE 扫描需要） |
| Android 13+ | `POST_NOTIFICATIONS`（同步进度通知） |
| Android 14+ | 前台服务类型 `connectedDevice` + `FOREGROUND_SERVICE_CONNECTED_DEVICE` |

调试时如果搜不到设备，先检查：
- 手机蓝牙已开；
- App 权限已授予；
- 没有被系统“附近设备”权限开关拦住；
- 设备在广播（长按配对键 3 秒）。

---

## 4. 学习路线（按项目需要，不按教科书）

### 第 1 周：Kotlin + Compose 最小集

只学这些就够开始：

- Kotlin：`val/var`、函数、`data class`、`when`、可空类型、集合、Lambda。
- Compose：`@Composable`、`Column/Row/Box`、`Text/Button/LazyColumn`、`remember/mutableStateOf`、`Scaffold`。
- 导航：`NavHost` + 两三个页面。
- 协程：`suspend`、`viewModelScope.launch`、`Flow.collect`。
- ViewModel：`StateFlow` + `collectAsStateWithLifecycle`。

**练手**：做一个“设备列表”页面，假数据展示名称/RSSI/电量，点击进入详情。

### 第 2 周：BLE 最小闭环

- 学 BLE 基础：GATT、Service、Characteristic、Notify、MTU、bond。
- 用 **nRF Connect** App 先手动连设备，确认能看到服务/特征/通知。
- 在项目里用 **Nordic Android-BLE-Library**：
  - 扫描 + 过滤 Service UUID；
  - 连接；
  - 读 Status；
  - 订阅 Notify；
  - 断开重连。

**练手**：扫描 → 连接 → 显示 Status 的 hex/字段 → 订阅日志。做到这一步，调试工具 MVP 的核心就通了。

### 第 3 周：数据与后台

- Room：实体、DAO、数据库、Flow 查询。
- DataStore：设置项。
- WorkManager / Foreground Service：同步任务与通知。
- 文件：Okio、日包读写、AES-GCM（先理解，后面再接）。

### 第 4 周：地图与导出

- MapLibre：加载样式、画线、标记、离线区域。
- 坐标：WGS-84 vs GCJ-02。
- 导出：GPX/KML/GeoJSON 字符串生成 + 分享。

> 不要在第 1 周就碰 MapLibre、SQLCipher、DFU；先把“扫描-连接-读状态”跑通。

---

## 5. 本项目 App 的结构

```text
App/
├── app/                 # 入口、导航、DI、dev/prod flavor
├── core/
│   ├── protocol/        # 纯 JVM：帧/CRC/记录/日包；可单测
│   ├── ble/             # Transport 接口：BLE / USB / Fake / Replay
│   ├── sync/            # 同步状态机、断点、重试
│   ├── data/            # Room、日包文件、加密、导出
│   ├── model/           # 时间/坐标/Flag/统计
│   └── designsystem/    # 主题与组件
├── feature/
│   ├── debug/           # GATT/终端/遥测/Flash/校准
│   ├── map/             # 地图/剖面/播放
│   ├── sync/            # 同步 UI/诊断
│   ├── devices/         # 设备列表/设置
│   ├── timeline/        # 日历/洞察
│   └── settings/        # 导出/备份/OTA/隐私
└── simulator/           # JVM 假设备 + 回放
```

**关键原则**：`feature` 不直接碰 BLE；通过 `core:ble` 的 `Transport` 接口。这样 UI 可以用 Fake Transport 在模拟器/CI 里跑，不需要真机。

---

## 6. 第一个真实任务（建议）

按这个顺序做，不要跳：

1. **空 Compose 页面**：底部两个 Tab：`设备` / `调试`。
2. **扫描列表**：名称、RSSI、广播 hex；点击连接。
3. **状态页**：电量、模式、GNSS 状态、Flash 用量（先显示 hex，再逐字段解码）。
4. **日志页**：订阅 Log 特征，滚动显示，支持暂停/清空/导出 txt。
5. **命令终端**：下拉选 opcode、hex 输入、发送、显示响应。
6. **Bring-up 清单**：把“扫描→连接→读 DIS→状态→设时→日志”做成逐项 PASS/FAIL。

完成 1～4，你就已经能独立调试固件了。

---

## 7. 常见坑

| 现象 | 原因/解决 |
| :--- | :--- |
| Gradle Sync 失败 | JDK 版本不对、SDK 未安装、网络代理；先看 Build 窗口第一条错误 |
| 模拟器搜不到 BLE | 模拟器没有蓝牙；换真机 |
| 真机搜不到设备 | 权限未给、设备未广播、Service UUID 过滤写错 |
| 连接后立刻断开 | 设备端连接参数/MTU/配对问题；先看设备日志 |
| Android 12 扫描失败 | 没申请 `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`，或 `neverForLocation` 用法不对 |
| 后台同步被杀 | 小米/OPPO 后台限制；用前台服务 + 电池优化白名单提示 |
| `adb devices` 空 | 数据线是纯充电线、没开 USB 调试、驱动问题 |
| 地图不显示 | 没网络、样式 URL 错、瓦片缓存/证书问题；先用官方示例样式验证 |
| Release 包崩溃 | R8 混淆；保留协议/序列化类，上传 `mapping.txt` |

---

## 8. 推荐资料

- Android 官方入门：<https://developer.android.com/get-started>
- Kotlin 官方文档：<https://kotlinlang.org/docs/home.html>
- Compose 教程：<https://developer.android.com/develop/ui/compose/tutorial>
- 协程：<https://developer.android.com/kotlin/coroutines>
- BLE 官方指南：<https://developer.android.com/develop/connectivity/bluetooth/ble>
- Nordic Android-BLE-Library：<https://github.com/NordicSemiconductor/Android-BLE-Library>
- nRF Connect（调试 BLE 必备）：应用商店搜索 “nRF Connect”
- MapLibre Android：<https://github.com/maplibre/maplibre-native>
- Now in Android（工程结构参考）：<https://github.com/android/nowinandroid>

---

## 9. 时间预期

| 基础 | 能开始写 | 能独立完成调试工具 MVP |
| :--- | :--- | :--- |
| 会其他语言、没写过 Android | 3～5 天 | 3～4 周 |
| 完全新手 | 1～2 周 | 4～6 周 |

不要等“学完”再开始。每学一个知识点，就落到本项目的页面上；调试工具本身就是最好的练习。
