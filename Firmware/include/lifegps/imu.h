/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - LSM6DSV16X 6-axis IMU + sensor hub + SFLP.
 *
 * U12 on Main_V0.2 is an LSM6DSVTR (netlist `Hardware/Schematic/
 * Netlist_Main_V0.2_2026-11-11.tel`), whose die is the LSM6DSV16X. Hardware
 * facts below are from ST datasheet DS13510 Rev 4:
 *   - WHO_AM_I (0x0F) is fixed at 0x70 (section 9.13).
 *   - SPI: the address byte is "RW AD6..AD0" sent MSb first, so a read is
 *     0x80 | reg (section 5.1.3, figure 10; ST numbers the SPI bits in
 *     transmission order, which is why the prose calls RW "bit 0" although it is
 *     the MSB of the byte). SPI mode 0/3 (section 5.1); the chip drives SDO from
 *     bit 8, so a burst has no dummy byte.
 *   - The sensor hub (auxiliary I2C master) registers live in a second bank,
 *     reachable only while SHUB_REG_ACCESS is set in FUNC_CFG_ACCESS (0x01)
 *     (sections 16/17). Its output is SENSOR_HUB_1..18 (0x02..0x13) and each
 *     slave's numop field selects how many bytes it contributes.
 *   - The embedded functions bank (SFLP, pedometer, FSM, MLC) uses the same
 *     register 0x01 but bit 7, EMB_FUNC_REG_ACCESS (section 12).
 *
 * The two external sensors behind the hub are identified by the netlist:
 *   LIS2MDL  (U3) 7-bit address 0x1E, WHO_AM_I (0x4F) = 0x40
 *   BMP388   (U4) 7-bit address 0x76 (SDO tied to GND), CHIP_ID (0x00) = 0x50
 */

#ifndef LIFEGPS_IMU_H_
#define LIFEGPS_IMU_H_

#include <stdbool.h>
#include <stdint.h>

/* LSM6DSV16X WHO_AM_I (0x0F), DS13510 section 9.13. */
#define LGPS_LSM6DSV16X_WHO_AM_I 0x70U

/* LIS2MDL / BMP388 identification values (their own datasheets).
 *
 * LIS2MDL: WHO_AM_I lives at register address 0x4f and reads back 0x40
 * (DS12144 Rev 6 section 8.4 and the register map in table 20). The address and
 * the value are easy to confuse; comparing against 0x4f made every probe report
 * MISMATCH on a healthy sensor.
 */
#define LGPS_LIS2MDL_WHO_AM_I 0x40U
#define LGPS_BMP388_CHIP_ID 0x50U

/* 7-bit I2C addresses of the sensors on the auxiliary bus (netlist strapping). */
#define LGPS_LIS2MDL_I2C_ADDR 0x1EU
#define LGPS_BMP388_I2C_ADDR 0x76U
/* BMP388 with SDO tied to VDDIO (datasheet 5.2: address LSB follows SDO). The
 * netlist shows SDO on GND, but the probe tries both so a mis-populated or
 * reworked board still reports which strapping is present.
 */
#define LGPS_BMP388_ALT_I2C_ADDR 0x77U

/* Full-scale settings used by this firmware and their sensitivities.
 *
 * FS_XL[1:0] = 01b in CTRL8 (17h, table 68) gives +/-4 g with LA_So =
 * 0.122 mg/LSB; FS_G[3:0] = 0011b in CTRL6 (15h, table 63) gives +/-1000 dps
 * with G_So = 35 mdps/LSB (DS13510 section 4.2, table 2).
 */
#define LGPS_IMU_ACCEL_FS_G 4.0F       /* CTRL8 FS_XL[1:0] = 01b */
#define LGPS_IMU_ACCEL_MG_LSB 0.122F   /* datasheet LA_So, FS = +/-4 g */
#define LGPS_IMU_GYRO_FS_DPS 1000.0F   /* CTRL6 FS_G[3:0] = 0011b */
#define LGPS_IMU_GYRO_MDPS_LSB 35.0F   /* datasheet G_So, FS = +/-1000 dps */

/* LIS2MDL sensitivity, 1.5 mG/LSB (1 mG = 0.1 uT). Same value the telemetry
 * formatter uses; named here so the navigation code does not invent its own.
 */
#define LGPS_LIS2MDL_UT_PER_LSB 0.15F

