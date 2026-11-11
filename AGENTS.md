# AGENTS.md — Life_GPS 项目 AI 编码约束（单文件版）

> 本文件是 Pi / pi-web 的 context file，会自动被发现（见文末「引用确认」）。
> **作用域**：仅当任务涉及 `Firmware/**`、nRF54L15 / nRF Connect SDK / Zephyr C、
> BLE 协议或 `protocol/**` 时适用。处理 App / 文档 / 硬件结构任务时，只遵守 §1 的
> 「变更边界」与 §5 输出契约，其余规则不适用。

---

## 0. 项目事实（AI 必须先读，不得假设）

| 项 | 实际状态（2026-11-11 核对） |
|---|---|
| 仓库根 | `/root/projects/Life_GPS`（pi-web 会话目录 `--root-projects-Life_GPS--`） |
| 远端 | `https://github.com/suntyrael/Life_GPS.git`，分支 `main` |
| 版本锁定 | `versions.yaml` **已锁定**：`distribution: nrf-connect-sdk`、`revision: v3.4.2`（LTS）、`sdk: ncs-toolchain-v3.4.2`、`kernel: 4.4.2` |
| 硬件 | **Main_V0.2**（2026-11-11 原理图 `Hardware/Schematic/Life_GPS_V0.2_SCH.pdf` + 网表 `Hardware/Schematic/Netlist_Main_V0.2_2026-11-11.tel`）；引脚 SSOT = `Doc/Hardware/Pinmap_Main_V0.2.md` |
| 固件状态 | `Firmware/` **只有 README/docs/tests 的骨架**，无 `CMakeLists.txt`、无 `west.yml`、无 `prj.conf`、无源码 |
| 芯片 | ✅ **已确认：Nordic `NRF54L15-QFAA-R`（nRF54L15-QFAA，QFN48，49 脚含 EP）**。规格书 `Doc/Hardware/Datasheets/nRF54L15_nRF54L10_nRF54L05_Datasheet_v1.0.pdf`。nRF52840 仅出现在 V0.1 历史文档中，**不得用于新代码/新配置** |
| 现有 CI | `.github/workflows/`：`protocol.yml`、`docs.yml`、`app.yml`。**没有 `firmware.yml`** |
| 本地编译 | **无**。项目约定「只使用 GitHub 编译」（`.github/workflows/README.md`）。本地不装 Android SDK / Gradle / west / Zephyr SDK |
| 固件 CI 计划 | `.github/workflows/README.md` 列了待加的 `firmware.yml`（Zephyr 构建、native_sim、MCUboot 体积/分区门禁）与 `release.yml`，**均尚未建立** |

**推论（必须遵守）**：当前任何固件改动都**不会触发编译门禁**。AI 不得声称「已编译通过」「已验证」，
必须在输出中明确标注「本次改动未经编译验证」，并提示用户是否需要新增 `firmware.yml`（新增文件也需先确认）。

**引脚与硬件口径（2026-11-11 起）**：任何引脚/电平/供电相关结论，一律以
`Doc/Hardware/Pinmap_Main_V0.2.md`（引脚 SSOT）+ 最新网表 + 原厂规格书为准；文档之间冲突时按此顺序裁定。

---

## 1. 变更边界（最高优先级，用户 2026-11-05 明确要求）

- **不得修改仓库中任何已有文件**，除非用户逐项明确批准。
  尤其不得擅自改动：`versions.yaml`、`Makefile`、`README.md`、`Firmware/README.md`、
  `protocol/lifegps_protocol.yaml`、`.github/workflows/*`、`.gitignore`、`tools/**`、`App/**`、`Doc/**`、`Hardware/**`。
- 允许**新增**文件，但新增前须在输出中列出将要创建的路径清单；若新增的是
  `.github/workflows/**` 或改动 `versions.yaml` 的锁版本，**必须先单独询问用户并等待确认**。
- 不允许用 `git commit` / `git push` / `git checkout` / `git restore` 代替确认；不允许 `git push --no-verify`。
- 生成/修改任何文件后，用 `git status --short` 复核，确认没有意外改动已有文件。
- 一次变更只解决一个需求（Zephyr Dir 3.1 可追溯性），不顺手重构无关模块。

---

## 2. 编译与验证契约（本地零编译 → GitHub Actions）

- **禁止**在本地执行 `west build`、`cmake`、`ninja`、`make fw`、`./gradlew`、
  `cargo build`、`twister` 等构建类命令；本机没有工具链，执行只会产生误导性失败。
- **允许**只读自检（`git status`、`git diff`、`grep -R`、`python3 tools/protocol-gen/validate.py`、
  `python3 tools/ci/check_links.py`）——这些不编译，且是现有 CI 的同款检查。
