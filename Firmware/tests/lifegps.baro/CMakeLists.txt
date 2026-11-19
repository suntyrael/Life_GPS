# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_baro)

# baro_store.c is deliberately not linked: the test supplies an in-memory stub of
# the two NOR functions, which is what makes the baseline state machine testable
# without a flash driver (same approach as tests/lifegps.calib).
target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/nav/baro.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
