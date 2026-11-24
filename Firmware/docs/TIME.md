# 系统时钟（软件 RTC）语义

> 状态：固件侧已实现，编译与单测由 GitHub Actions 判定。本机只做零依赖语法检查，
> 不执行 `west`/`cmake`/`ninja`/`twister`（见 `AGENTS.md` §2）。

## 1. 它是什么

一个**持久于本次上电**的软件时钟，不是外设 RTC：

- 时间基准仍然是项目统一的“Life_GPS 2020 epoch”秒（2020-01-01T00:00:00Z = 0），
  与轨迹记录、GNSS UTC、BLE Status 帧使用同一套计数；
- 上电初值取**本镜像的编译时间**：`Firmware/src/util/time.c` 解析 C 标准固定的
  `__DATE__`（`"Mmm dd yyyy"`）与 `__TIME__`（`"hh:mm:ss"`），不读任何 RTC 寄存器，
  也不依赖 libc `<time.h>`；
- 之后按调用方传入的 `lgps_diag_uptime_ms()` 走时；uptime 的 32 位回绕用无符号减法处理，
  因此 49.7 天回绕不会让时间倒退；
- 收到 GNSS NAV-PVT 的有效 UTC 后自动锚定；之后即使定位丢失，时钟继续按 uptime 走。

代码位置：

| 文件 | 作用 |
| :--- | :--- |
| `Firmware/include/lifegps/time.h` | 纯 C API，零 Zephyr 依赖 |
| `Firmware/src/util/time.c` | 日历换算 + 软件时钟状态 |
| `Firmware/src/main.c` | 上电调用 `lgps_time_init()` |
| `Firmware/src/gnss/gnss.c` | 每个有效 UTC 历元调用 `lgps_time_sync()` |
| `Firmware/src/diag/telemetry.c` | 1 Hz `SYS` 行追加 `clk=` 与 `sync=` |
| `Firmware/tests/lifegps.time` | native_sim 单测 |

## 2. 语义

1. `lgps_time_init()` 在 `main()` 开头、日志横幅之前调用一次。播种成功后
   `lgps_time_now_utc_s()` **立即可用**，但 `lgps_time_synced() == false`。
   “播种”不算同步。
2. `lgps_time_sync(utc_epoch_s, uptime_ms)` 每收到一个有效 NAV-PVT UTC 就调用一次。
   接受后记录锚点：`now = sync_epoch_s + (uptime_ms - sync_uptime_ms) / 1000`。
   每个历元都重锚，不做“漂移小于 X 才同步”的判断。
3. 不可信的 UTC 不锚定，时钟保持原值：epoch 为 0，或与当前时钟相差超过 10 年
   （`LGPS_TIME_MAX_SYNC_SKEW_S`）。`gnss.c` 用 `LOG_WRN` 报告，限频 5 s 一条。
4. 本地时间 = UTC + 偏移，默认 **UTC+8**，范围 **UTC-12h .. UTC+14h**，
   越界返回 `-EINVAL`。负偏移不会让 epoch 下溢（项目时间范围 2020..2156）。
5. `lgps_time_now_utc_s()` / `lgps_time_now_local_s()` 返回 0 表示“时钟尚不可用”。
   只有编译时间无法解析且还没有 GNSS UTC 时才会出现；正常镜像一上电就可用。

## 3. 编译时间 ≠ 真实时间

`__DATE__` / `__TIME__` 是**编译器构建这个镜像的时刻**，不是设备上电时刻，也不是
真实世界时间，它可能带来以下偏差：

- CI 构建机与运行时区无关：`__TIME__` 是构建机本地时间，但本项目按 UTC 解释；
  构建机时钟若未同步 UTC，初值就会整体偏移；
- 镜像烧录后放置多久才上电，时钟就从那个旧时间继续走，不会自动追上；
- 晶振频差会让它慢慢漂移，软件时钟没有温补或校准；
- 重新编译会改变“出厂时间”。

因此它只适合：

- 在 GNSS 首次定位前给日志/记录一个**大致可读**的时间；
- 让“同一次上电内”的时间先后顺序可比较；
- GNSS 定位后作为真实 UTC 的锚点。

**不要**把它当作校准过的时间源，也不要用它做跨设备对时。需要真实时间时以
GNSS UTC 为准；没有 GNSS 时应在 App/文档中明确“这是构建时间”。

## 4. 与 BLE 协议的关系

**没有改变任何线上语义。** Status 帧里的 `utc_epoch_s` / `utc_valid` 仍然表示
“接收机这一历元报的 UTC（是否有效）”，不是“固件时钟已同步”。系统时钟只是固件
内部状态和诊断日志；本次改动不碰 `protocol/lifegps_protocol.yaml`、不碰 App。

## 5. 诊断日志

1 Hz 的 `lgps_tele` `SYS` 行在原有字段之后追加两个字段，既有字段顺序不变：

```text
SYS bat_mv=4021 chg=1 keys=0,0 zupt=move var_mg2=30 steps=0 ring_seq=1 ring_rec=0 \
    clk=15:20:01+08:00 sync=0
```

- `clk=` 本地 `HH:MM:SS` 与偏移；时钟不可用时为 `---`；
- `sync=1` 表示本次上电至少接受过一次 GNSS UTC，`0` 表示当前仍使用编译时间；
- 该行每 1 s 一条，不新增日志行，不改变其他行的节奏。

## 6. 单测覆盖

`Firmware/tests/lifegps.time/src/main.c` 新增：

- `__DATE__` / `__TIME__` 解析与范围检查，播种后初值等于编译时间；
- 播种后走时（含 4999/5000 ms 秒边界）；
- GNSS 同步覆盖初值，以及同步后“丢星仍走时”；
- 默认 +8、0、+1、-2、最小/最大偏移与越界 `-EINVAL`；
- uptime 32 位回绕（锚点在 `0xFFFFFF00`，跨过 0 后仍只前进 4 s）；
- epoch 0、超过 10 年、恰好 10 年边界的接受/拒绝，以及 NULL 保护。
