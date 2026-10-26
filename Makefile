# Life_GPS 单仓入口
#
# 当前只有文档与协议校验；App/Firmware 脚手架完成后补齐 build/test 目标。

.PHONY: help proto proto-check docs-check app app-test fw fw-test

help:
	@echo "Life_GPS Makefile"
	@echo ""
	@echo "  make proto-check   校验 protocol/lifegps_protocol.yaml"
	@echo "  make docs-check    检查 Markdown 相对链接"
	@echo "  make app           构建 Android devDebug（待脚手架）"
	@echo "  make app-test      Android 单测/lint（待脚手架）"
	@echo "  make fw            Zephyr 固件构建（待脚手架）"
	@echo "  make fw-test       native_sim/twister 测试（待脚手架）"

proto-check:
	python3 tools/protocol-gen/validate.py

proto: proto-check

docs-check:
	python3 tools/ci/check_links.py

app:
	@echo "App scaffold is not generated yet."
	@echo "Read: Doc/Software/Android_Dev_Guide.md and App/README.md"

app-test: app

fw:
	@echo "Firmware scaffold is not generated yet."
	@echo "Read: Firmware/README.md and Firmware/docs/PARTITIONS.md"

fw-test: fw
