# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_calib)

target_sources(app PRIVATE
  src/main.c
  src/store_stub.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/calib/calib.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/util/crc.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
