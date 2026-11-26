# Life_GPS 单仓入口
#
# 本地仅执行协议生成/校验与文档链接检查；固件与 Android 构建/单测由 GitHub Actions 执行。

.PHONY: help proto proto-check docs-check app app-test fw fw-test

help:
	@echo "Life_GPS Makefile"
	@echo ""
	@echo "  make proto         校验 YAML 并重新生成 C 头文件、Kotlin 常量与 Golden Vectors"
	@echo "  make proto-check   校验 YAML 并检查 C/Kotlin/Vectors 漂移"
	@echo "  make docs-check    检查 Markdown 相对链接"
	@echo "  make app / fw      请在 GitHub Actions（app.yml / firmware.yml）查看构建与测试"

proto:
	python3 tools/protocol-gen/validate.py
	python3 tools/protocol-gen/gen_c.py
	python3 tools/protocol-gen/gen_kt.py
	python3 tools/protocol-gen/gen_vectors.py

proto-check:
	python3 tools/protocol-gen/validate.py
	python3 tools/protocol-gen/gen_c.py --check
	python3 tools/protocol-gen/gen_kt.py --check
	python3 tools/protocol-gen/gen_vectors.py --check

docs-check:
	python3 tools/ci/check_links.py

app:
	@echo "Android builds run in GitHub Actions (.github/workflows/app.yml)."
	@echo "Read: Doc/Software/Android_Dev_Guide.md and App/README.md"

app-test: app

fw:
	@echo "Firmware builds and twister tests run in GitHub Actions (.github/workflows/firmware.yml)."
	@echo "Read: Firmware/README.md and Firmware/docs/PARTITIONS.md"

fw-test: fw
