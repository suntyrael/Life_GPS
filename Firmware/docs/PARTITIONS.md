# Flash 分区与 OTA（nRF54L15 Main_V0.2 + 32 MB / 64 MB 外部 NOR 兼容）

> 本文件记录 **M5 最终落地** 的内部 RRAM 与外部 SPI NOR 分区契约，所有固件编译与 OTA 流程均以此为准。

## 1. 为什么采用内部 RRAM 双槽（而非外部 Flash slot1）

nRF54L15 内置 **1524 KB RRAM**，而 Life_GPS 生产固件体积约 260–320 KB。将 MCUboot 主备双槽（`mcuboot_primary` 与 `mcuboot_secondary`）和 `settings_storage` 全部置于内部 RRAM 具有三大优势：

1. **100% 兼容 32 MB（W25Q256）与 64 MB（W25Q512）两版硬件**：外部 Flash 末尾偏移不再硬编码 `0x3F00000`，外部 NOR 全部用于轨迹环、标定/气压/配置与 LGD1 归档日志，容量由 `CONFIG_SPI_NOR_SFDP_RUNTIME=y` 运行时探测。
2. **零外部 Flash 掉电/DPD 变砖风险**：MCUboot 无需初始化外部 SPI NOR 或在看门狗复位后发送 `0xAB` 唤醒 DPD 即可完成镜像校验与 `SWAP_USING_MOVE` 回滚。
3. **快速安全升级**：内部 RRAM 无需扇区擦除时间开销，通过 SMP over BLE（`CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y` + `CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_ENCRYPT=y`）加密上传，新固件在 `main.c` 完成全部子系统初始化后调用 `lgps_ota_confirm_boot()` 自确认；若初始化异常则看门狗复位并自动回滚旧版本。

## 2. 内部 RRAM 1524 KB 分区（官方 `vendor/nordic/nrf54l15_cpuapp_partition.dtsi`）

通过 `Firmware/sysbuild.conf`（`SB_CONFIG_BOOTLOADER_MCUBOOT=y`）与 `Firmware/boards/lifegps/lifegps/lifegps_nrf54l15_cpuapp.dts` 引入官方 `#include <vendor/nordic/nrf54l15_cpuapp_partition.dtsi>`，分配如下精确布局：

| 分区节点 | 偏移 | 大小 | 后端 / 算法 | 用途 |
| :--- | :--- | :--- | :--- | :--- |
| `boot_partition` (`mcuboot`) | `0x000000` | 62 KB | Ed25519 + SHA-512（内置公钥，无需 KMU OTP 烧录） | 不可变引导加载器 |
| `slot0_partition` (`image-0`) | `0x010000` | 712 KB | RRAM，CI 体积门禁上限 **712 KB** | 当前运行固件 |
| `slot1_partition` (`image-1`) | `0x0C2000` | 712 KB | RRAM，`SWAP_USING_MOVE`（无需 scratch 分区） | SMP over BLE OTA 新镜像暂存与回滚槽 |
| `storage_partition` (`storage`) | `0x174000` | 36 KB | Zephyr ZMS (`CONFIG_SETTINGS_ZMS=y`) | BLE 配对/绑定密钥持久化 (`CONFIG_BT_SETTINGS=y`) |

### 烧录与 OTA 产物说明（Actions `lifegps-firmware-{prod,debug}`）

| 文件 | 用途 |
| :--- | :--- |
| `merged.hex` | **首次 J-Link 烧录**（包含 MCUboot + 已签名应用镜像） |
| `Firmware/zephyr/zephyr.signed.bin` | **Android App BLE SMP OTA 升级文件**（由 `OtaUpdateScreen` 校验 MCUboot 魔数 `0x96F3B83D` 后通过 SMP 上传） |
| `Firmware/zephyr/zephyr.signed.hex` / `mcuboot/zephyr/zephyr.hex` | 分离的已签名应用镜像与 MCUboot 镜像 |
| `Firmware/zephyr/zephyr.elf` / `zephyr.map` | 符号表与体积门禁检查 |

