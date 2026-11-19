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
#define MASTER_SHUB_PU_EN BIT(3)        /* internal pull-up on the auxiliary bus */
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

/* LIS2MDL registers used by the numop mapping test (DS12144 Rev 6 section 8).
 * The interrupt is never enabled by this firmware, so its threshold registers
 * are inert and safe to use as a write pattern.
 */
#define LIS2MDL_INT_CTRL_REG 0x63U
#define LIS2MDL_INT_THS_L_REG 0x65U
#define LIS2MDL_INT_THS_H_REG 0x66U
#define LIS2MDL_INT_THS_L_PATTERN 0xA5U
#define LIS2MDL_INT_THS_H_PATTERN 0x5AU
#define LIS2MDL_WHO_AM_I_REG 0x4FU

#define BMP388_CHIP_ID_REG 0x00U
#define BMP388_PRESS_XLSB 0x04U
#define BMP388_OSR 0x1CU
#define BMP388_ODR 0x1DU
#define BMP388_PWR_CTRL 0x1BU
#define BMP388_OSR_P_X8_T_X2 0x0BU /* osr_t = 001b (bits 5:3), osr_p = 011b */
#define BMP388_ODR_25HZ 0x03U
#define BMP388_PWR_NORMAL 0x33U /* press_en | temp_en | mode = 11b (normal) */

/* BMP388 registers used only by the hub diagnostics, see
 * lifegps_imu_hub_diag(). Table numbers refer to BST-BMP388-DS001-07.
 */
#define BMP388_EVENT_REG 0x10U           /* Table 31 */
#define BMP388_CMD_REG 0x7EU             /* Table 45 */
#define BMP388_CMD_SOFTRESET 0xB6U       /* Table 46 */
#define BMP388_IF_CONF 0x1AU             /* Table 39 */
/* i2c_wdt_en = 1 with the long timeout: the NVM default is enabled with the
 * 1.25 ms short timeout, which a six-byte burst at the sensor hub's MSCL rate
 * may not fit into.
 */
#define BMP388_IF_CONF_WDT_LONG 0x06U
#define BMP388_EVENT_POR_DETECTED BIT(0) /* por_detected, clear-on-read */
/* osr_t = 101b (x32), osr_p = 011b (x8): distinguishable from both the reset
 * value (0x02) and the value this firmware programs (0x0B).
 */
#define BMP388_OSR_DIAG_PATTERN 0x2BU

/* Time allowed for one hub cycle plus the aux I2C transaction. The hub is
 * triggered by the accelerometer/gyro data-ready, which runs at 26 Hz, so two
 * trigger periods are 77 ms.
 */
#define HUB_DIAG_READ_MS 150

/* STATUS_MASTER_MAINPAGE (0x39) lives on the main page, so it can be polled
 * without switching to the sensor hub bank (DS11976 section 9.40, table 112).
 */
#define REG_STATUS_MASTER_MAINPAGE 0x39U
#define SHUB_WR_ONCE_DONE BIT(7)
#define SHUB_SLAVE1_NACK BIT(4)
#define SHUB_SLAVE0_NACK BIT(3)
#define SHUB_SENS_HUB_ENDOP BIT(0)

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

/*
 * Last sensor hub transaction status. It has to be sampled while the master is
 * still enabled: clearing MASTER_ON (which every helper here does on the way
 * out) resets endop and the NACK bits, so a later read returns zeros. Written
 * and read under imu_lock, like every other access to the auxiliary bus.
 */
static uint8_t last_hub_status;

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

