# Life_GPS - trajectory record codec unit tests (native_sim only).

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lifegps_traj)

target_sources(app PRIVATE
	src/main.c
	../../src/storage/traj.c
	../../src/util/crc.c
)

target_include_directories(app PRIVATE
	../../include
)
