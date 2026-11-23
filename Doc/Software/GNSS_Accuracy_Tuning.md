# 不改硬件的前提下，怎么让接收机用上更多卫星、把 h/v/p DOP 压下来

结论先写：**能动的只有接收机的信号配置、解算门限和固件策略**。天线、地平面、LNA、
供电都没变，所以下面每一条都只改 `MAX-M10S` 的 `CFG-*` 或固件的采集策略，
收益可以用 App GPS 页的 `# Sats`、`H/V DOP`、`PDOP` 直接对照。

数据来源只有两个，页码/章节按仓库里的 PDF：

- `Doc/Hardware/Datasheets/UBLOX/MAX-M10S_IntegrationManual_UBX-20053088.pdf`（下称 **IM**）
- `Doc/Hardware/Datasheets/UBLOX/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf`（下称 **ID**）

## 1. 四星座并发（收益最大，已做成开关）

IM §2.1.3：MAX-M10S 出厂是 **GPS + Galileo + BeiDou B1I**（QZSS/SBAS 随 GPS 打开）。
四星座并发只有在 **BeiDou B1C** 上才成立，因为 *"BeiDou B1I signal cannot be used
simultaneously with the BeiDou B1C or GLONASS L1OF signals"* —— 也就是说：

```
GPS L1C/A + Galileo E1 + BeiDou B1C + GLONASS L1OF   ← 四种，可用
GPS L1C/A + Galileo E1 + BeiDou B1I + GLONASS L1OF   ← 接收机直接 ACK-NAK
```

所以“加卫星”不是多勾一个星座，而是把 B1I 换成 B1C 再加 GLONASS，四条 `CFG-SIGNAL`
写入必须按这个顺序（每一步本身都合法）：

| 顺序 | 键 | 值 | 作用 |
| :--- | :--- | :--- | :--- |
| 1 | `CFG-SIGNAL-BDS_B1_ENA` | 0 | 关掉 B1I（不先关，后面两步都会被 NAK） |
| 2 | `CFG-SIGNAL-BDS_B1C_ENA` | 1 | 打开 B1C |
| 3 | `CFG-SIGNAL-GLO_L1_ENA` | 1 | 打开 GLONASS L1OF |
| 4 | `CFG-SIGNAL-GLO_ENA` | 1 | 打开 GLONASS 星座 |

IM §2.1.3 还要求 *"Any change to the signal configuration items triggers a restart of
the GNSS subsystem … wait for message acknowledgement and a margin of 0.5 seconds"*，
固件里这四步因此各带 600 ms 间隔（`bringup_profile`，`Firmware/src/gnss/gnss.c`）。

**固件已实现为 Kconfig 开关**，默认打开：

```ini
# Firmware/prj.conf
CONFIG_LGPS_GNSS_FOUR_GNSS=y   # 改成 n 就回到出厂的三星座（GPS+GAL+B1I）
```

代价（IM §2.1.3.1）：B1C 与 B1I 共用中心频率、功耗更低，但**捕获灵敏度与 TTFF 不如
B1I**；再叠加 GLONASS 的接收链，整机平均电流会上升。两种组合在同一片天空下哪个卫星
更多，只能上电实测——App 上对比 `# Sats` 与 `H/V DOP` 就够了，不需要仪器。

## 2. 解算门限（把已经收到的卫星留在解算里）

ID 的默认值表（`CFG-NAVSPG-*`）：`INFIL_MINELEV=5°`、`INFIL_MINCNO=6 dBHz`、
`OUTFIL_PDOP=25.0`。这三个是“哪些卫星参与解算”的闸门：

| 键 | 默认 | 建议 | 说明 |
| :--- | :--- | :--- | :--- |
| `CFG-NAVSPG-INFIL_MINELEV` | 5° | 5°，最多 8° | 抬高会**减少**卫星。低仰角卫星几何上最有用，但多径最重；城市里 8° 常比 5° 稳 |
| `CFG-NAVSPG-INFIL_MINCNO` | 6 dBHz | 6，不要抬 | 抬高等于把弱信号卫星踢出解算，DOP 只会变差 |
| `CFG-NAVSPG-OUTFIL_PDOP` | 25.0 | 25.0 | 这是**输出门限**，超过就不给解，不是精度旋钮 |

App 的「GNSS 设置」页可以逐项读写并回读（`0x35`），所以这三项不需要重新烧固件。

## 3. 动态模型（同样卫星数下更稳）

ID §CFG-NAVSPG-DYNMODEL（表 23）：0=便携、2=静止、3=步行、4=车载……固件的解算滤波
按这个假设调参。步行/随身场景用 **3（步行）** 比默认的 0（便携）在低速时更稳；
放在桌上不动用 **2（静止）** 时水平漂移最小，但一旦移动就会明显滞后——不要长期留在 2。

## 4. 提高导航频率不会改善 DOP

`CFG-RATE-MEAS` 只决定每秒解算几次。DOP 是**几何**量（卫星相对接收机的分布），
1 Hz 与 10 Hz 的 DOP 完全一样（同一片天空）。提高频率换来的是更密的轨迹点和更短的
反应时间，代价是功耗与 UART 带宽。固件现在是 `CONFIG_LGPS_GNSS_RATE_MS=100`（10 Hz）。

## 5. 不成立的几条路（写清楚，免得反复试）

- **双频（L1+L5）**：MAX-M10S 只有 L1，双频是 MAX-F10S/M10N 的事，换模组属于改硬件。
- **SBAS**：IM §2.1.3 说明 SBAS 随 GPS 生效，覆盖区是欧美/日本；在中国境内没有可用
  SBAS 星，打开不增加卫星。
- **QZSS**：只在日本/东亚部分可见。IM 建议 *"enable also the QZSS L1C/A when the GPS
  L1C/A signal is enabled"*，本仓库的 `0x35` 白名单还没有 QZSS 键——加键要动
  `protocol` 文档，按需单独提。
- **AssistNow Autonomous（`CFG-ANA`）**：改善的是**冷启动 TTFF**，不是 DOP。

## 6. 怎么验证改对了

1. 同一天线、同一位置，记录 5 分钟：`# Sats`（NAV-PVT `numSV`）、`PDOP`、`H/V DOP`。
2. 改一项，等接收机重新锁定（信号配置写入会重启 GNSS 子系统），再记 5 分钟。
3. DOP 变小、`numSV` 变大才是收益；`numSV` 变大但 DOP 不变，通常只是多了低仰角卫星。

## 7. 代码位置

| 内容 | 文件 |
| :--- | :--- |
| 四星座写入顺序 | `Firmware/src/gnss/gnss.c`（`bringup_profile`） |
| 开关 | `Firmware/boards/lifegps/lifegps/Kconfig.lifegps`（`LGPS_GNSS_FOUR_GNSS`） |
| 接收机设置白名单 | `Firmware/src/gnss/gnss.c`（`cfg_keys`）、`App/.../ble/GnssCodec.kt`（`GnssCfgKeys`） |
| 气压计 GNSS 门限 | `Firmware/include/lifegps/baro.h`（`LGPS_BARO_GNSS_*`） |
