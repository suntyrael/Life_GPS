# SPDX-License-Identifier: MIT
# Life_GPS - voltage-only state-of-charge unit tests (native_sim only).

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_battery_soc)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../src/power/battery_soc.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