- AI 输出中必须写明：
  1. 本次改动预计触发哪个 workflow（按 `.github/workflows/*.yml` 的 `on.push.paths` 判断）；
  2. 若涉及 `Firmware/**`：**明确警告「仓库无 firmware.yml，改动不会被编译」**；
  3. CI 待检查项清单，以及「本地未编译，最终以 Actions 结果为准」。
- push 后由用户在 Actions 页面看结果；AI 不得替用户判断 CI 已通过。
- `versions.yaml` 已锁定 **NCS v3.4.2 / toolchain ncs-toolchain-v3.4.2 / kernel 4.4.2**；生成代码必须相对该版本查证。
  若将来出现 `revision`/`sdk` 被改回 `TBD` 的情况，**拒绝生成依赖具体 NCS/Zephyr 版本的代码**（对应 §3 META-02）。

---

## 3. 硬规则（must）

格式：`ID · 级别 · 依据 · 要求 · 判定方式`。级别只用 `must` / `recommended` / `may`。

### META（AI 特有）

- **META-01 `must`** 规则必须可判定。不能自动/人工判定的不标 `must`。依据：Google Verilog Style Guide 术语分级。
- **META-02 `must` · 不得编造 API** 所有 Zephyr/NCS API、`CONFIG_*`、devicetree 属性、
  board target、头文件必须来自：① Nordic MCP（`https://aidev.nordicsemi.com/mcp`，
  工具 `nordicsemi_search_sources` / Kconfig 查询）；② 社区 `nrf-mcp`（`nrf_search`/`nrf_kconfig`，
  用 `NRF_SDK_REF` 锁定版本）；③ 已锁定 NCS 源码树 `grep -R`，回答给出 `路径:行号`。
  禁止凭记忆写、禁止把 nRF52 S1xx SoftDevice 套到 nRF54L、禁止混入其它 RTOS/厂商 SDK。
  **判定**：`versions.yaml` 未锁定版本 → 直接拒绝生成；CI 编译失败视为编造兜底。
- **META-03 `must` · 例外可搜索且可过期** 任何绕过必须紧邻代码写：
  `/* WAIVER <RULE-ID>: <技术理由> | owner: <name> | expires: YYYY-MM-DD | ticket: <ID> */`
  四字段缺一即违规。禁止用 `#pragma`、`// NOLINT`、`-Wno-*`、改 CI、加白名单替代。
  依据：Verilog「Justify all exceptions + lint waiver」、Rust `// SAFETY:`、Python pylint 抑制理由。
- **META-04 `must` · 生成代码可复现** 由 `tools/protocol-gen` 等生成的文件必须提交输入与生成命令、
  幂等、且生成结果同样过本约束。依据：Verilog Generated Code。
- **META-05 `must` · TODO 格式** `/* TODO(<ticket>): <可验收完成条件> */`；禁止 `TODO(username)` / 裸 `TODO:`。
- **META-06 `must` · 不留死代码** 不留注释掉的代码、不留「以后可能用」的分支（Zephyr Dir 4.4 / Rule 2.2）。
- **META-07 `recommended` · 包容性命名** 不引入 `master/slave`、`blacklist/whitelist`、`sanity`；
  SPI 用 `controller/peripheral`/`SDO/SDI/CS`，I2C 用 `controller/target`（Zephyr Additional Rule A.2）。

### C 语言（依据：Zephyr Coding Guidelines = MISRA-C:2012 子集；Zephyr C Style；CERT C）

- **C-ERR-01 `must`** 返回错误信息的函数必须检查（Dir 4.7 / Rule 17.7）。
  `int ret = gpio_pin_set_dt(...); if (ret) { LOG_ERR(...); return ret; }`。
  禁止 `gpio_pin_set_dt(&led, 1);` 裸调用。**判定**：clang-tidy / CI 编译 + 人工评审。
- **C-ERR-02 `must`** 外部来源（BLE/串口/传感器/OTA/flash/shell）用作下标、长度、循环边界、
  指针偏移、枚举转换前必须校验范围与空指针（Dir 4.11 / Dir 4.14 / CERT ARR30-C）。
- **C-MEM-01 `must`** 禁 `malloc/calloc/realloc/free/aligned_alloc/alloca`、VLA、`<stdarg.h>`、
  `<setjmp.h>`；自动存储期变量读取前必须赋值（Dir 4.12 / Rule 9.1/18.8/21.3、Zephyr A.4/A.5）。
  用 `static uint8_t rx_buf[CONFIG_...];` + 聚合初始化加花括号（Rule 9.2）。
  **判定**：只读 `grep -RInE '\b(malloc|calloc|realloc|free|aligned_alloc|alloca|va_list|setjmp)\s*\('`。
- **C-MEM-02 `must`** 无 UB：禁有符号溢出、越界移位、依赖求值顺序的副作用；
  指针算术不得越出同一数组（Dir 1.1 / Rule 1.3 / Rule 18.1-18.3）。