static void hub_status_capture(void)
{
	int ret = imu_read_buf(REG_STATUS_MASTER_MAINPAGE, &last_hub_status, 1U);

	if (ret < 0) {
		LOG_ERR("sensor hub status read failed (%d)", ret);
	}
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

	/* Sample endop/wr_once_done/NACK before the master is switched off. */
	hub_status_capture();

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

/* --- Bring-up diagnostics for the auxiliary bus ------------------------- */

/*
 * One-shot read of a single external sensor into SENSOR_HUB_1..N. numop is the
 * number of read operations the hub performs; the 2026-11-19 bench log showed
 * that 0 performs none at all, so callers ask for at least one and read the
 * bytes they need. Only one slave is configured, which keeps the first byte at
 * SENSOR_HUB_1 whatever the numop-to-bytes mapping turns out to be.
 */
static int hub_single_read(uint8_t i2c_addr, uint8_t sub_reg, uint8_t numop,
			   uint8_t *out, size_t len_bytes)
{
	int ret;

	if (out == NULL || len_bytes == 0U || len_bytes > 18U) {
		return -EINVAL;
	}

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

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((i2c_addr << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, sub_reg);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_CONFIG, numop);
	if (ret < 0) {
		goto out;
	}

	/* AUX_SENS_ON = 00b -> one slave (table 367). */
	ret = imu_write(REG_MASTER_CONFIG, MASTER_ON);
	if (ret < 0) {
		goto out;
	}

	k_msleep(HUB_DIAG_READ_MS);

	ret = imu_read_buf(REG_SENSOR_HUB_1, out, len_bytes);
	if (ret == 0) {
		hub_status_capture();
	}

out:
	(void)imu_write(REG_MASTER_CONFIG, 0x00U);
	(void)shub_bank_enable(false);
	return ret;
}

/*
 * Read the sensor hub configuration block back (bank 1, 0x14..0x1A). Seeing the
 * values that were just written proves that SHUB_REG_ACCESS really engaged and
 * that the writes landed, which is the first thing to rule out when an external
 * sensor appears dead.
 */
static int hub_dump_config(void)
{
	uint8_t regs[7] = { 0U };
	int ret;

	ret = shub_bank_enable(true);
	if (ret < 0) {
		return ret;
	}

	ret = imu_read_buf(REG_MASTER_CONFIG, regs, sizeof(regs));
	(void)shub_bank_enable(false);

	if (ret < 0) {
		LOG_ERR("hub diag 2: config readback failed (%d)", ret);
		return ret;
	}

	LOG_INF("hub diag 2: bank1 readback MASTER=0x%02x SLV0_ADD=0x%02x SLV0_SUB=0x%02x "
		"SLV0_CFG=0x%02x SLV1_ADD=0x%02x SLV1_SUB=0x%02x SLV1_CFG=0x%02x",
		regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);

	return 0;
}

/*
 * One-byte read used by the diagnostics; logs the value and returns it in
 * *value. numop is 1, not 0: a zero numop makes the sensor hub perform no read
 * at all (the 2026-11-19 bench log returned zeros for every numop = 0 read),
 * so a one-byte read must ask for at least one operation.
 */
static int hub_diag_read1(uint8_t i2c_addr, uint8_t sub_reg, uint8_t *value,
			  const char *what)
{
	int ret;

	ret = hub_single_read(i2c_addr, sub_reg, 1U, value, 1U);
	if (ret < 0) {
		LOG_ERR("hub diag: %s (dev 0x%02x reg 0x%02x) read failed (%d)", what,
			i2c_addr, sub_reg, ret);
		return ret;
	}

	LOG_INF("hub diag: %s (dev 0x%02x reg 0x%02x) = 0x%02x", what, i2c_addr, sub_reg,
		*value);

	return 0;
}

/*
 * Decode STATUS_MASTER_MAINPAGE as captured by hub_status_capture() while the
 * master was still enabled. endop tells whether a hub cycle concluded at all,
 * and the NACK bits tell whether each slave acknowledged its address: together
 * they separate "device not there" from "device there but silent" from "the
 * hub never ran". Reading 0x39 here would return zeros, because every helper
 * clears MASTER_ON before returning.
 */
static void hub_status_log(const char *tag)
{
	LOG_INF("hub diag: %s 0x39=0x%02x endop=%u wr_once_done=%u nack(s0=%u s1=%u)", tag,
		last_hub_status,
		(unsigned int)((last_hub_status & SHUB_SENS_HUB_ENDOP) ? 1U : 0U),
		(unsigned int)((last_hub_status & SHUB_WR_ONCE_DONE) ? 1U : 0U),
		(unsigned int)((last_hub_status & SHUB_SLAVE0_NACK) ? 1U : 0U),
		(unsigned int)((last_hub_status & SHUB_SLAVE1_NACK) ? 1U : 0U));
}

/*
 * Two-slave reproduction of the streaming layout (AUX_SENS_ON = 01b, table 367)
 * with the identification registers as payload: Sensor 1 = LIS2MDL WHO_AM_I,
 * Sensor 2 = BMP388 CHIP_ID. A healthy run logs 0x4f first and 0x50 seventh.
 * pull_up sets SHUB_PU_EN, so running it twice gives an A/B answer on the
 * auxiliary bus electrical side.
 */
static int hub_diag_two_slave(uint8_t bmp_addr, bool pull_up)
{
	uint8_t block[12] = { 0U };
	uint8_t master = (uint8_t)(BIT(0) | MASTER_ON);
	int ret;

	if (pull_up) {
		master = (uint8_t)(master | MASTER_SHUB_PU_EN);
	}

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

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((LGPS_LIS2MDL_I2C_ADDR << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, 0x4FU);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)(BIT(6) | 0x05U));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_ADD, (uint8_t)((bmp_addr << 1) | SLV0_ADD_READ));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_SUBADD, (uint8_t)BMP388_CHIP_ID_REG);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_CONFIG, 0x05U);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_MASTER_CONFIG, master);
	if (ret < 0) {
		goto out;
	}

	k_msleep(HUB_DIAG_READ_MS);

	ret = imu_read_buf(REG_SENSOR_HUB_1, block, sizeof(block));
	if (ret == 0) {
		hub_status_capture();
	}