/*
 * Die temperatures, both sensors. Both are die temperatures, not ambient.
 *
 * LSM6DSV16X OUT_TEMP (0x20/0x21): 16-bit two's complement, 256 LSB/degC with
 * 0 LSB at +25 degC (DS13510 section 4.3 table 6, TSen and note 3).
 *
 * LIS2MDL TEMP_OUT (0x6e/0x6f): 16-bit two's complement, 8 LSB/degC. The zero
 * level is +25 degC, i.e. T = raw / 8 + 25 (ST LIS2MDL driver and AN5069; the
 * datasheet section 8.16 only states the sensitivity). Bench cross-check
 * 2026-11-19: raw 13 -> 26.6 degC while the IMU die on the same board read
 * 26.0 degC, which rules out reading it as an absolute 1.6 degC.
 */
#define LGPS_IMU_TEMP_LSB_PER_C 256.0F
#define LGPS_IMU_TEMP_OFFSET_C 25.0F
#define LGPS_LIS2MDL_TEMP_LSB_PER_C 8.0F
#define LGPS_LIS2MDL_TEMP_OFFSET_C 25.0F

/*
 * ODR programmed for both chains: 30 Hz high-performance (CTRL1/CTRL2,
 * ODR_[XL|G] = 0100b, DS13510 tables 52 and 55).
 *
 * The LSM6DSR this replaces had 26 Hz and no 30 Hz, and the ZUPT window was 26
 * samples = 1.000 s. The LSM6DSV16X ODR set is 1.875 / 7.5 / 15 / 30 / 60 / 120
 * / 240 / 480 Hz and up: 30 Hz is the closest rate to the 20 Hz the PDR design
 * assumes, and 30 samples of it make the ZUPT window exactly 1.000 s again
 * (lifegps/nav.h). SFLP is programmed at the same 30 Hz so the quaternion rate
 * and the register burst rate cannot drift apart.
 */
#define LGPS_IMU_ODR_HZ 30.0F

struct lgps_imu_sample {
	int16_t raw_accel[3];
	int16_t raw_gyro[3];
	int16_t raw_temp; /* OUT_TEMP, 256 LSB/degC, +25 degC offset */
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
	int16_t raw_temp;    /* LIS2MDL TEMP_OUT, 8 LSB/degC, +25 degC offset */
	bool valid;
};

/*
 * BMP388 burst, raw plus the compensated physical values.
 *
 * The raw pair has no unit on its own: temperature_c and pressure_pa only exist
 * once the part's trimming coefficients have been applied (datasheet
 * BST-BMP388-DS001-07 sections 9.2 and 9.3, implemented in bmp388.c). The
 * coefficients are read once by lgps_imu_hub_configure(); when that read fails
 * the driver still streams the raw values but leaves "compensated" false, so
 * nobody publishes a number that was never converted.
 */
struct lgps_imu_baro {
	uint32_t raw_pressure;    /* BMP388 DATA_0..2 (0x04..0x06), 24 bit */
	uint32_t raw_temperature; /* BMP388 DATA_3..5 (0x07..0x09), 24 bit */
	float pressure_pa;        /* TPG compensated, Pa; valid when compensated */
	float temperature_c;      /* TPG compensated, degC; valid when compensated */
	bool compensated;
	bool valid;
};

/*
 * One SFLP sample out of the FIFO (DS13510 section 2.8; formats in
 * lifegps/sflp.h). The game rotation vector and the gravity vector are separate
 * FIFO entries with their own tags, so either half can arrive without the other:
 * check the two valid flags instead of assuming they travel together.
 *
 * The quaternion is in the SENSOR frame and has no magnetometer in it, so its
 * yaw drifts. That is why the BLE attitude packet keeps using the firmware
 * filter (which can be yaw-aligned to GNSS course) and only the gravity vector
 * is consumed for the gravity / linear-acceleration split.
 */
struct lgps_imu_sflp {
	float quat[4];        /* w, x, y, z - game rotation vector, sensor frame */
	bool quat_valid;
	int16_t raw_gravity[3]; /* FIFO tag 0x17, int16, 0.061 mg/LSB */
	float gravity_g[3];     /* same vector in g, sensor frame */
	bool gravity_valid;
};

/*
 * Reset and configure the IMU: BDU + IF_INC, +/-4 g, +/-1000 dps, 30 Hz
 * high-performance, data-ready on INT1, and - when CONFIG_LGPS_IMU_SFLP is set -
 * the SFLP game rotation vector + gravity vector at 30 Hz in the FIFO.
 * Returns 0 on success, -ENODEV if WHO_AM_I does not match (wrong part or dead
 * bus), negative errno otherwise.
 */
