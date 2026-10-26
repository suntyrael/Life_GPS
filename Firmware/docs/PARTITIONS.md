# Flash 分区与 OTA

> 这是固件/硬件/App 的共同契约。改偏移必须同时改 DTS、生成头文件、App 常量和 CI 校验。

## 运行槽与备份槽

**固件最终永远从内部 `slot0` 启动。** `slot1` 只是 OTA 新镜像的暂存/回滚副本：

1. App 通过 SMP 把新镜像上传到外部 `slot1`。
2. 重启后 MCUboot 校验签名，把 `slot1` 镜像 swap 到 `slot0`。
3. MCUboot 从 `slot0` 启动新固件；`slot1` 保留旧镜像/回滚信息。
4. 新固件 confirm 后，`slot1` 可作为下一次 OTA 的暂存区。

`slot1` **不执行**，也不做 XIP；它只在 OTA 期间被 MCUboot 读写。

## 内部 1MB（nRF52840）

| 区域 | 偏移 | 大小 | 说明 |
| :--- | :--- | :--- | :--- |
| MCUboot | `0x00000` | 64KB | 含 SPI NOR/flash map；M1 实测可调 |
| slot0 | `0x10000` | 896KB | 当前运行固件；镜像预算 860KB |
| scratch | `0xF0000` | 32KB | swap 暂存 |
| settings/NVS | `0xF8000` | 32KB | 绑定/配置 |

## 外部 64MB（W25Q512，4KB sectors）

| 区域 | 偏移 | 大小 | 说明 |
| :--- | :--- | :--- | :--- |
| 轨迹暂存/归档/索引 | `0x0000000` | 63MB | App 只写此区；AES-CTR |
| OTA slot1 | `0x3F00000` | 1MB | MCUboot 只写此区；只签名 |

## DTS 参考

```dts
&spi1 {
  w25q512: flash@0 {
    compatible = "jedec,spi-nor";
    reg = <0>;
    spi-max-frequency = <8000000>;
    size = <0x4000000>;
    partitions {
      compatible = "fixed-partitions";
      #address-cells = <1>;
      #size-cells = <1>;
      traj_partition: partition@0 {
        label = "traj";
        reg = <0x0000000 0x3F00000>;
      };
      slot1_partition: partition@3F00000 {
        label = "slot1";
        reg = <0x3F00000 0x100000>;
      };
    };
  };
};
```

内部 Flash 还需要：`boot_partition`、`slot0_partition`、`scratch_partition`、`settings_partition`。

## MCUboot 配置要点

```conf
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_BOOT_SWAP_USING_SCRATCH=y
CONFIG_BOOT_MAX_IMG_SECTORS=256
CONFIG_SPI_NOR=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_MCUMGR_TRANSPORT_BLE=y
CONFIG_MCUMGR_GRP_IMG=y
```

- slot1 1MB = 256 × 4KB sectors，`BOOT_MAX_IMG_SECTORS` 必须覆盖。
- MCUboot 要编译 SPI NOR 驱动，并在初始化时发送 **0xAB Release Deep Power-Down**，否则 App 关机/看门狗复位后读不到外部 Flash。
- App 只使用 `traj_partition`；最后 1MB 绝不写。

## M1 POC 清单

1. DK + W25Q512 接线，MCUboot 能读 JEDEC ID。
2. SMP 上传到外部 slot1 → swap → 启动 → confirm → 再升级。
3. 上传/swap 中断电，重启可回滚/继续。
4. App 把 Flash 置 DPD 后看门狗复位，MCUboot 能 0xAB 释放。
5. 实测 896KB swap 时间/电流，OTA <60s。
6. `traj_partition` 末尾写入/擦除不触碰 `0x3F00000` 之后。

## 失败兜底

1. 回退内部双槽 456KB/槽，App 预算 430KB（功能裁剪）。
2. 单槽 + 外部 OTA 暂存区（需批准；不做 XIP）。
3. 单槽 + 物理调试口恢复（仅救砖）。

详细讨论见 [Android_App_Plan.md](../../Doc/Software/Android_App_Plan.md) §5.9 与附录 C。