out:
	(void)imu_write(REG_MASTER_CONFIG, 0x00U);
	(void)shub_bank_enable(false);

	if (ret < 0) {
		return ret;
	}

	LOG_INF("hub diag 8%s: mag half = %02x %02x %02x %02x %02x %02x (byte 0 must be "
		"0x%02x)", pull_up ? "b" : "a", block[0], block[1], block[2], block[3],
		block[4], block[5], LGPS_LIS2MDL_WHO_AM_I);
	LOG_INF("hub diag 8%s: baro half = %02x %02x %02x %02x %02x %02x (byte 0 must be "
		"0x%02x)", pull_up ? "b" : "a", block[6], block[7], block[8], block[9],
		block[10], block[11], LGPS_BMP388_CHIP_ID);

	return 0;
}

int lgps_imu_hub_diag(void)
{
	uint8_t value = 0U;
	uint8_t block[6] = { 0U };
	uint8_t bmp_addr = bmp388_addr;
	int ret = 0;

	k_mutex_lock(&imu_lock, K_FOREVER);

	/* 1. Control. LIS2MDL sits on the same auxiliary bus and is known to be
	 *    populated, so it answers the question "is the hub or the sensor
	 *    broken": address 0x1e, register 0x4f, value 0x40.
	 */
	ret = hub_single_read(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_WHO_AM_I_REG, 1U, block, 1U);
	if (ret < 0) {
		LOG_ERR("hub diag 1: LIS2MDL one-shot read failed (%d)", ret);
	} else {
		LOG_INF("hub diag 1: LIS2MDL WHO_AM_I @0x%02x[0x4f] = 0x%02x (expect 0x%02x) %s",
			LGPS_LIS2MDL_I2C_ADDR, block[0], LGPS_LIS2MDL_WHO_AM_I,
			(block[0] == LGPS_LIS2MDL_WHO_AM_I) ? "OK" : "MISMATCH");
	}

	/* 2. Prove SHUB_REG_ACCESS and the configuration writes work at all: the
	 *    readback still shows the single-slave setup written in step 1.
	 */
	(void)hub_dump_config();
	hub_status_log("1: LIS2MDL one-shot");

	/* 3. BMP388 identity at both strapping addresses, one byte each. */
	for (uint8_t idx = 0U; idx < 2U; idx++) {
		uint8_t addr = (idx == 0U) ? LGPS_BMP388_I2C_ADDR : LGPS_BMP388_ALT_I2C_ADDR;

		ret = hub_single_read(addr, BMP388_CHIP_ID_REG, 1U, &value, 1U);
		if (ret < 0) {
			LOG_ERR("hub diag 3: CHIP_ID read @0x%02x failed (%d)", addr, ret);
			continue;
		}

		LOG_INF("hub diag 3: BMP388 CHIP_ID @0x%02x[0x00] = 0x%02x (expect 0x%02x) %s",
			addr, value, LGPS_BMP388_CHIP_ID,
			(value == LGPS_BMP388_CHIP_ID) ? "OK" : "NO RESPONSE");
		hub_status_log("3: BMP388 CHIP_ID");

		if (value == LGPS_BMP388_CHIP_ID) {
			bmp_addr = addr;
		}
	}

	/* 4. Same sensor as the only slave with numop = 5 (six bytes). This is the
	 *    shape used by the streaming configuration, so a pass here and a fail
	 *    in step 3 would point at the numop encoding rather than at U4.
	 *    Byte order is CHIP_ID, ERR_REG, STATUS, DATA_0..2 (tables 25-28).
	 */
	ret = hub_single_read(LGPS_BMP388_I2C_ADDR, BMP388_CHIP_ID_REG, 5U, block,
			      sizeof(block));
	if (ret < 0) {
		LOG_ERR("hub diag 4: 6-byte block read failed (%d)", ret);
	} else {
		LOG_INF("hub diag 4: 0x76 regs 0x00..0x05 = %02x %02x %02x %02x %02x %02x",
			block[0], block[1], block[2], block[3], block[4], block[5]);
	}
	hub_status_log("4: BMP388 block");

	/* 4b. The BMP388 I2C watchdog is enabled from NVM with the 1.25 ms short
	 *     timeout (IF_CONF 0x1A, table 39) and this firmware never writes that
	 *     register. A six-byte burst is roughly 80 SCL periods; at the sensor
	 *     hub's MSCL rate that can exceed 1.25 ms, in which case the BMP388
	 *     aborts the transfer while the shorter reads still succeed. Repeat the
	 *     same six-byte read with the 40 ms timeout to separate that from a part
	 *     that is not there at all.
	 */
	ret = shub_write_byte(bmp_addr, BMP388_IF_CONF, BMP388_IF_CONF_WDT_LONG);
	if (ret < 0) {
		LOG_ERR("hub diag 4b: IF_CONF write failed (%d)", ret);
	} else {
		ret = hub_single_read(bmp_addr, BMP388_CHIP_ID_REG, 5U, block, sizeof(block));
		if (ret < 0) {
			LOG_ERR("hub diag 4b: IF_CONF retry read failed (%d)", ret);
		} else {
			LOG_INF("hub diag 4b: after IF_CONF wdt_long = %02x %02x %02x %02x "
				"%02x %02x (0x50 in byte 0 = watchdog was aborting the burst)",
				block[0], block[1], block[2], block[3], block[4], block[5]);
		}
		hub_status_log("4b: IF_CONF retry");
	}

	/* 5. por_detected (EVENT, table 31) is set after every power-on and soft
	 *    reset and cleared by a read, so it tells a live die from a silent bus.
	 */
	ret = hub_diag_read1(bmp_addr, BMP388_EVENT_REG, &value, "EVENT");
	if (ret == 0) {
		LOG_INF("hub diag 5: por_detected=%u (1 = die alive since last read)",
			(unsigned int)((value & BMP388_EVENT_POR_DETECTED) ? 1U : 0U));
	}

	/* 6. Write path. A soft reset (table 46) must re-raise por_detected, which
	 *    proves the write direction of the hub works even though reads return
	 *    all zero.
	 */
	ret = shub_write_byte(bmp_addr, BMP388_CMD_REG, BMP388_CMD_SOFTRESET);
	if (ret < 0) {
		LOG_ERR("hub diag 6: soft reset write failed (%d)", ret);
	} else {
		k_msleep(5);
		ret = hub_diag_read1(bmp_addr, BMP388_EVENT_REG, &value, "EVENT after reset");
		if (ret == 0) {
			LOG_INF("hub diag 6: soft reset %s",
				((value & BMP388_EVENT_POR_DETECTED) != 0U) ? "ACKED" :
									   "DID NOT LAND");
		}
	}
	hub_status_log("6: soft reset write");

	/* 7. Write/readback of a register with a distinctive value: the definitive
	 *    test that OSR (and therefore PWR_CTRL before it) can be programmed.
	 */
	ret = shub_write_byte(bmp_addr, BMP388_OSR, BMP388_OSR_DIAG_PATTERN);
	if (ret < 0) {
		LOG_ERR("hub diag 7: OSR write failed (%d)", ret);
	} else {
		ret = hub_single_read(bmp_addr, BMP388_OSR, 1U, &value, 1U);
		if (ret < 0) {
			LOG_ERR("hub diag 7: OSR readback failed (%d)", ret);
		} else {
			LOG_INF("hub diag 7: OSR wrote 0x%02x read 0x%02x %s",
				BMP388_OSR_DIAG_PATTERN, value,
				(value == BMP388_OSR_DIAG_PATTERN) ? "OK" : "WRITE LOST");
		}
	}

	/* hub_configure() reprograms OSR/ODR/PWR_CTRL, but leave the reset period
	 * the firmware intends rather than the diagnostic pattern.
	 */
	(void)shub_write_byte(bmp_addr, BMP388_OSR, BMP388_OSR_P_X8_T_X2);

	/* 8. Exercise the two-slave layout twice: without and with the internal
	 *    auxiliary pull-ups (MASTER_CONFIG bit 3). A NACK that disappears in
	 *    8b points at the external pull-up resistors R14/R15, not at U4.
	 */
	ret = hub_diag_two_slave(bmp_addr, false);
	if (ret < 0) {
		LOG_ERR("hub diag 8a: two-slave read failed (%d)", ret);
	}
	hub_status_log("8a: two slaves, no pull-up");

	ret = hub_diag_two_slave(bmp_addr, true);
	if (ret < 0) {
		LOG_ERR("hub diag 8b: two-slave read failed (%d)", ret);
	}
	hub_status_log("8b: two slaves, SHUB_PU_EN");

	/* 9. Map numop to the number of bytes actually read. The LIS2MDL interrupt is
	 *    never enabled by this firmware, so INT_THS_L/H are inert and safe to
	 *    use as a write pattern: reading INT_CTRL (0x63) three bytes deep must
	 *    show A5 at offset 2 and 5A at offset 3 when numop counts bytes + 1, and
	 *    only A5 when numop counts bytes. The same write also re-proves that the
	 *    one-shot write path works on a slave that acknowledges.
	 */
	ret = shub_write_byte(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_INT_THS_L_REG,
			      LIS2MDL_INT_THS_L_PATTERN);
	if (ret < 0) {
		LOG_ERR("hub diag 9: INT_THS_L write failed (%d)", ret);
	} else {
		ret = shub_write_byte(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_INT_THS_H_REG,
				      LIS2MDL_INT_THS_H_PATTERN);
		if (ret < 0) {
			LOG_ERR("hub diag 9: INT_THS_H write failed (%d)", ret);
		}
	}

	ret = hub_single_read(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_INT_CTRL_REG, 3U, block,
			      sizeof(block));
	if (ret < 0) {
		LOG_ERR("hub diag 9: numop = 3 read failed (%d)", ret);
	} else {
		LOG_INF("hub diag 9: 0x1e[0x63] numop=3 -> %02x %02x %02x %02x %02x %02x "
			"(A5 at 2 and 5A at 3 = numop + 1 bytes)", block[0], block[1],
			block[2], block[3], block[4], block[5]);
	}
	hub_status_log("9: numop mapping");

	LOG_INF("hub diag: summary -> LIS2MDL control above must be 0x40; if it is, "
		"every 0x00 from U4 is a hardware answer at 0x%02x", bmp_addr);

	k_mutex_unlock(&imu_lock);

	return ret;
}

