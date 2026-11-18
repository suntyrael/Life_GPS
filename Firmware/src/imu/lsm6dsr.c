/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 *
 * LSM6DSR driver. All register addresses, bit positions and sensitivities are
 * quoted from ST datasheet DS11976 Rev 2 in the comments next to the value.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include <lifegps/imu.h>

LOG_MODULE_REGISTER(lgps_imu, LOG_LEVEL_INF);

/* --- LSM6DSR register map (datasheet table 20 / table 329) --------------- */
#define REG_FUNC_CFG_ACCESS 0x01U
#define REG_INT1_CTRL 0x0DU
#define REG_STATUS_REG 0x1EU
#define REG_WHO_AM_I 0x0FU
#define REG_CTRL1_XL 0x10U
#define REG_CTRL2_G 0x11U
#define REG_CTRL3_C 0x12U
#define REG_CTRL9_XL 0x18U
#define REG_OUTX_L_G 0x22U
#define REG_OUTX_L_A 0x28U

/* Sensor hub bank (visible while SHUB_REG_ACCESS is set). */
#define REG_SENSOR_HUB_1 0x02U
#define REG_MASTER_CONFIG 0x14U
#define REG_SLV0_ADD 0x15U
#define REG_SLV0_SUBADD 0x16U
#define REG_SLV0_CONFIG 0x17U
#define REG_SLV1_ADD 0x18U
#define REG_SLV1_SUBADD 0x19U
#define REG_SLV1_CONFIG 0x1AU
#define REG_DATAWRITE_SLV0 0x21U

/* Bit definitions. */
#define FUNC_CFG_SHUB_REG_ACCESS BIT(6)  /* FUNC_CFG_ACCESS (0x01), table 21 */
#define CTRL3_C_BDU BIT(6)              /* CTRL3_C (0x12), table 54 */
#define CTRL3_C_IF_INC BIT(2)
#define CTRL3_C_SW_RESET BIT(0)
#define MASTER_RST_REGS BIT(7)          /* MASTER_CONFIG (0x14), table 366 */
#define MASTER_WRITE_ONCE BIT(6)
#define MASTER_ON BIT(2)
#define SLV0_ADD_READ BIT(0)            /* SLV0_ADD (0x15): addr[6:0] << 1 | rw */

/* Control values picked for this firmware. */
#define CTRL1_XL_ODR_26HZ 0x20U  /* ODR_XL[3:0] = 0010b, table 50 (26 Hz) */
#define CTRL1_XL_FS_4G 0x08U     /* FS[1:0]_XL = 10b, table 49 */
#define CTRL2_G_ODR_26HZ 0x20U   /* ODR_G[3:0] = 0010b, table 53 (26 Hz) */
#define CTRL2_G_FS_1000DPS 0x08U /* FS[1:0]_G = 10b, table 52 */
#define CTRL9_XL_AXES_ON 0xE0U   /* datasheet reset value enables the three axes */
#define STATUS_XLDA BIT(0)       /* STATUS_REG (0x1E), table 81 */
#define STATUS_GDA BIT(1)
#define INT1_DRDY_XL BIT(0)      /* INT1_CTRL (0x0D), table 43 */
#define INT1_DRDY_G BIT(1)


/* External sensor registers (their own datasheets). */
#define LIS2MDL_CFG_REG_A 0x60U
#define LIS2MDL_CFG_REG_C 0x62U
#define LIS2MDL_OUTX_L_REG 0x68U
#define LIS2MDL_CFG_A_TEMP_COMP_50HZ 0x88U /* COMP_TEMP_EN | ODR=10b | MD=00b */
#define LIS2MDL_CFG_C_BDU 0x10U

#define BMP388_CHIP_ID_REG 0x00U
#define BMP388_PRESS_XLSB 0x04U
#define BMP388_OSR 0x1CU
#define BMP388_ODR 0x1DU
#define BMP388_PWR_CTRL 0x1BU
#define BMP388_OSR_P_X8_T_X2 0x0BU /* osr_t = 001b (bits 5:3), osr_p = 011b */
#define BMP388_ODR_25HZ 0x03U
#define BMP388_PWR_NORMAL 0x33U /* press_en | temp_en | mode = 11b (normal) */

