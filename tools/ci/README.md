# tools/ci

| 脚本 | 作用 |
| :--- | :--- |
| `check_links.py` | 检查 Markdown 相对链接 |
| `check_fw_size.py`（待做） | 固件镜像/MCUboot/外部 slot1 偏移门禁 |
| `release_checksums.sh`（待做） | Release 资产 SHA256/SBOM |

## 本地运行

```bash
python3 tools/ci/check_links.py
```

## CI

- `.github/workflows/docs.yml` 跑链接检查。
- `.github/workflows/protocol.yml` 跑协议校验。
- App/Firmware 脚手架完成后加入构建与体积门禁。
