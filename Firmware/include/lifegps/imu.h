/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - LSM6DSRTR 6-axis IMU + sensor hub.
 *
 * Hardware facts (ST datasheet DS11976 Rev 2, "LSM6DSR"):
 *   - WHO_AM_I (0x0F) is fixed at 0x6B (section 9.14).
 *   - SPI read: first bit of the address byte is the READ bit, so a read is
 *     0x80 | reg, SPI mode 3 (section 5.1.2.1).
 *   - Sensor hub registers live in a second register bank, reachable only while
 *     SHUB_REG_ACCESS is set in FUNC_CFG_ACCESS (0x01) (section 14).
 *   - Sensor hub output is SENSOR_HUB_1..18 (0x02..0x13); each slave's
 *     SlaveN_numop field selects how many bytes it contributes (section 14/15).
 *
 * The two external sensors behind the hub are identified by the netlist:
 *   LIS2MDL  (U3) 7-bit address 0x1E, WHO_AM_I (0x4F) = 0x4F
 *   BMP388   (U4) 7-bit address 0x76 (SDO tied to GND), CHIP_ID (0x00) = 0x50
 */

#ifndef LIFEGPS_IMU_H_
#define LIFEGPS_IMU_H_

#include <stdbool.h>
#include <stdint.h>

/* LSM6DSR WHO_AM_I (0x0F), datasheet section 9.14. */
#define LGPS_LSM6DSR_WHO_AM_I 0x6BU

/* LIS2MDL / BMP388 identification values (their own datasheets). */
#define LGPS_LIS2MDL_WHO_AM_I 0x4FU
#define LGPS_BMP388_CHIP_ID 0x50U

/* 7-bit I2C addresses of the sensors on the auxiliary bus (netlist strapping). */
#define LGPS_LIS2MDL_I2C_ADDR 0x1EU
#define LGPS_BMP388_I2C_ADDR 0x76U

/* Full-scale settings used by this firmware and their sensitivities. */
#define LGPS_IMU_ACCEL_FS_G 4.0F       /* CTRL1_XL FS[1:0]_XL = 10b */
#define LGPS_IMU_ACCEL_MG_LSB 0.122F   /* datasheet LA_So, FS = +/-4 g */
#define LGPS_IMU_GYRO_FS_DPS 1000.0F   /* CTRL2_G FS[1:0]_G = 10b */
#define LGPS_IMU_GYRO_MDPS_LSB 35.0F   /* datasheet G_So, FS = +/-1000 dps */

/* ODR actually programmed: 104 Hz high-performance for both chains. */
#define LGPS_IMU_ODR_HZ 104.0F

struct lgps_imu_sample {
	int16_t raw_accel[3];
	int16_t raw_gyro[3];
	float accel_g[3];
	float gyro_dps[3];
};

struct lgps_imu_hub_ids {
	uint8_t lis2mdl_who_am_i;
	uint8_t bmp388_chip_id;
	bool lis2mdl_ok;
	bool bmp388_ok;
};

struct lgps_imu_mag {
	int16_t raw[3];      /* LIS2MDL OUTX..OUTZ, LSB */
	bool valid;
};

struct lgps_imu_baro {
	uint32_t raw_pressure;    /* BMP388 press_xlsb..press_msb, 24 bit */
	uint32_t raw_temperature; /* BMP388 temp_xlsb..temp_msb, 24 bit */
	bool valid;
};

/*
 * Reset and configure the IMU: BDU + IF_INC, +/-4 g, +/-1000 dps, 104 Hz
 * high-performance, data-ready on INT1. Returns 0 on success, -ENODEV if
 * WHO_AM_I does not match (wrong part or dead bus), negative errno otherwise.
 */
int lgps_imu_init(void);

/*
 * Read the two identification registers through the sensor hub. This proves the
 * auxiliary I2C bus before any streaming configuration is attempted.
 */
int lgps_imu_probe_hub(struct lgps_imu_hub_ids *ids);

/*
 * Program LIS2MDL (50 Hz continuous, temperature compensation, BDU) and BMP388
 * (normal mode, pressure + temperature enabled) through one-shot hub writes, then
 * switch the hub into 2-slave read mode at 52 Hz.
 */
int lgps_imu_hub_configure(void);

/* Read one acceleration/gyro pair (also used by the 20 Hz PDR loop). */
int lgps_imu_read_sample(struct lgps_imu_sample *out);

/*
 * Read the 12 bytes produced by the hub (6 magnetometer + 6 barometer).
 * Both values stay "valid = false" until lgps_imu_hub_configure() succeeded.
 */
int lgps_imu_read_hub(struct lgps_imu_mag *mag, struct lgps_imu_baro *baro);

#endif /* LIFEGPS_IMU_H_ */
