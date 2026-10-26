# ble-sim（规划）

Python BLE 外设模拟器，让 Android 手机连接一个“假 Life_GPS”，用于在真板打样前开发 App。

## 目标

- 广播：`LGPS-XXXX` + Service UUID + Manufacturer Data。
- GATT：Status / Control / Data / Telemetry / Log / Config。
- 同帧格式、同 MTU 247、同 CRC。
- 合成 30 天轨迹 + 日包 + 故障注入（断线、CRC 错、BUSY、低电量、擦除）。
- 支持 Linux BlueZ / macOS。

## 技术选型

- Python 3.11+
- `bless`（BLE peripheral）
- 复用 `protocol/lifegps_protocol.yaml` 与 `protocol/vectors/`

## 替代方案

- nRF52840 DK 运行 `CONFIG_LGPS_MOCK_DATA=y` 固件：最接近真实 BLE 栈，能验证 2M PHY/DLE。
- App 内 Fake Transport：CI/模拟器用，不经过真实蓝牙。

## 待办

- [ ] 广播 + 静态 Status
- [ ] Control 命令解析
- [ ] Data 分块 + 同步状态机
- [ ] 30 天合成数据生成
- [ ] 故障注入脚本
