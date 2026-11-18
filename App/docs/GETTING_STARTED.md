# LIFE GPS App 入门 / 第一次运行

> 详细学习路线见 [Android 零基础开发指南](../../Doc/Software/Android_Dev_Guide.md)。  
> 本项目所有编译在 GitHub Actions 完成，本地不要求 Android SDK/Gradle。

## 1. 最短路径（不本地编译）

1. 改代码 → commit → push。
2. GitHub → Actions → **app** → 最新绿色 run。
3. 下载 Artifacts → `app-dev-debug`。
4. 解压得到 `app-dev-debug.apk`。
5. 手机开启 USB 调试：
   ```bash
   adb devices
   adb install -r app-dev-debug.apk
   ```

## 2. 第一次打开 App

- 首页是“Life_GPS 硬件调试”。
- 点“串口日志 / 传感器状态 / GPS 状态 / 硬件自检 / Flash 检查 / 命令终端 / 空间轨迹 / 地图实时轨迹”。
- 首次启动先授权蓝牙权限；没有 BLE 硬件时显示说明页。
- 点“扫描 / 连接”扫描真实 LGPS 设备，连接后自动发 `GET_CAPS` + `GET_STATUS` + `FLASH_INFO`。
- `runSelfTest` 会真实发送 `SELFTEST`；v1 返回 `03 NOT_SUPPORTED`，检查项保持 PENDING。

## 3. 想本地预览（可选）

1. Android Studio → Open → `App/`。
2. Gradle Sync（会下载依赖）。
3. Build Variants 选 `devDebug`。
4. Run 到模拟器/真机。

> 如果严格不希望本地编译，跳过本节，直接用 CI APK。

## 4. 真机 BLE 权限

| 系统 | 权限 |
| :--- | :--- |
| Android 12+ | `BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT` |
| Android 11 及以下 | `ACCESS_FINE_LOCATION` |
| Android 13+ | `POST_NOTIFICATIONS` |

## 5. 已完成的真实接入

1. 用 `nRF Connect` 手动确认设备广播/GATT。
2. `BleDebugRepository` 已完成：
   - 扫描 + Service UUID 过滤（4 s 无结果回退不过滤，10 s 超时）
   - 连接 + `requestMtu(247)` + 服务发现 + CCCD 订阅
   - Status 1 Hz 刷新全部页面；Log 通知写入日志页
   - Control 命令 + 2 s 响应匹配，真实 `NOT_SUPPORTED` 直接显示
3. 页面只依赖 `DebugRepository` 接口，换数据源不需要改 UI。

## 6. 常用命令

```bash
adb devices
adb install -r app-dev-debug.apk
adb logcat | grep -i lifegps
```

## 7. CI 文件

- `.github/workflows/app.yml`
- 构建任务：`assembleDevDebug` + `testDevDebugUnitTest` + `lintDevDebug`
- 产物：`app-dev-debug` artifact
