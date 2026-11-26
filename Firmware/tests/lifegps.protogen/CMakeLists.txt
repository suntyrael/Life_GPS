# SPDX-License-Identifier: MIT
# Life_GPS - generated protocol header contract tests (native_sim only).

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_protogen)

target_sources(app PRIVATE
	src/main.c
	../../src/ble/ble_frame.c
	../../src/storage/archive_codec.c
	../../src/storage/traj.c
	../../src/util/crc.c
)

target_include_directories(app PRIVATE
	../../include
)