- **C-TYP-01 `must`** 用 `<stdint.h>` 精确宽度类型；不用裸 `char/short/long` 表达宽度；
  `unsigned` 只用于位模式/模运算/寄存器位域，不表达「非负」；整型常量带 `U` 后缀，禁八进制
  （Dir 4.6 / Rule 7.1/7.2 / Rule 10.x；迁移自 Google C++「Integer Types」）。
  **判定**：CI `-Wconversion -Wsign-conversion -Wshadow -Wundef -Werror`。
- **C-TYP-02 `must`** 命名带单位/量纲：`timeout_ms`、`clock_hz`、`len_bytes`、`voltage_mv`；
  禁止 `data`/`value`/`buf` 无语义名；位操作显式加括号（Rule 12.1），移位量在 `0..width-1`（Rule 12.2）。
- **C-CTL-01 `must`** 所有 `if/else/do/while/for/switch` 体必须复合语句（Rule 15.6，Zephyr 风格）；
  `if...else if` 以 `else` 结束（Rule 15.7）；`switch` 必须有 `default`、每 case `break`（Rule 16.3/16.4）；
  布尔上下文用显式比较（Rule 14.4）；禁递归（Rule 17.2）。
- **C-MAC-01 `must`** 能用函数就不用函数式宏（Dir 4.9）；宏参数展开加括号（Rule 20.7）；
  不在头文件条件编译函数/结构体声明（Zephyr A.1），优先 `if (IS_ENABLED(CONFIG_FOO)) {}`；
  不得给 `MIN/MAX/ARRAY_SIZE` 加私有前缀或 `#undef`（Zephyr A.3）。
- **C-HDR-01 `must`** 头文件自包含 + include guard（Dir 4.10）；内部链接一律 `static`（Rule 8.8）；
  函数原型带命名参数（Rule 8.2）；外部对象只定义一次。迁移自 Google C++「Self-contained Headers / IWYU」。
- **C-STY-01 `must`** Zephyr C 风格：8 字符 tab、`snake_case`、≤100 列、
  所有控制流加 `{}`、**仅 `/* */` 注释（禁 `//`）**、`/** */` 用于 doxygen、
  避免 `0b` 字面量与非 ASCII 符号、注释中名词保持正确大小写（`UART`/`CMake`/`devicetree`）、
  注释写「为什么」不复述代码。**判定**：只读 `grep -RIn '//'`、`awk 'length>100'`、CI `checkpatch.pl` + `clang-format`。
- **C-STY-02 `must`** 标识符不冲突、内层不遮蔽外层、宏名与标识符区分（Rule 5.x）。
- **C-DOC-01 `recommended`** 汇编用法、实现定义行为、公开函数的前置条件/返回值/失败原因/
  「是否 ISR 可调用」必须文档化（Dir 4.2；迁移自 Rust 指南 document invariants）。

### 硬件（编译器与语言都管不了，必须人工确认）

- **HW-01 `must`** ISR 共享变量须 `volatile` + 明确保护机制（`atomic_t`/`k_mutex`/`irq_lock()`）
  并注释保护方式；ISR 内禁阻塞、禁分配、禁非 ISR-safe API、禁重日志；注明触发源/优先级/WCET。
- **HW-02 `must`** DMA buffer 注明所属外设、SRAM 位置、对齐、cache 一致性、生命周期；
  禁止栈上 buffer 交给 DMA；禁止 cast 移除 `const`/`volatile`（Rule 11.8 / CERT EXP32-C）；
  外设单一 owner，init→config→enable→use→disable 顺序正确（Dir 4.13）。
- **HW-03 `must`** 每个外设记录时钟域、时钟源（LFXO/HFXO/RC）、Secure/Non-Secure 归属、
  已查证的 errata 编号（或明确「已查无相关 errata」+ 来源）；`*.overlay`/`prj.conf`
  每项配置注明依据；低功耗变更列唤醒源与 PPK2 实测电流。
- **HW-04 `must`** 禁大栈帧/递归；新增全局缓冲区须给出 RAM 增量字节数；
  新增 `CONFIG_*` 须给出 flash 增量与依据。
- **HW-05 `must`** BLE / Thread / Zigbee / Matter / MCUboot / TF-M / secure boot 一律走
  nRF Connect SDK（Zephyr）的 C 实现；不得自造协议栈，不得套用 nRF52 S1xx SoftDevice API。
- **HW-06 `must`** 目标芯片已确认为 **`NRF54L15-QFAA-R`**（§0）。
  仍**禁止**把 nRF52 系列（nRF52840/nRF52832 等）的寄存器、board target、Kconfig、overlay、SoftDevice API 混入；
  board target 名称（如 `nRF54L15DK` 在 NCS v3.4.2 下的确切写法）必须用 `west boards` 或已锁定源码树查证后再写。

