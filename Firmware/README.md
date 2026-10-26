# Firmware（Zephyr / nRF Connect SDK）

> 状态：**文档/骨架阶段**。固件源码尚未生成；先读 [分区表](docs/PARTITIONS.md) 与 [Android 规划](../Doc/Software/Android_App_Plan.md)。

## 推荐工具链

nRF52840 + BLE + MCUboot + SMP 的组合，建议直接用 **nRF Connect SDK（NCS，Zephyr 下游）**，比纯 upstream Zephyr 少踩 Nordic 驱动/DFU 的坑。

- nRF Connect SDK Toolchain（VS Code 扩展）或手动 `west`
- Zephyr SDK / NCS toolchain 版本在根目录 `versions.yaml` 锁定
- Python 3.11+、`west`、`cmake`、`ninja`

## 目录

```text
Firmware/
├── west.yml                 # 固定 NCS/Zephyr 与模块 revision
├── boards/lifegps/          # 自定义 nRF52840 板级定义
├── src/                     # 电源状态机/GNSS/IMU/PDR/存储/BLE
├── include/generated/       # 协议生成 C 头文件（CI 校验）
├── tests/                   # native_sim / twister
└── docs/PARTITIONS.md       # 内部/外部 Flash 分区与 OTA
```

## 构建（脚手架完成后）

```bash
# 第一次
west init -m https://github.com/nrfconnect/sdk-nrf --mr <pinned-tag> ~/ncs
cd ~/ncs && west update

# 真实板
west build -b lifegps/nrf52840 Life_GPS/Firmware

# DK + mock 传感器/轨迹
west build -b nrf52840dk_nrf52840 Life_GPS/Firmware -- -DCONFIG_LGPS_MOCK_DATA=y

# 单元测试
cd Life_GPS/Firmware && twister -p native_sim
```

> 具体 board 名称、overlay、Kconfig 在 M1 脚手架时确定；`make fw` 会封装这些命令。

## 关键约束

- **内部 Flash**：MCUboot 64KB + slot0 896KB + scratch 32KB + NVS 32KB。
- **外部 64MB Flash**：63MB 轨迹 + 1MB OTA slot1；App 只写轨迹区。
- **镜像预算**：860KB（slot0 896KB - header/trailer）。
- **OTA POC**：外部 SPI NOR slot1 的 swap/回滚/DPD 释放必须在 M1 验证；失败回退内部双槽 430KB。
- **协议**：C 头文件由 `protocol/lifegps_protocol.yaml` 生成，不手写。

## 工作包

见 [Android_App_Plan.md](../Doc/Software/Android_App_Plan.md) 附录 B（F1～F11）。优先级：

1. F1 GATT + Status/Control/Log
2. F2/F3 同步 + 日包
3. F8 2M PHY/MTU/DLE
4. F7 OTA
5. F5/F6/F9/F10 调试与遥测
6. F11 产测