/*
 * Sensor hub timing: SHUB_ODR stays at its 104 Hz default (table 373), but with
 * START_CONFIG = 0 the hub cycle is triggered by the accelerometer/gyro data-ready,
 * which now runs at 26 Hz. Two trigger periods are 77 ms, so 150 ms guarantees at
 * least two complete hub cycles before any register is sampled.
 */
#define SHUB_SETTLE_MS 150

#define SPI_OP (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

static const struct spi_dt_spec imu_spi = SPI_DT_SPEC_GET(DT_NODELABEL(imu), SPI_OP, 0);
static K_MUTEX_DEFINE(imu_lock);
static bool hub_streaming_configured;
static uint8_t detected_who_am_i;
static uint8_t bmp388_addr = LGPS_BMP388_I2C_ADDR;

static int imu_read_buf(uint8_t reg, uint8_t *buf, size_t len_bytes)
{
	uint8_t tx[1 + 18];
	uint8_t rx[1 + 18];
	struct spi_buf tx_buf = { .buf = tx, .len = 1U + (uint32_t)len_bytes };
	struct spi_buf rx_buf = { .buf = rx, .len = 1U + (uint32_t)len_bytes };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
	int ret;

	if (len_bytes == 0U || len_bytes > 18U) {
		return -EINVAL;
	}

	tx[0] = (uint8_t)(reg | 0x80U); /* READ bit is the MSB, datasheet 5.1.2.1 */
	memset(&tx[1], 0, len_bytes);

	ret = spi_transceive_dt(&imu_spi, &tx_set, &rx_set);
	if (ret < 0) {
		LOG_ERR("SPI read reg 0x%02x failed (%d)", reg, ret);
		return ret;
	}

	memcpy(buf, &rx[1], len_bytes);

	return 0;
}

static int imu_write(uint8_t reg, uint8_t value)
{
	uint8_t tx[2] = { (uint8_t)(reg & 0x7FU), value };
	const struct spi_buf_set tx_set = {
		.buffers = &(struct spi_buf) { .buf = tx, .len = sizeof(tx) },
		.count = 1,
	};
	int ret = spi_write_dt(&imu_spi, &tx_set);

	if (ret < 0) {
		LOG_ERR("SPI write reg 0x%02x failed (%d)", reg, ret);
	}

	return ret;
}

static int shub_bank_enable(bool enable)
{
	return imu_write(REG_FUNC_CFG_ACCESS, enable ? FUNC_CFG_SHUB_REG_ACCESS : 0x00U);
}

/*
 * One-shot byte write to an external sensor through the hub:
 * the hub performs the write on its first cycle, then the master is switched off
 * again so the configuration registers can be reprogrammed for reading.
 */
static int shub_write_byte(uint8_t i2c_addr, uint8_t reg, uint8_t value)
{
	int ret;

	ret = shub_bank_enable(true);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_MASTER_CONFIG, MASTER_RST_REGS);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_MASTER_CONFIG, 0x00U);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_SLV0_ADD, (uint8_t)(i2c_addr << 1)); /* rw = 0 -> write */
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_SLV0_SUBADD, reg);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_DATAWRITE_SLV0, value);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_SLV0_CONFIG, 0x00U); /* numop = 0 -> single write operation */
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_MASTER_CONFIG, MASTER_WRITE_ONCE | MASTER_ON);
	if (ret < 0) {
		return ret;
	}

	k_msleep(SHUB_SETTLE_MS);

	ret = imu_write(REG_MASTER_CONFIG, 0x00U);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_SLV0_CONFIG, 0x00U);
	if (ret < 0) {
		return ret;
	}

	return shub_bank_enable(false);
}

