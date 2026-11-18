# GitHub Actions

当前已启用：

| 文件 | 触发 | 作用 |
| :--- | :--- | :--- |
| `protocol.yml` | `protocol/**`、`tools/protocol-gen/**` | 校验协议 YAML：尺寸、偏移、UUID、opcode |
| `docs.yml` | `**/*.md` | 检查相对 Markdown 链接 |
| `app.yml` | `App/**`、`protocol/**` | 构建 `devDebug` APK、单测、lint，上传 artifact |
| `firmware.yml` | `Firmware/**`、`protocol/**`、`tools/protocol-gen/**`、`versions.yaml` | 协议头漂移门禁 + NCS v3.4.2 工具链容器内构建 `lifegps/nrf54l15/cpuapp`、体积门禁（text+data ≤ 712 KB / data+bss ≤ 256 KB）、twister `native_sim` 单测、上传 hex/elf/map |

待加入：

| 文件 | 触发 | 作用 |
| :--- | :--- | :--- |
| `release.yml` | tag `app-v*` / `fw-v*` / `hw-v*` | 签名 APK/AAB、固件 hex/bin/DFU、校验和、GitHub Release |

> `firmware.yml` 使用官方镜像 `ghcr.io/nrfconnect/sdk-nrf-toolchain:v3.4.2`（与 `versions.yaml`
> 的 `ncs-toolchain-v3.4.2` 对应），NCS 工作区按 revision 缓存；**MCUboot 体积/分区门禁**
> 仍待 `Firmware/docs/PARTITIONS.md` 的 M1 重算表定稿后补入。

## 只使用 GitHub 编译

- 本地只编辑源码，不要求安装 Android SDK / Gradle。
- 每次 push 或 PR，`app.yml` 自动构建。
- 构建成功后，在 Actions run 页面底部下载 `app-dev-debug` artifact，得到 APK。
- 手机安装：
  ```bash
  adb install -r app-dev-debug.apk
  ```
  或把 APK 传到手机手动安装。

## 原则

- 按路径分流，不做无意义的全量构建。
- 每个 workflow 加 `concurrency`，同分支新提交取消旧运行。
- 签名密钥只放 GitHub Environment `release` secrets。
- 公开仓 Actions 免费；私有仓注意额度。
