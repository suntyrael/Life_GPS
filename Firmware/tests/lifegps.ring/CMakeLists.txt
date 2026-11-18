# SPDX-License-Identifier: MIT
# Life_GPS - staging-ring sector format unit tests (native_sim only).
#
# Needs no flash driver: the module under test is driver free on purpose
# (lifegps/sector_format.h), so a 4 KiB "sector image" is just a byte array.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_ring)

target_sources(app PRIVATE
	src/main.c
	../../src/util/crc.c
)

target_include_directories(app PRIVATE
	../../include
)
