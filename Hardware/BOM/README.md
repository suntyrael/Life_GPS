# BOM

## 基线

以根 [README](../../README.md) §2 的《唯一硬件配置基线表》为准。

## 双 BOM / 兼容位

| 选项 | 装配 | 说明 |
| :--- | :--- | :--- |
| RF Option A | 直通（R28=100Ω，R27=NC） | 默认；无 LNA/SAW |
| RF Option B | LNA ATR2652 + SAW B39162B2651P810 | 增强灵敏度/抗干扰；与 C46/C47 互斥 |
| GNSS 模组 | MAX-M10S（R28） / LC97H（R27） | Pin-to-Pin；固件协议自适应 |

## 规则

- 每个 BOM 行包含：位号、料号、封装、数量、DNP/NC、替代料、装配选项。
- 互斥器件必须在 BOM 与原理图同时标注，禁止同时贴装。
- 关键物料（GNSS、IMU、Flash、DCDC、LDO、充电 IC）记录原厂/代理渠道与批次追溯。
- 导出给贴片厂的 BOM/CPL 必须与 Gerber 同版本，附校验和。