static int hub_read_bytes(uint8_t i2c0_addr, uint8_t i2c0_reg, uint8_t i2c1_addr,
			   uint8_t i2c1_reg, uint8_t *out, size_t len_bytes)
{
	int ret;

	ret = shub_bank_enable(true);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_MASTER_CONFIG, MASTER_RST_REGS);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_MASTER_CONFIG, 0x00U);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((i2c0_addr << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, i2c0_reg);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_CONFIG, 0x00U); /* numop = 0 -> one byte */
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_ADD, (uint8_t)((i2c1_addr << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_SUBADD, i2c1_reg);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_CONFIG, 0x00U);
	if (ret < 0) {
		goto out;
	}

	/* AUX_SENS_ON = 01b -> two slaves (table 367). */
	ret = imu_write(REG_MASTER_CONFIG, BIT(0) | MASTER_ON);
	if (ret < 0) {
		goto out;
	}

	k_msleep(SHUB_SETTLE_MS);

	ret = imu_read_buf(REG_SENSOR_HUB_1, out, len_bytes);

out:
	(void)shub_bank_enable(false);
	return ret;
}

int lgps_imu_probe_hub(struct lgps_imu_hub_ids *ids)
{
	uint8_t hub[2] = { 0U, 0U };
	int ret = 0;

	if (ids == NULL) {
		return -EINVAL;
	}

	memset(ids, 0, sizeof(*ids));

	k_mutex_lock(&imu_lock, K_FOREVER);

	/*
	 * The first hub cycle can be missed (trigger alignment), and BMP388 strapping
	 * is only known from the netlist, so both addresses are tried. Four attempts of
	 * 150 ms cover all of them.
	 */
	for (uint8_t attempt = 0U; attempt < 4U; attempt++) {
		ret = hub_read_bytes(LGPS_LIS2MDL_I2C_ADDR, 0x4FU, bmp388_addr,
				     (uint8_t)BMP388_CHIP_ID_REG, hub, sizeof(hub));
		if (ret < 0) {
			break;
		}

		ids->lis2mdl_who_am_i = hub[0];
		ids->bmp388_chip_id = hub[1];
		ids->lis2mdl_ok = (hub[0] == LGPS_LIS2MDL_WHO_AM_I);
		ids->bmp388_ok = (hub[1] == LGPS_BMP388_CHIP_ID);

		if (ids->lis2mdl_ok && ids->bmp388_ok) {
			break;
		}

		if (!ids->bmp388_ok && (bmp388_addr == LGPS_BMP388_I2C_ADDR)) {
			bmp388_addr = LGPS_BMP388_ALT_I2C_ADDR;
			continue;
		}

		if (!ids->bmp388_ok && (bmp388_addr == LGPS_BMP388_ALT_I2C_ADDR)) {
			bmp388_addr = LGPS_BMP388_I2C_ADDR;
		}
	}

	if (ret == 0) {
		LOG_INF("sensor hub probe: LIS2MDL WHO_AM_I=0x%02x (%s) BMP388 CHIP_ID=0x%02x @0x%02x "
			"(%s)", hub[0], ids->lis2mdl_ok ? "ok" : "MISMATCH", hub[1], bmp388_addr,
			ids->bmp388_ok ? "ok" : "MISMATCH");
		LOG_DBG("sensor hub raw: 0x%02x 0x%02x", hub[0], hub[1]);
	}

	k_mutex_unlock(&imu_lock);

	return ret;
}

