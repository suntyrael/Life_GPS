# Copyright (c) 2026 Life_GPS contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

# The board definition ships with the application (boards/lifegps/lifegps).
# Appending it here makes the single-image build work without -DBOARD_ROOT
# (west build -b lifegps/nrf54l15/cpuapp .). It does NOT cover sysbuild: each child
# image (e.g. MCUboot) resolves the board in its own CMake project, so the OTA build
# has to pass -DBOARD_ROOT=<this directory> on the command line.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Application level devicetree bindings (dts/bindings) are picked up because the
# application directory is part of DTS_ROOT.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_firmware)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_sources(app PRIVATE
  src/main.c
  src/ble/ble_frame.c
  src/ble/ble_gatt.c
  src/ble/ble_log.c
  src/board/board_io.c
  src/diag/diag.c
  src/diag/telemetry.c
  src/gnss/gnss.c
  src/gnss/ubx.c
  src/imu/lsm6dsr.c
  src/imu/bmp388.c
  src/nav/attitude.c
  src/nav/baro.c
  src/nav/baro_store.c
  src/nav/nav.c
  src/power/battery.c
  src/power/keys.c
  src/power/led.c
  src/power/power_sm.c
  src/power/wdt.c
  src/storage/flash_ring.c
  src/storage/traj.c
  src/util/crc.c
  src/util/time.c
)

# Calibration: pure maths + session state (src/calib/calib.c, unit tested on
# native_sim) and the NOR record backend (src/calib/calib_store.c).
if(CONFIG_LGPS_CALIB)
  target_sources(app PRIVATE
    src/calib/calib.c
    src/calib/calib_store.c
  )
endif()

# Keep the generated protocol header in the dependency graph so a protocol change
# forces a rebuild instead of silently linking stale offsets.
set_property(SOURCE src/storage/traj.c src/ble/ble_frame.c APPEND PROPERTY
  OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/generated/lifegps_protocol.h)