int lgps_imu_probe_hub(struct lgps_imu_hub_ids *ids)
{
	uint8_t mag_id = 0U;
	uint8_t baro_id = 0U;
	int ret = 0;

	if (ids == NULL) {
		return -EINVAL;
	}

	memset(ids, 0, sizeof(*ids));

	k_mutex_lock(&imu_lock, K_FOREVER);

	/*
	 * One slave per hub cycle: the single slave's first byte always lands in
	 * SENSOR_HUB_1, so the identification does not depend on how numop is
	 * counted. (The earlier two-slave, numop = 0 read never transferred
	 * anything and reported a false MISMATCH for both parts.) The BMP388
	 * strapping is only known from the netlist, so both addresses are tried.
	 */
	for (uint8_t attempt = 0U; attempt < 3U; attempt++) {
		ret = hub_single_read(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_WHO_AM_I_REG, 1U,
				      &mag_id, 1U);
		if (ret < 0) {
			break;
		}

		ret = hub_single_read(bmp388_addr, (uint8_t)BMP388_CHIP_ID_REG, 1U,
				      &baro_id, 1U);
		if (ret < 0) {
			break;
		}

		ids->lis2mdl_who_am_i = mag_id;
		ids->bmp388_chip_id = baro_id;
		ids->lis2mdl_ok = (mag_id == LGPS_LIS2MDL_WHO_AM_I);
		ids->bmp388_ok = (baro_id == LGPS_BMP388_CHIP_ID);

		if (ids->bmp388_ok) {
			break;
		}

		if (bmp388_addr == LGPS_BMP388_I2C_ADDR) {
			bmp388_addr = LGPS_BMP388_ALT_I2C_ADDR;
		} else {
			bmp388_addr = LGPS_BMP388_I2C_ADDR;
		}
	}

	if (ret == 0) {
		LOG_INF("sensor hub probe: LIS2MDL WHO_AM_I=0x%02x (%s) BMP388 CHIP_ID=0x%02x "
			"@0x%02x (%s)", mag_id, ids->lis2mdl_ok ? "ok" : "MISMATCH", baro_id,
			bmp388_addr, ids->bmp388_ok ? "ok" : "MISMATCH");
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

		/* Master is still enabled here, so this is the streaming layout's own
		 * verdict: SLAVE1_NACK set means U4 did not acknowledge its address
		 * (DS11976 section 9.40). hub_status_log() only prints the snapshot.
		 */
		hub_status_capture();
		hub_status_log("streaming first burst");
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
