# AGENTS.md — Life_GPS AI 编码约束（最小集）

> 本文件只写两类内容：**不可逆红线** 与 **本机做不到的事**。
> 可自动判定的规则交给 CI（`.github/workflows/*.yml`），这里不复制它们的阈值。
> 本文件**不记录项目当前状态**（哪个 workflow 存在、源码写到哪、分区多大、清单里有什么）。
> 状态一律现查现说，命令见 §4。事实会腐烂，命令不会。

## 1. 变更边界（不可逆，违反前必须先问）

1. **不静默修改已有文件。** 新增前先列出路径清单；改已有文件先说明改哪几处、为什么。
   改完用 `git status --short` 复核，确认没有意外改动。
2. **未经明确批准不得改动**：`.github/workflows/**`、`versions.yaml` 的
   `revision` / `sdk` / `kernel`、`protocol/lifegps_protocol.yaml` 的帧 / 记录 / 包布局、
   硬件料号与引脚定义。CI 是本项目唯一的编译、单测、体积门禁，削弱它等于失去全部验证手段。
3. **不用 `git commit` / `git push` / `git checkout` / `git restore` 代替授权**；
   不 `git push --no-verify`。
4. **一次只做一个需求**，不顺手重构无关模块。
5. **生成物不手改**（例如 `Firmware/include/generated/lifegps_protocol.h`）：改输入后重新生成，
   由 CI 的漂移检查判定（§4）。确需例外时在当次交付里点名说明，不引入代码内豁免标记机制。

## 2. 固件红线

- **本地不执行构建类命令**：`west build`、`cmake`、`ninja`、`twister`、`./gradlew`、`make fw`。
  PATH 上虽然有 `west` / `cmake` / `ninja`，但本机**没有 NCS/Zephyr 源码树，也没有
  `arm-zephyr-eabi-gcc`**，跑起来只会得到误导性的失败。编译与单测一律由 Actions 负责。
- **写 Zephyr/NCS 代码前必须有可核对来源**：仓库内既有写法（`Firmware/boards/**`、
  `Firmware/prj.conf`、相邻驱动）或 `Doc/Hardware/Datasheets/*.pdf` 的**章节 + 表号**。
  本机没有 Nordic MCP、没有 nrf-mcp、没有可 grep 的 NCS 源码树 —— 拿不到来源就写
  「本机无法核实」，**不要凭记忆写 `CONFIG_*`、devicetree 属性、board target、寄存器位**。
- **目标芯片 `NRF54L15-QFAA-R`**（QFN48）。禁止把 nRF52 系列（nRF52840 等）的寄存器、
  board target、Kconfig、overlay、S1xx SoftDevice API 混入。BLE / Thread / MCUboot / TF-M
  一律用 NCS（Zephyr）的 C 实现，不自造协议栈。
- **C 侧底线**（其余风格照 Zephyr 与仓库现有代码，不逐条自证）：不引入动态内存、VLA、
  `<stdarg.h>` / `<setjmp.h>`；不吞掉返回错误的函数；外部输入（BLE / 串口 / 传感器 / OTA）
  在用作下标、长度、循环边界或指针偏移前先校验范围与空指针。

## 3. 硬件口径

- 引脚与电平：起点是 `Doc/Hardware/Pinmap_Main_V0.2.md`，配合 `Hardware/Schematic/` 下的网表。
- **网表与实测 / 装配记录冲突时，不要按网表静默写代码 —— 先问用户。**
  本项目已出现同一颗 IMU 被记成三个不同料号的情况，硬排序会直接产出错驱动。
- 新增 `prj.conf` / `*.dts` 配置项必须写明依据；引脚、电平、供电有疑问一律问，不猜。
- 涉及低功耗或 ISR 的改动，列出唤醒源、共享变量的保护方式、以及 RAM / flash 增量。

## 4. 现查现说（陈述前先执行，不要凭记忆）

| 想知道 | 执行 |
| :--- | :--- |
| 固件是否已落地 | `ls Firmware/CMakeLists.txt Firmware/prj.conf Firmware/west.yml` |
| 有哪些 CI 门禁 | `ls .github/workflows/` |
| 本次改动会触发什么 | 现读对应 `.yml` 的 `on.push.paths`，**不要引用任何清单** |
| 工具链锁定的版本 | `sed -n '1,30p' versions.yaml` |
| 改协议后自检 | `pip install -r tools/protocol-gen/requirements.txt`，然后 `python3 tools/protocol-gen/validate.py` 与 `python3 tools/protocol-gen/gen_c.py --check` |
| 改 Markdown 后自检 | `python3 tools/ci/check_links.py` |
| 编译 / 单测 / 体积是否过 | 本机不做。由 `firmware.yml`（NCS 容器构建 + `twister -p native_sim` + 体积门禁）判定，结果只看 Actions |

## 5. 输出契约（每次改动末尾，≤5 行）

```
改动：<新增/修改了哪些文件>；动了已有文件的，注明用户批准依据
自检：<实际执行的只读命令与结果>；本机未构建
触发：<现读 YAML 得出的 workflow>；未验证项以 Actions 结果为准
待确认：<仅列本次真正涉及的：ISR/共享状态 · DMA 所有权 · 时钟域/errata · RAM/flash 增量 ·
        料号 · API 来源>；没有就写「无」
```

不要替用户断言 CI 已通过，不要复述本文件内容。

## 6. 加载说明

Pi 从工作目录及其父目录加载本文件。cwd 为 `/root` 时不会自动加载，用
`@/root/projects/Life_GPS/AGENTS.md` 显式引用；改完本文件后在 pi-web 执行 `/reload`。