int lgps_imu_hub_configure(void)
{
	int ret;

	k_mutex_lock(&imu_lock, K_FOREVER);

	/* LIS2MDL: temperature compensation, 50 Hz continuous, BDU. */
	ret = shub_write_byte(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_CFG_REG_A,
			      LIS2MDL_CFG_A_TEMP_COMP_50HZ);
	if (ret < 0) {
		goto out;
	}

	ret = shub_write_byte(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_CFG_REG_C, LIS2MDL_CFG_C_BDU);
	if (ret < 0) {
		goto out;
	}

	/* BMP388: oversampling, 25 Hz, normal mode with pressure + temperature. */
	ret = shub_write_byte(bmp388_addr, BMP388_OSR, BMP388_OSR_P_X8_T_X2);
	if (ret < 0) {
		goto out;
	}

	ret = shub_write_byte(bmp388_addr, BMP388_ODR, BMP388_ODR_25HZ);
	if (ret < 0) {
		goto out;
	}

	ret = shub_write_byte(bmp388_addr, BMP388_PWR_CTRL, BMP388_PWR_NORMAL);
	if (ret < 0) {
		goto out;
	}

	/* Read mode: 6 bytes from LIS2MDL + 6 bytes from BMP388. */
	ret = shub_bank_enable(true);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_MASTER_CONFIG, MASTER_RST_REGS);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_MASTER_CONFIG, 0x00U);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((LGPS_LIS2MDL_I2C_ADDR << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, (uint8_t)LIS2MDL_OUTX_L_REG);
	if (ret < 0) {
		goto out;
	}

	/* SHUB_ODR = 01b (52 Hz) | numop = 5 (6 bytes). */
	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)(BIT(6) | 0x05U));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_ADD, (uint8_t)((bmp388_addr << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_SUBADD, (uint8_t)BMP388_PRESS_XLSB);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_CONFIG, 0x05U); /* numop = 5 (6 bytes) */
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_MASTER_CONFIG, BIT(0) | MASTER_ON); /* two sensors, master on */
	if (ret < 0) {
		goto out;
	}

	hub_streaming_configured = true;

	k_msleep(SHUB_SETTLE_MS);

	uint8_t check[12] = { 0 };

	if (imu_read_buf(REG_SENSOR_HUB_1, check, sizeof(check)) == 0) {
		LOG_INF("sensor hub streaming: LIS2MDL 6B @50Hz, BMP388 6B @25Hz @0x%02x, "
			"hub ODR 52Hz, 12 of 18 bytes used", bmp388_addr);
		LOG_INF("sensor hub first burst: %02x %02x %02x %02x %02x %02x | "
			"%02x %02x %02x %02x %02x %02x", check[0], check[1], check[2], check[3],
			check[4], check[5], check[6], check[7], check[8], check[9], check[10],
			check[11]);

		if ((check[6] == 0U) && (check[7] == 0U) && (check[8] == 0U)) {
			LOG_WRN("BMP388 pressure reads 0: check PWR_CTRL write path, SDO strapping "
				"(0x76/0x77) and that U4 is populated");
		}
	}

out:
	(void)shub_bank_enable(false);
	k_mutex_unlock(&imu_lock);
	return ret;
}

static void decode_sample(const uint8_t raw[12], struct lgps_imu_sample *out)
{
	/* Gyro first (0x22..0x27), then accelerometer (0x28..0x2D), little endian. */
	for (uint8_t axis = 0U; axis < 3U; axis++) {
		out->raw_gyro[axis] = (int16_t)((uint16_t)raw[2U * axis] |
						((uint16_t)raw[2U * axis + 1U] << 8));
		out->raw_accel[axis] = (int16_t)((uint16_t)raw[6U + 2U * axis] |
						 ((uint16_t)raw[6U + 2U * axis + 1U] << 8));
		out->gyro_dps[axis] = (float)out->raw_gyro[axis] * LGPS_IMU_GYRO_MDPS_LSB / 1000.0F;
		out->accel_g[axis] = (float)out->raw_accel[axis] * LGPS_IMU_ACCEL_MG_LSB / 1000.0F;
	}
}

uint8_t lgps_imu_who_am_i(void)
{
	return detected_who_am_i;
}

int lgps_imu_read_sample(struct lgps_imu_sample *out)
{
	uint8_t raw[12];
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&imu_lock, K_FOREVER);
	ret = imu_read_buf(REG_OUTX_L_G, raw, sizeof(raw));
	k_mutex_unlock(&imu_lock);

	if (ret < 0) {
		return ret;
	}

	decode_sample(raw, out);

	return 0;
}

int lgps_imu_read_sample_if_ready(struct lgps_imu_sample *out)
{
	uint8_t status = 0U;
	uint8_t raw[12];
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&imu_lock, K_FOREVER);

	ret = imu_read_buf(REG_STATUS_REG, &status, 1U);
	if (ret < 0) {
		k_mutex_unlock(&imu_lock);
		return ret;
	}

	if ((status & (STATUS_XLDA | STATUS_GDA)) != (STATUS_XLDA | STATUS_GDA)) {
		/* The pair is only complete when both chains have fresh data. */
		k_mutex_unlock(&imu_lock);
		return -EAGAIN;
	}

	ret = imu_read_buf(REG_OUTX_L_G, raw, sizeof(raw));
	k_mutex_unlock(&imu_lock);

	if (ret < 0) {
		return ret;
	}

	decode_sample(raw, out);

	return 0;
}

