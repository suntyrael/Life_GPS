# Firmware（Zephyr / nRF Connect SDK）

> 状态：**M1 板级 Bring-up 与 M1.5 单机记录闭环已落地**，由 `.github/workflows/firmware.yml` 自动执行目标板构建、体积门禁与 `twister -p native_sim` 单元测试。详见 [M1 Bring-up 实测说明](docs/M1_BRINGUP.md)、[分区表](docs/PARTITIONS.md) 与 [Android 规划](../Doc/Software/Android_App_Plan.md)。

## 推荐工具链

**nRF54L15**（Cortex-M33 + BLE + MCUboot + SMP）使用 **nRF Connect SDK（NCS v3.4.2，Zephyr 4.4.2）**。

- nRF Connect SDK Toolchain（`ncs-toolchain-v3.4.2`）
- 版本在根目录 `versions.yaml` 与 `Firmware/west.yml` 锁定
- 本项目约定本地零编译，所有构建在 GitHub Actions `firmware.yml` 中运行

## 目录

```text
Firmware/
├── CMakeLists.txt           # Zephyr 应用构建入口
├── prj.conf                 # Kconfig 配置（附硬件/手册依据）
├── west.yml                 # 固定 NCS v3.4.2 与子模块列表
├── boards/lifegps/lifegps/  # 自定义 lifegps/nrf54l15/cpuapp 板级定义与设备树
├── dts/bindings/            # 自定义设备树绑定（lifegps,lsm6dsr）
├── include/
│   ├── generated/           # 协议生成 C 头文件（gen_c.py --check 门禁）
│   └── lifegps/             # 模块公共头文件
├── src/                     # 板级 IO / 电源与按键 / GNSS(UBX) / IMU(LSM6DSR) / 导航 / 存储 / 遥测
├── tests/                   # native_sim / twister 单元测试套件（lifegps.*）
└── docs/                    # M1_BRINGUP.md 与 PARTITIONS.md
```

## 构建（GitHub Actions `firmware.yml` 同款命令）

```bash
# 第一次初始化工作区
west init -l Firmware
west update --narrow -o=--depth=1

# 真实板（Main_V0.2）
west build -b lifegps/nrf54l15/cpuapp Firmware

# 单元测试（全部纯算法与编解码套件）
twister -p native_sim -T Firmware/tests --inline-logs
```

## 关键约束

- **内部 RRAM（1524KB）**：采用 nRF54L15 标准分区（`boot` 62KB / `slot0` 712KB / `slot1` 712KB / `storage` 36KB），CI 强制门禁 `text+data <= 712 KB`、`data+bss <= 256 KB`。
- **外部 SPI NOR Flash（运行时 SFDP 自适应）**：同时兼容 **32MB (`W25Q256`)** 与 **64MB (`W25Q512NW`)**；顶部 `1 MiB`（`容量 - 1 MiB`）保留给 OTA `slot1`，其余（`31 MiB` 或 `63 MiB`）全部作为轨迹暂存与归档区。
- **6 轴 IMU（`LSM6DSRTR`）**：运行于 26Hz 原生 ODR（ZUPT 窗口 $N=26$ 恰好 1.000s），机体旋转矩阵经台架四姿态实测锁定为 `[[0,-1,0],[-1,0,0],[0,0,-1]]`（$\det=+1$）。
- **协议**：C 头文件 `include/generated/lifegps_protocol.h` 由 `protocol/lifegps_protocol.yaml` 经 `tools/protocol-gen/gen_c.py` 生成，禁止手写。

## 工作包

见 [Android_App_Plan.md](../Doc/Software/Android_App_Plan.md) 附录 B（F1～F11）。优先级：

1. F1 GATT + Status/Control/Log
2. F2/F3 同步 + 日包
3. F8 2M PHY/MTU/DLE
4. F7 OTA
5. F5/F6/F9/F10 调试与遥测
6. F11 产测
