# Flash 分区与 OTA

> 这是固件/硬件/App 的共同契约。改偏移必须同时改 DTS、生成头文件、App 常量和 CI 校验。

## 运行槽与备份槽

**固件最终永远从内部 `slot0` 启动。** `slot1` 只是 OTA 新镜像的暂存/回滚副本：

1. App 通过 SMP 把新镜像上传到外部 `slot1`。
2. 重启后 MCUboot 校验签名，把 `slot1` 镜像 swap 到 `slot0`。
3. MCUboot 从 `slot0` 启动新固件；`slot1` 保留旧镜像/回滚信息。
4. 新固件 confirm 后，`slot1` 可作为下一次 OTA 的暂存区。

`slot1` **不执行**，也不做 XIP；它只在 OTA 期间被 MCUboot 读写。

## 内部 RRAM 1524KB（nRF54L15）

> ⚠️ 下表为 nRF52840（1MB Flash）时代的分区方案，**迁移到 nRF54L15（1524KB RRAM）后必须重算**，M1 出表前不得作为实现依据。

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

## 外部 NOR：标定记录（0x10000，1 个扇区）

轨迹环只用前 16 个 4 KB 扇区（`LGPS_RING_SECTOR_COUNT`），第 17 个扇区
（**偏移 `0x10000`，扇区号 16**）单独留给传感器标定记录，由
`Firmware/src/calib/calib_store.c` 管理（Kconfig `CONFIG_LGPS_CALIB_SECTOR_INDEX`，
默认 16，`BUILD_ASSERT` 保证它不低于环的扇区数）。

| 属性 | 值 |
| :--- | :--- |
| 记录大小 | 80 字节（`LGPS_CALIB_RECORD_SIZE`） |
| 每扇区槽位 | 51（`4096 / 80`） |
| 写策略 | append-only：每次保存写下一个未编程槽位，带 magic / format_ver / seq / CRC-16 |
| 满扇区 | 51 次保存后擦除扇区再写第 0 槽（约 45–400 ms 无有效记录窗口，掉电即回落默认值） |
| 读取 | 启动时扫描全扇区，取 seq 最大且 CRC 通过的那条；缺失/损坏不算错误 |

**为什么不用 `CONFIG_SETTINGS` + NVS**：那需要在本文件上面那张内部 RRAM 分区表里
新增 `settings_partition`，而那张表自己写着"nRF52840 时代方案，迁到 nRF54L15 后必须
重算、M1 出表前不得作为实现依据"。为 80 字节去动内部镜像区分区，风险与收益不成比例。
`lifegps/calib_store.h` 只暴露 load/save/erase 三个函数，将来要换后端只换实现。

**谁不能碰这个扇区**：轨迹环（16 个扇区之后才会外扩）、App 的 SYNC/FLASH_* 读取、
`FACTORY_RESET`（将来实现时应把本扇区一并擦除，并在协议文档里写明）。

## 外部 NOR：气压基准记录（0x11000，1 个扇区）

第 18 个扇区（**偏移 `0x11000`，扇区号 17**）独立存放气压基准，由
`Firmware/src/nav/baro_store.c` 管理（Kconfig `CONFIG_LGPS_BARO_SECTOR_INDEX`，默认 17，
`BUILD_ASSERT` 保证它不在环内、也不等于标定扇区）。

| 属性 | 值 |
| :--- | :--- |
| 记录大小 | 20 字节（`LGPS_BARO_RECORD_SIZE`：magic `LBAR` / version / flags / `baseline_pa_q4` / unix_s / CRC-16） |
| 每扇区槽位 | 204（`4096 / 20`） |
| 写策略 | append-only；新的基准才写（`u32` 值变化 > 1 hPa 或手动设定） |
| 读取 | 取最后一条 CRC 通过的记录（槽号大的更新） |

**为什么不和标定记录共用一个扇区**：`lgps_calib_store_erase()` 是“删光整个 4 KB 扇区”，
共用时“清除校准”或“扇区写满后擦除”会默默把高度参考一起丢掉；分成两个扇区后两边互不影响。
