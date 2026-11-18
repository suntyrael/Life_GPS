# Flash 分区与 OTA

> 这是固件/硬件/App 的共同契约。改偏移必须同时改 DTS、生成头文件、App 常量和 CI 校验。

## 运行槽与备份槽

**固件最终永远从内部 `slot0` 启动。** `slot1` 只是 OTA 新镜像的暂存/回滚副本：

1. App 通过 SMP 把新镜像上传到外部 `slot1`。
2. 重启后 MCUboot 校验签名，把 `slot1` 镜像 swap 到 `slot0`。
3. MCUboot 从 `slot0` 启动新固件；`slot1` 保留旧镜像/回滚信息。
4. 新固件 confirm 后，`slot1` 可作为下一次 OTA 的暂存区。

`slot1` **不执行**，也不做 XIP；它只在 OTA 期间被 MCUboot 读写。

## 内部 RRAM 1524KB（nRF54L15，NCS v3.4.2 标准布局）

依据 `dts/vendor/nordic/nrf54l15_cpuapp_partition.dtsi` 与 `.github/workflows/firmware.yml` 体积门禁：

| 区域 | 偏移 | 大小 | 说明 |
| :--- | :--- | :--- | :--- |
| `boot_partition` (MCUboot) | `0x00000` | 62KB (`0x0F800`) | 引导加载程序 |
| `slot0_partition` | `0x0F800` | 712KB (`0xB2000`) | 当前运行固件槽；CI 门禁 `text + data <= 712 KB` |
| `slot1_partition`（内部备用） | `0xC1800` | 712KB (`0xB2000`) | 内部双槽备份区（若不启用外部 Flash slot1 时直接可用） |
| `storage_partition` | `0x173800` | 36KB (`0x09000`) | Settings / NVS 绑定与配置存储 |

## 外部 SPI NOR Flash（运行时 SFDP 容量自适应：32MB / 64MB 通用）

固件开启 `CONFIG_SPI_NOR_SFDP_RUNTIME=y`，在挂载时通过 `flash_get_page_count()` 动态获取真实物理容量（`total_bytes`），**不写死 64MB**，从而同时兼容样板贴装的 **32MB (`W25Q256`)** 与 BOM 基线的 **64MB (`W25Q512NW`)**：

| 区域 | 起始偏移 | 大小（计算规则） | 32MB 料 (`W25Q256`) | 64MB 料 (`W25Q512NW`) | 说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 暂存环形队列 | `0x0000000` | 64KB（16 × 4KB 扇区） | `0x0000000 .. 0x0010000` | `0x0000000 .. 0x0010000` | 每日 32B 原始记录磨损均衡轮转区 |
| 轨迹归档与索引区 | `0x0010000` | `total_bytes - 1 MiB - 64 KiB` | 约 30.94 MiB (`0x0010000 .. 0x1F00000`) | 约 62.94 MiB (`0x0010000 .. 0x3F00000`) | App 仅读写轨迹暂存与归档区（`0 .. total_bytes - 1 MiB`） |
| 外部 OTA `slot1` 预留 | `total_bytes - 1 MiB` | 1 MiB (`0x100000`) | `0x1F00000 .. 0x2000000` | `0x3F00000 .. 0x4000000` | 固定预留在芯片最高 1 MiB；应用层 `lgps_ring` 绝不触碰该区域 |

## DTS 配置（`lifegps_nrf54l15_cpuapp.dts`）

> 注意：`jedec,jesd216.yaml` 规定 `size` 属性单位为 **bit**（而非 byte）。512 Mbit = `536870912`（256 Mbit = `268435456`）；开启 `CONFIG_SPI_NOR_SFDP_RUNTIME=y` 后，运行时容量由 SFDP 决定，轨迹上限由 `flash_ring.c` 按 `total_bytes - LGPS_FLASH_OTA_RESERVE_BYTES` 动态计算。

```dts
&spi00 {
	status = "okay";
	pinctrl-0 = <&spi00_default>;
	pinctrl-1 = <&spi00_sleep>;
	pinctrl-names = "default", "sleep";
	cs-gpios = <&gpio2 5 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;

	w25q512: flash@0 {
		compatible = "jedec,spi-nor";
		reg = <0>;
		spi-max-frequency = <8000000>;
		size = <536870912>;
	};
};
```

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