int lgps_imu_read_hub(struct lgps_imu_mag *mag, struct lgps_imu_baro *baro)
{
	uint8_t raw[12];
	int ret;

	if (mag == NULL || baro == NULL) {
		return -EINVAL;
	}

	mag->valid = false;
	baro->valid = false;

	k_mutex_lock(&imu_lock, K_FOREVER);

	if (!hub_streaming_configured) {
		k_mutex_unlock(&imu_lock);
		return -EAGAIN;
	}

	ret = shub_bank_enable(true);
	if (ret < 0) {
		(void)shub_bank_enable(false);
		k_mutex_unlock(&imu_lock);
		return ret;
	}

	ret = imu_read_buf(REG_SENSOR_HUB_1, raw, sizeof(raw));
	(void)shub_bank_enable(false);
	k_mutex_unlock(&imu_lock);

	if (ret < 0) {
		return ret;
	}

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		mag->raw[axis] = (int16_t)((uint16_t)raw[2U * axis] |
					   ((uint16_t)raw[2U * axis + 1U] << 8));
	}

	/* BMP388 pressure and temperature are unsigned 24-bit values. */
	baro->raw_pressure = (uint32_t)raw[6] | ((uint32_t)raw[7] << 8) | ((uint32_t)raw[8] << 16);
	baro->raw_temperature = (uint32_t)raw[9] | ((uint32_t)raw[10] << 8) |
				((uint32_t)raw[11] << 16);

	mag->valid = true;
	baro->valid = true;

	return 0;
}

int lgps_imu_init(void)
{
	uint8_t who_am_i = 0U;
	int ret;

	if (!spi_is_ready_dt(&imu_spi)) {
		LOG_ERR("SPIM22 not ready");
		return -ENODEV;
	}

	k_mutex_lock(&imu_lock, K_FOREVER);

	ret = imu_read_buf(REG_WHO_AM_I, &who_am_i, 1U);
	if (ret < 0) {
		goto out;
	}

	if (who_am_i != LGPS_LSM6DSR_WHO_AM_I) {
		LOG_ERR("WHO_AM_I=0x%02x, expected 0x%02x (LSM6DSR)", who_am_i,
			LGPS_LSM6DSR_WHO_AM_I);
		ret = -ENODEV;
		goto out;
	}

	detected_who_am_i = who_am_i;
	LOG_INF("LSM6DSR detected on SPIM22 (WHO_AM_I=0x%02x, CS=P1.14, 8 MHz)", who_am_i);

	/* Software reset, then BDU + IF_INC. */
	ret = imu_write(REG_CTRL3_C, CTRL3_C_SW_RESET);
	if (ret < 0) {
		goto out;
	}

	k_msleep(10);

	ret = imu_write(REG_CTRL3_C, CTRL3_C_BDU | CTRL3_C_IF_INC);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_CTRL9_XL, CTRL9_XL_AXES_ON);
	if (ret < 0) {
		goto out;
	}

	/* +/-4 g at 26 Hz high performance, +/-1000 dps at 26 Hz high performance. */
	ret = imu_write(REG_CTRL1_XL, CTRL1_XL_ODR_26HZ | CTRL1_XL_FS_4G);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_CTRL2_G, CTRL2_G_ODR_26HZ | CTRL2_G_FS_1000DPS);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_INT1_CTRL, INT1_DRDY_XL | INT1_DRDY_G);
	if (ret < 0) {
		goto out;
	}

	LOG_INF("LSM6DSR configured: +/-%.0f g (%.3f mg/LSB), +/-%.0f dps (%.0f mdps/LSB), "
		"ODR %.0f Hz, INT1=DRDY_XL|DRDY_G",
		(double)LGPS_IMU_ACCEL_FS_G, (double)LGPS_IMU_ACCEL_MG_LSB,
		(double)LGPS_IMU_GYRO_FS_DPS, (double)LGPS_IMU_GYRO_MDPS_LSB,
		(double)LGPS_IMU_ODR_HZ);

out:
	k_mutex_unlock(&imu_lock);
	return ret;
}
