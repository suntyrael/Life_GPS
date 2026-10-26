# GitHub Actions

当前已启用：

| 文件 | 触发 | 作用 |
| :--- | :--- | :--- |
| `protocol.yml` | `protocol/**`、`tools/protocol-gen/**` | 校验协议 YAML：尺寸、偏移、UUID、opcode |
| `docs.yml` | `**/*.md` | 检查相对 Markdown 链接 |
| `app.yml` | `App/**`、`protocol/**` | 构建 `devDebug` APK、单测、lint，上传 artifact |

待 Firmware 脚手架完成后加入：

| 文件 | 触发 | 作用 |
| :--- | :--- | :--- |
| `firmware.yml` | `Firmware/**`、`protocol/**` | Zephyr 构建、native_sim 测试、MCUboot 体积/分区门禁 |
| `release.yml` | tag `app-v*` / `fw-v*` / `hw-v*` | 签名 APK/AAB、固件 hex/bin/DFU、校验和、GitHub Release |

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