### CI / 流程

- **CI-01 `must`** 遵守 §2：本地零编译，只做只读自检，输出中标注 CI 待验项。
- **CI-02 `must`** 改动 `protocol/lifegps_protocol.yaml` 或 `tools/protocol-gen/**` 时，
  可本地跑 `python3 tools/protocol-gen/validate.py`（与 `protocol.yml` 同款，不编译）。
- **CI-03 `must`** 新增/改动 `*.md`（含本文件）会触发 `docs.yml`；
  `tools/ci/check_links.py` 只扫描根 `README.md` 与 `Doc/App/Firmware/Hardware/protocol/tools` 下的 md，
  **不扫描根目录 `AGENTS.md`**——但仍应避免在根文件里写相对 Markdown 链接，只用绝对 URL 或代码格式路径。
- **CI-04 `must`** 未经用户确认，不得新增/修改 `.github/workflows/**`。

---

## 4. 明确不导入

- Google **没有 C style guide**（官网是 C++ 与 C#）；C 侧权威依据是 Zephyr Coding Guidelines（MISRA-C:2012 子集）、
  Zephyr C Style、CERT C。Google C++ 只取上面标注「迁移自」的 8 条与 META 元机制。
- Google Rust 中带 `[chrome]/[fuchsia]/[android]` 标签的规则、以及 `nrf-softdevice` 思路（不支持 nRF54L）。
- Verilog / SystemVerilog 指南（本项目不写 RTL）；JSON / XML / Shell / Markdown 指南主体
  （仅保留 META-03/04/05 元机制）。
- MISRA 完整 147+ 条全塞给 AI 无收益；完整合规需 ECLAIR / Polyspace 等商业静态分析。

---

## 5. 输出契约（每次改动末尾固定三段）

```
【规则自评】
- 命中的规则 ID：C-ERR-01, HW-03, CI-01, ...
- 查证来源：Nordic MCP/<tool> 或 <文件路径:行号>；目标芯片：已确认/未确认
- 新增文件清单：<路径>；是否修改已有文件：无 / <需用户确认的清单>
- 新增豁免：无 / WAIVER-xxx（理由、owner、到期日、ticket）

【验证】
- 本地只读自检：PASS / FAIL（附命令与结果）
- 本地编译：未执行（项目约定，按 CI-01）
- 预计触发 workflow：<protocol.yml|docs.yml|app.yml|无>
- 固件改动且无 firmware.yml：是/否（是 → 明确警告「未经编译验证」并询问是否新增 workflow）
- 最终判定：以 GitHub Actions 结果为准

【人工确认项】
- HW-01 ISR/共享状态 | HW-02 DMA 所有权 | HW-03 时钟/S-NS/errata
- HW-04 栈/RAM/flash 预算 | HW-06 芯片型号 | META-02 API 查证证据
```

---

## 6. 引用确认（pi-web 怎么读到本文件）

依据 Pi 官方文档 `docs/configuration.md`「Context files」与 `docs/security.md`：
Pi 从 **agent 目录、工作目录、及其父目录**加载 `AGENTS.md` / `CLAUDE.md` / `AGENTS.override.md`，
且 **context-file discovery 不需要 project trust**；`<cwd>` 目录及其子目录中的会话都会加载。

- ✅ **项目会话**：pi-web 在 `/root/projects/Life_GPS`（或 `Firmware/`、`App/` 等子目录）开会话时，
  本文件**自动加载**。仓库里已有 `--root-projects-Life_GPS--` 会话目录，说明这个 cwd 是正常用法。
- ⚠️ **注意**：若会话 cwd 是 `/root`（`PI_SESSION_FILE` 为 `--root--/...`），`/root/projects/Life_GPS` 是**子目录**、
  不在父目录链上，**不会自动加载本文件**；此时按下列任一方式生效：
  1. 用 `@/root/projects/Life_GPS/AGENTS.md` 显式引用（Pi 支持 `@path` 引用）；
  2. 在 `/root/projects/Life_GPS` 目录新开 pi-web 会话（推荐，零额外改动）；
  3. 或让 AI 读取本文件路径后按内容执行。
- 🔶 **若希望任何 cwd 都能自动加载**，需要额外新增 `/root/.pi/agent/AGENTS.md`（agent 级）或
  `/root/AGENTS.md`（父目录级）作为转发文件 —— 这会全局生效、影响其他项目，
  **按 §1 需先经你确认，我没有擅自创建**。
- 修改本文件后，在 pi-web 里执行 `/reload` 可重新加载 context files。
- 本文件随 `git push` 上传 GitHub，其他机器 / 协作者的 Pi 同样自动发现。

> 注意：`.gitignore` 忽略 `.vscode/` 但**不忽略 `AGENTS.md`**，本文件会被正常跟踪。
