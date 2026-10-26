# Production

## 内容

| 文件 | 说明 |
| :--- | :--- |
| `*-gerber.zip` | Gerber + 钻孔 |
| `*-cpl.csv` | 贴片坐标 |
| `*-bom.csv` | 贴片 BOM |
| `*-assembly.pdf` | 装配图/极性/注意事项 |
| `*-testpoints.pdf` | 产测点位置与夹具说明 |
| `SHA256SUMS` | 所有生产文件校验和 |

## 命名

```text
lifegps-hw-v0.1-gerber-2026xxxx.zip
lifegps-hw-v0.1-cpl-2026xxxx.csv
lifegps-hw-v0.1-bom-2026xxxx.csv
```

## 规则

- 不提交未定版的生产文件；定版后放本目录或 GitHub Release 资产。
- 每次打样记录：板厂、工艺（沉金/阻抗）、层压结构、表面处理、批次。
- 产测点与固件 `FACTORY/SELFTEST` 命令对应；ICT 夹具说明与固件版本绑定。
