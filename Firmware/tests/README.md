# Firmware 测试

## 分层

| 层 | 内容 | 运行方式 |
| :--- | :--- | :--- |
| 协议 | 帧/CRC/32B/16B/日包/delta/heatshrink | `native_sim` + Golden Vectors |
| 算法 | PDR/步长/ZUPT/气压楼层 | `native_sim`，喂合成 IMU 数据 |
| 存储 | 环形扇区/Journal/掉电回滚 | `native_sim` + 模拟 Flash |
| BLE | GATT/MTU/PHY/同步 | nRF DK + 手机/模拟器 |
| 硬件 | I2C/SPI/GPIO/功耗 | 真板 + 逻辑分析仪/PPK2 |

## 命令（脚手架完成后）

```bash
cd Firmware
twister -p native_sim
west build -b nrf52840dk_nrf52840 -- -DCONFIG_LGPS_MOCK_DATA=y
```

## Golden Vectors

- 位置：`protocol/vectors/`
- 同一份二进制同时被 C（native_sim）和 Kotlin（App JVM 单测）读取。
- 覆盖：轨迹 32B、Beacon 16B、命令帧、Data 块、日包、跨 0xFF delta、heatshrink 边界。