int lgps_imu_init(void);

/*
 * Read the two identification registers through the sensor hub. This proves the
 * auxiliary I2C bus before any streaming configuration is attempted.
 */
int lgps_imu_probe_hub(struct lgps_imu_hub_ids *ids);

/*
 * Bring-up diagnostics for the auxiliary I2C bus, called once between the hub
 * probe and the streaming configuration. It logs, in this order: a LIS2MDL
 * WHO_AM_I control read, the sensor hub configuration readback (proves
 * SHUB_REG_ACCESS and the register writes), the BMP388 CHIP_ID at both strapping
 * addresses, a six-byte block from register 0x00, BMP388 EVENT por_detected, a
 * soft reset plus an OSR write/readback pair, the two-slave layout with and
 * without IF_CFG.SHUB_PU_EN, and a numop mapping test. Every step also decodes
 * STATUS_MASTER_MAINPAGE (endop, wr_once_done, per-slave NACK). If the LIS2MDL
 * control returns 0x40 the bus and the hub are healthy, so anything U4 answers
 * with 0x00 is a hardware finding at its strapping address.
 * Blocking: about 4 s. Returns 0 unless the last hub access failed.
 */
int lgps_imu_hub_diag(void);

/*
 * Program LIS2MDL (50 Hz continuous, temperature compensation, BDU) and BMP388
 * (normal mode, pressure + temperature enabled) through one-shot hub writes, then
 * switch the hub into 3-slave read mode. The transfer rate is capped by the
 * accelerometer/gyro data-ready that triggers each cycle, i.e. 30 Hz here.
 *
 * This also reads the BMP388 trimming coefficients (0x31..0x45, three 7-byte hub
 * reads) before the part leaves sleep mode, which is what makes the compensated
 * pressure/temperature possible. A part that does not answer keeps
 * baro->compensated = false and only the raw values are reported.
 */
int lgps_imu_hub_configure(void);

/* WHO_AM_I value read at init (0x70 = LSM6DSV16X); 0 when the IMU was not
 * detected.
 */
uint8_t lgps_imu_who_am_i(void);

/* Read one acceleration/gyro pair unconditionally (diagnostics, self test). */
int lgps_imu_read_sample(struct lgps_imu_sample *out);

/*
 * Read a sample only when the chip reports new data (STATUS_REG XLDA | GDA).
 * Returns -EAGAIN when no new sample is available, 0 on success. Polling this at
 * a rate above the ODR gives the ZUPT chain exact 30 Hz pacing with no drift,
 * because the pacing comes from the sensor and not from a sleep period.
 */
int lgps_imu_read_sample_if_ready(struct lgps_imu_sample *out);

/*
 * Read the 14 bytes produced by the hub's three slaves: LIS2MDL magnetometer
 * (0x68, 6 bytes), LIS2MDL TEMP_OUT (0x6e, 2 bytes) and BMP388 pressure +
 * temperature (0x04..0x09, 6 bytes), and compensate the BMP388 pair with the
 * coefficients read at configure time. numop is a 3-bit field, so one slave can
 * transfer at most seven bytes - that is why the magnetometer temperature gets
 * its own slave instead of extending the first one. The BMP388 stays last because
 * it did not acknowledge at all until the 2026-11 rework, and the sensor hub does
 * not reliably fill the slots of slaves queued behind a device that NACKs. A
 * sensor that did not answer its identification read at boot keeps
 * "valid = false", so its zeros are never mistaken for a measurement.
 */
int lgps_imu_read_hub(struct lgps_imu_mag *mag, struct lgps_imu_baro *baro);

/*
 * Drain the FIFO and return the newest SFLP game rotation vector and gravity
 * vector it carried (DS13510 section 6.12, tags 0x13 and 0x17). Returns 0 when at
 * least one of the two was found, -EAGAIN when the FIFO was empty or carried only
 * entries this firmware ignores, negative errno on a bus error.
 *
 * Safe to call with CONFIG_LGPS_IMU_SFLP disabled: it reports -EAGAIN and the
 * caller keeps using its own attitude filter.
 */
int lgps_imu_read_sflp(struct lgps_imu_sflp *out);

#endif /* LIFEGPS_IMU_H_ */