## 3. 外部 SPI NOR 分区（32 MB W25Q256 / 64 MB W25Q512 动态兼容）

外部 SPI NOR 挂在 `spi00`（8 MHz，CS `P2.05`，4 KB 扇区），所有模块均通过 `flash_get_page_info_by_offs` / `flash_get_size` 读取 SFDP 实际容量，**绝不硬编码 64 MB 地址**。自 M5 起 MCUboot `slot1_partition`（712 KB）位于内部 RRAM，外部 NOR 为 OTA 预留字节数为 **`0`**（`LGPS_EXT_FLASH_OTA_SLOT1_BYTES = 0`），整颗 32 MB / 64 MB 外部 NOR 容量全部服务于轨迹暂存、持久化参数与日归档。

| 扇区号 | 字节偏移 | 大小 | 所属模块 | 策略与说明 |
| :--- | :--- | :--- | :--- | :--- |
| `0..15` | `0x00000..0x0FFFF` | 64 KB | `flash_ring.c` | 实时 32 B 轨迹环形暂存区（每扇区 127 条 + 16 B `TRAC` 扇区头，共 2032 条；>=75% 水位即 1524 条触发后台压缩刷盘） |
| `16` | `0x10000..0x10FFF` | 4 KB | `calib_store.c` | 传感器标定记录（80 B append-only，51 槽/扇区，带 CRC-16） |
| `17` | `0x11000..0x11FFF` | 4 KB | `baro_store.c` | 气压基准记录（20 B append-only，204 槽/扇区，带 CRC-16） |
| `18..19` | `0x12000..0x13FFF` | 8 KB | `app_config_store.c` | 设备配置 TLV 存储区（`CFG1` 魔数，48 B append-only + CRC-16，覆盖全部 11 项 Config TLV；当前主写扇区 18，扇区 19 为配置扩展预留） |
| `20..35` | `0x14000..0x23FFF` | 64 KB | `journal_store.c` | 按天归档目录 Bank A（`JNL1` 魔数 + 代际号 + CRC-16/CCITT-FALSE，每条目 28 B，最多 **2048 天 / ~5.6 年**） |
| `36..51` | `0x24000..0x33FFF` | 64 KB | `journal_store.c` | 按天归档目录 Bank B（`JNL1` + CRC-16，与 Bank A 组成多扇区 Ping-Pong 原子切换，写完校验通过才切主） |
| `52..END` | `0x34000..(SFDP_bytes - 1)` | 全量可用（~31.8 MB @ 32 MB 或 ~63.8 MB @ 64 MB；外部 OTA `slot1` 预留 = `0`） | `journal_store.c` | 明文 `LGD1` 日归档包区（4 KB 对齐；单日首归档 ≤256 条采用 32 B Byte-Delta + Heatshrink W=8/L=4 压缩，超 256 条或追加合并采用无截断流式 `RAW`；含回读整包 CRC-32 校验与满盘最旧日包轮转回收） |

### 关键保护不变量

- `calib_store`（16）、`baro_store`（17）、`app_config_store`（18..19）、`journal_index` Bank A/B（20..35 / 36..51）与 `LGD1` 数据区起点（`0x34000`）均通过编译期常量与 `BUILD_ASSERT` 保证互不重叠且不侵入轨迹环（0..15）。
- 外部 NOR 的 `ota_slot1_bytes` 固定为 `0`，`traj_bytes == flash_bytes`（由 SFDP 动态探测）；MCUboot `slot1_partition` 独占内部 RRAM `0x0C2000..0x173FFF`。
- `FACTORY_RESET`（`0x51`）会统一擦除配置、标定、气压基准、轨迹环与双 Bank `JNL1` 日归档索引，使设备回到出厂默认状态。
- 详细架构背景参见 [Dev_Log_code_by_agy.md](../../Doc/Software/Dev_Log_code_by_agy.md)。
