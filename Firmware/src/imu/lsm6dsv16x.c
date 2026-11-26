/*
 * Copyright (c) 2026 Life_GPS contributors
 * SPDX-License-Identifier: MIT
 *
 * LSM6DSV16X driver - the die inside the LSM6DSVTR fitted as U12 on Main_V0.2.
 * Every register address, bit position and sensitivity is quoted from ST
 * datasheet DS13510 Rev 4 (Doc/Hardware/Datasheets/LSM6DSV16X_Datasheet.pdf)
 * next to the value that uses it, and the FIFO/SFLP encodings are cross-checked
 * against ST's LSM6DSV16X standard driver examples.
 *
 * What differs from the LSM6DSR this file replaces (every item is a register-map
 * fact, not a behaviour change):
 *   - SPI framing is unchanged: section 5.1.3 figure 10 labels the address byte
 *     "RW AD6 AD5 AD4 AD3 AD2 AD1 AD0" and data is sent MSb first, so despite the
 *     prose calling RW "bit 0" (ST numbers the SPI bits in *transmission* order,
 *     not as byte positions) the read bit is the MSB of the address byte. A read
 *     is 0x80 | reg and a write is reg & 0x7F - the same form the LSM6DSR driver
 *     used, and what ST's own LSM6DSV16X platform_read() does (reg |= 0x80).
 *   - Full scale is not in CTRL1/CTRL2 any more: FS_XL[1:0] is in CTRL8 (17h,
 *     table 68) and FS_G[3:0] in CTRL6 (15h, table 63). CTRL1/CTRL2 keep only
 *     OP_MODE_[XL|G][2:0] and ODR_[XL|G][3:0].
 *   - The ODR set is 1.875 / 7.5 / 15 / 30 / 60 / 120 / 240 / 480 Hz and up
 *     (tables 52 and 55). There is no 26 Hz, so this firmware runs at 30 Hz and
 *     the ZUPT window is 30 samples for the same 1.000 s (see lifegps/nav.h).
 *   - Sensor hub: SHUB_ODR lives in SLV0_CONFIG[7:5] (table 483); SLV1_CONFIG
 *     bit 4 must be written as 1 (table 488, note 2); the per-slave NACK bits and
 *     endop are on the main page at STATUS_MASTER_MAINPAGE (48h, table 136), so
 *     the status can be sampled with the bank closed; the auxiliary pull-up is
 *     IF_CFG.SHUB_PU_EN (03h, table 30), not a MASTER_CONFIG bit.
 *   - SFLP (section 2.8) has no register output. The game rotation vector, the
 *     gravity vector and the gyro bias appear only as FIFO entries tagged
 *     0x13 / 0x17 / 0x16 (table 218); lifegps/sflp.h owns that decoding.
 *
 * The two external sensors behind the hub are identified by the netlist:
 *   LIS2MDL  (U3) 7-bit address 0x1E, WHO_AM_I (0x4F) = 0x40
 *   BMP388   (U4) 7-bit address 0x76 (SDO tied to GND), CHIP_ID (0x00) = 0x50
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include <lifegps/bmp388.h>
#include <lifegps/imu.h>
#include <lifegps/sflp.h>

LOG_MODULE_REGISTER(lgps_imu, LOG_LEVEL_INF);

/* --- LSM6DSV16X register map (DS13510 section 8, table 24) ---------------- */
#define REG_FUNC_CFG_ACCESS 0x01U
#define REG_IF_CFG 0x03U
#define REG_FIFO_CTRL3 0x09U
#define REG_FIFO_CTRL4 0x0AU
#define REG_INT1_CTRL 0x0DU
#define REG_WHO_AM_I 0x0FU
#define REG_CTRL1 0x10U
#define REG_CTRL2 0x11U
#define REG_CTRL3 0x12U
#define REG_CTRL6 0x15U
#define REG_CTRL8 0x17U
#define REG_FIFO_STATUS1 0x1BU
#define REG_FIFO_STATUS2 0x1CU
#define REG_STATUS_REG 0x1EU
#define REG_OUT_TEMP_L 0x20U
#define REG_WAKE_UP_SRC 0x45U
#define REG_STATUS_MASTER_MAINPAGE 0x48U
#define REG_FUNCTIONS_ENABLE 0x50U
#define REG_WAKE_UP_THS 0x5BU
#define REG_WAKE_UP_DUR 0x5CU
#define REG_MD1_CFG 0x5EU
#define REG_FIFO_DATA_OUT_TAG 0x78U

/* Embedded functions bank (visible while EMB_FUNC_REG_ACCESS is set). */
#define REG_EMB_FUNC_EN_A 0x04U
#define REG_EMB_FUNC_FIFO_EN_A 0x44U
#define REG_SFLP_ODR 0x5EU

/* Sensor hub bank (visible while SHUB_REG_ACCESS is set). */
#define REG_SENSOR_HUB_1 0x02U
#define REG_MASTER_CONFIG 0x14U
#define REG_SLV0_ADD 0x15U
#define REG_SLV0_SUBADD 0x16U
#define REG_SLV0_CONFIG 0x17U
#define REG_SLV1_ADD 0x18U
#define REG_SLV1_SUBADD 0x19U
#define REG_SLV1_CONFIG 0x1AU
#define REG_SLV2_ADD 0x1BU
#define REG_SLV2_SUBADD 0x1CU
#define REG_SLV2_CONFIG 0x1DU
#define REG_DATAWRITE_SLV0 0x21U

/* Bit definitions. */
#define FUNC_CFG_SHUB_REG_ACCESS BIT(6)     /* FUNC_CFG_ACCESS (0x01), table 26 */
#define FUNC_CFG_EMB_FUNC_REG_ACCESS BIT(7) /* same register, same table */
#define IF_CFG_SHUB_PU_EN BIT(6)            /* IF_CFG (0x03), table 30 */
#define CTRL3_BDU BIT(6)                    /* CTRL3 (0x12), table 57 */
#define CTRL3_IF_INC BIT(2)
#define CTRL3_SW_RESET BIT(0)
#define MASTER_RST_REGS BIT(7)              /* MASTER_CONFIG (0x14), table 477 */
#define MASTER_WRITE_ONCE BIT(6)
#define MASTER_ON BIT(2)
#define SLV0_ADD_RW BIT(0)                  /* SLV0_ADD (0x15), table 479 */
#define SLV1_CONFIG_MUST_ONE BIT(4)         /* SLV1_CONFIG (0x1A), table 488 note 2 */
#define STATUS_XLDA BIT(0)                  /* STATUS_REG (0x1E), table 83 */
#define STATUS_GDA BIT(1)
#define INT1_DRDY_XL BIT(0)                 /* INT1_CTRL (0x0D), table 46 */
#define INT1_DRDY_G BIT(1)
#define MASTER_STATUS_WR_ONCE_DONE BIT(7)   /* STATUS_MASTER_MAINPAGE (0x48), table 137 */
#define MASTER_STATUS_SLAVE1_NACK BIT(4)
#define MASTER_STATUS_SLAVE0_NACK BIT(3)
#define MASTER_STATUS_ENDOP BIT(0)
#define FIFO_STATUS2_DIFF_8 BIT(0)          /* FIFO_STATUS2 (0x1C), table 79 */
#define EMB_FUNC_EN_A_SFLP_GAME BIT(1)      /* EMB_FUNC_EN_A (0x04), table 266 */
#define EMB_FUNC_FIFO_EN_A_SFLP_GAME BIT(1)    /* (0x44), table 296 */
#define EMB_FUNC_FIFO_EN_A_SFLP_GRAVITY BIT(4)
#define FUNCTIONS_ENABLE_INT_EN BIT(7)      /* FUNCTIONS_ENABLE (0x50), M1_BRINGUP.md */
#define MD1_CFG_INT1_WU BIT(5)              /* MD1_CFG (0x5E), M1_BRINGUP.md */
#define WAKE_UP_THS_0P25G 32U               /* 32 * 7.8125 mg/LSB = 0.25 g */

/* Control values picked for this firmware. */
#define CTRL1_ODR_POWER_DOWN 0x00U
#define CTRL1_ODR_15HZ 0x03U      /* ODR_XL[3:0] = 0011b, table 52 (15 Hz) */
#define CTRL1_ODR_30HZ 0x04U      /* ODR_XL[3:0] = 0100b, table 52 (30 Hz) */
#define CTRL2_ODR_POWER_DOWN 0x00U
#define CTRL2_ODR_30HZ 0x04U      /* ODR_G[3:0] = 0100b, table 55 (30 Hz) */
#define CTRL6_FS_1000DPS 0x03U    /* FS_G[3:0] = 0011b, table 63 (+/-1000 dps) */
#define CTRL8_FS_4G 0x01U         /* FS_XL[1:0] = 01b, table 68 (+/-4 g) */
#define OP_MODE_HIGH_PERFORMANCE 0x00U /* OP_MODE_[XL|G][2:0] = 000b */
#define SHUB_ODR_30HZ 0x02U       /* SLV0_CONFIG[7:5] = 010b, table 483 */
#define SFLP_GAME_ODR_30HZ 0x08U  /* SFLP_ODR[5:3] = 001b, table 324 */
#define FIFO_MODE_CONTINUOUS 0x06U /* FIFO_CTRL4[2:0], table 40 */

/* External sensor registers (their own datasheets). */
#define LIS2MDL_CFG_REG_A 0x60U
#define LIS2MDL_CFG_REG_C 0x62U
#define LIS2MDL_OUTX_L_REG 0x68U
#define LIS2MDL_TEMP_OUT_L_REG 0x6EU
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

/*
 * Hub block layout. numop is a 3-bit field, so one slave transfers at most seven
 * bytes; asking for eight silently reinterprets bit 3 as BATCH_EXT_SENS_0_EN and
 * sets numop = 0, i.e. no read at all (that is what broke the magnetometer on the
 * 2026-11-19 build). The LIS2MDL temperature therefore gets its own slave.
 *
 * The order puts the devices that are known to answer first: the hub fills
 * SENSOR_HUB_1.. in slave order, and the BMP388 on the 2026-11 bench unit did
 * not respond. Slaves behind a device that NACKs are not reliably filled, so
 * the magnetometer temperature must not sit behind it.
 *
 *   Sensor 0: LIS2MDL 0x68, 6 bytes (magnetometer)   -> hub 0..5
 *   Sensor 1: LIS2MDL 0x6e, 2 bytes (TEMP_OUT)       -> hub 6..7
 *   Sensor 2: BMP388  0x04, 6 bytes (pressure + T)   -> hub 8..13
 */
#define HUB_MAX_NUMOP 7U
#define HUB_MAG_BYTES 6U
#define HUB_MAG_TEMP_BYTES 2U
#define HUB_BARO_BYTES 6U
#define HUB_BLOCK_BYTES (HUB_MAG_BYTES + HUB_MAG_TEMP_BYTES + HUB_BARO_BYTES)
/* Sample burst: OUT_TEMP (0x20/0x21) is immediately followed by the gyro and
 * accelerometer outputs, so one burst covers all three quantities.
 */
#define IMU_BURST_BYTES 14U

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
 * triggered by the accelerometer/gyro data-ready, which runs at 30 Hz, so two
 * trigger periods are 67 ms.
 */
#define HUB_DIAG_READ_MS 150

/*
 * Sensor hub settle time: with START_CONFIG = 0 the hub cycle is triggered by
 * the accelerometer/gyro data-ready, which runs at 30 Hz (33 ms), so 150 ms
 * guarantees at least two complete hub cycles before any register is sampled.
 */
#define SHUB_SETTLE_MS 150

/* FIFO entries drained per lgps_imu_read_sflp() call. At 30 Hz SFLP and a 10 ms
 * poll period two entries is the normal case; the cap only bounds a catch-up
 * burst so a stalled main loop cannot turn into an unbounded SPI loop.
 */
#define SFLP_MAX_ENTRIES_PER_READ 16U

#define SPI_OP (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

static const struct spi_dt_spec imu_spi = SPI_DT_SPEC_GET(DT_NODELABEL(imu), SPI_OP, 0);
static K_MUTEX_DEFINE(imu_lock);
static bool hub_streaming_configured;
static bool sflp_configured;
static uint8_t detected_who_am_i;
static uint8_t bmp388_addr = LGPS_BMP388_I2C_ADDR;
/* Set by lgps_imu_probe_hub(). A device that never answered its identification
 * read must not have its zeros reported as measurements.
 */
static bool mag_present;
static bool baro_present;

/*
 * BMP388 trimming coefficients, read once in lgps_imu_hub_configure() while the
 * part is still in sleep mode. Without them the 24-bit raw values have no unit
 * (see lifegps/bmp388.h), so "baro_calib_valid == false" means the telemetry
 * must print raw counts only.
 */
static struct lgps_bmp388_calib baro_calib;
static bool baro_calib_valid;
/* One-shot guard: an out-of-range compensated value is a finding, not a sample. */
static bool baro_range_warned;
/* One-shot guard for "SFLP was enabled but the FIFO never carried a quaternion". */
static bool sflp_empty_warned;

/*
 * Last sensor hub transaction status. It has to be sampled while the master is
 * still enabled: clearing MASTER_ON resets endop and the NACK bits, so a later
 * read returns zeros. Written and read under imu_lock, like every other access
 * to the auxiliary bus.
 */
static uint8_t last_hub_status;

/*
 * SPI framing: figure 10 sends "RW AD6..AD0" MSb first, so the read bit is the
 * MSB of the address byte: a read is 0x80 | reg, a write is reg & 0x7F
 * (DS13510 section 5.1.3). The prose in that section calls RW "bit 0" because it
 * counts transmission order, not byte positions - reading it as an LSB would
 * halve the register address and silently talk to the wrong register.
 */
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

	tx[0] = (uint8_t)(reg | 0x80U); /* READ bit is the MSB, section 5.1.3 */
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

/* Read/modify/write one register. Callers hold imu_lock. */
static int imu_update_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t current = 0U;
	int ret = imu_read_buf(reg, &current, 1U);

	if (ret < 0) {
		return ret;
	}

	return imu_write(reg, (uint8_t)((current & (uint8_t)~mask) | (value & mask)));
}

static int shub_bank_enable(bool enable)
{
	return imu_write(REG_FUNC_CFG_ACCESS, enable ? FUNC_CFG_SHUB_REG_ACCESS : 0x00U);
}

static int emb_bank_enable(bool enable)
{
	return imu_write(REG_FUNC_CFG_ACCESS, enable ? FUNC_CFG_EMB_FUNC_REG_ACCESS : 0x00U);
}

static void hub_status_capture(void)
{
	int ret = imu_read_buf(REG_STATUS_MASTER_MAINPAGE, &last_hub_status, 1U);

	if (ret < 0) {
		LOG_ERR("sensor hub status read failed (%d)", ret);
	}
}

/*
 * Sample STATUS_MASTER_MAINPAGE for the transaction that just finished. 0x48 is
 * a main-page register, so the sensor hub bank has to be closed first: with
 * SHUB_REG_ACCESS set, 0x48 is not the status register. The master must still be
 * enabled, because clearing MASTER_ON resets endop and the NACK bits. The bank is
 * reopened afterwards so the caller can finish its own cleanup.
 */
static void hub_status_capture_main_page(void)
{
	int ret;

	ret = shub_bank_enable(false);
	if (ret < 0) {
		LOG_ERR("sensor hub bank close failed (%d)", ret);
		return;
	}

	hub_status_capture();

	ret = shub_bank_enable(true);
	if (ret < 0) {
		LOG_ERR("sensor hub bank reopen failed (%d)", ret);
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

	/* numop = 0 -> single write operation; SHUB_ODR keeps its 30 Hz setting. */
	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)(SHUB_ODR_30HZ << 5));
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_MASTER_CONFIG, MASTER_WRITE_ONCE | MASTER_ON);
	if (ret < 0) {
		return ret;
	}

	k_msleep(SHUB_SETTLE_MS);

	/* Sample endop/wr_once_done/NACK before the master is switched off. */
	hub_status_capture_main_page();

	ret = imu_write(REG_MASTER_CONFIG, 0x00U);
	if (ret < 0) {
		return ret;
	}

	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)(SHUB_ODR_30HZ << 5));
	if (ret < 0) {
		return ret;
	}

	return shub_bank_enable(false);
}

/* --- Bring-up diagnostics for the auxiliary bus ------------------------- */

/*
 * One-shot read of a single external sensor into SENSOR_HUB_1..N. The sensor hub
 * numop field is the number of read operations, and the 2026-11-19 bench run
 * showed it counts bytes exactly (numop = 0 transferred nothing, numop = 3
 * transferred three bytes), so it is derived from the requested length here
 * instead of being passed in. Only one slave is configured, which keeps the
 * first byte at SENSOR_HUB_1.
 */
static int hub_single_read(uint8_t i2c_addr, uint8_t sub_reg, uint8_t *out,
			   size_t len_bytes)
{
	uint8_t numop = (uint8_t)len_bytes;
	int ret;

	if (out == NULL || len_bytes == 0U || len_bytes > HUB_MAX_NUMOP) {
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

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((i2c_addr << 1) | SLV0_ADD_RW));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, sub_reg);
	if (ret < 0) {
		goto out;
	}

	/* SHUB_ODR = 30 Hz | numop; the cycle itself is gated by the data-ready. */
	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)((SHUB_ODR_30HZ << 5) | numop));
	if (ret < 0) {
		goto out;
	}

	/* AUX_SENS_ON = 00b -> one slave (table 477). */
	ret = imu_write(REG_MASTER_CONFIG, MASTER_ON);
	if (ret < 0) {
		goto out;
	}

	k_msleep(HUB_DIAG_READ_MS);

	ret = imu_read_buf(REG_SENSOR_HUB_1, out, len_bytes);
	if (ret == 0) {
		hub_status_capture_main_page();
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

	ret = hub_single_read(i2c_addr, sub_reg, value, 1U);
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
 * hub never ran".
 */
static void hub_status_log(const char *tag)
{
	LOG_INF("hub diag: %s 0x48=0x%02x endop=%u wr_once_done=%u nack(s0=%u s1=%u)", tag,
		last_hub_status,
		(unsigned int)((last_hub_status & MASTER_STATUS_ENDOP) ? 1U : 0U),
		(unsigned int)((last_hub_status & MASTER_STATUS_WR_ONCE_DONE) ? 1U : 0U),
		(unsigned int)((last_hub_status & MASTER_STATUS_SLAVE0_NACK) ? 1U : 0U),
		(unsigned int)((last_hub_status & MASTER_STATUS_SLAVE1_NACK) ? 1U : 0U));
}

/*
 * Two-slave reproduction of the streaming layout (AUX_SENS_ON = 01b, table 477)
 * with the identification registers as payload: Sensor 0 = LIS2MDL WHO_AM_I,
 * Sensor 1 = BMP388 CHIP_ID. A healthy run logs 0x40 first and 0x50 seventh.
 * pull_up toggles IF_CFG.SHUB_PU_EN (table 30), so running it twice gives an A/B
 * answer on the auxiliary bus electrical side.
 */
static int hub_diag_two_slave(uint8_t bmp_addr, bool pull_up)
{
	uint8_t block[12] = { 0U };
	uint8_t if_cfg = 0U;
	int ret;

	if (pull_up) {
		/* IF_CFG is a main-page register and is not reset by SW_RESET, so it is
		 * read back and restored rather than overwritten with a constant.
		 */
		ret = imu_read_buf(REG_IF_CFG, &if_cfg, 1U);
		if (ret < 0) {
			return ret;
		}

		ret = imu_write(REG_IF_CFG, (uint8_t)(if_cfg | IF_CFG_SHUB_PU_EN));
		if (ret < 0) {
			return ret;
		}
	}

	ret = shub_bank_enable(true);
	if (ret < 0) {
		goto restore;
	}

	ret = imu_write(REG_MASTER_CONFIG, MASTER_RST_REGS);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_MASTER_CONFIG, 0x00U);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((LGPS_LIS2MDL_I2C_ADDR << 1) | SLV0_ADD_RW));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, LIS2MDL_WHO_AM_I_REG);
	if (ret < 0) {
		goto out;
	}

	/* numop = 6: address plus five following registers. */
	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)((SHUB_ODR_30HZ << 5) | 0x06U));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_ADD, (uint8_t)((bmp_addr << 1) | SLV0_ADD_RW));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_SUBADD, (uint8_t)BMP388_CHIP_ID_REG);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_CONFIG, (uint8_t)(SLV1_CONFIG_MUST_ONE | 0x06U));
	if (ret < 0) {
		goto out;
	}

	/* AUX_SENS_ON = 01b -> two sensors (table 477), master on. */
	ret = imu_write(REG_MASTER_CONFIG, (uint8_t)(BIT(0) | MASTER_ON));
	if (ret < 0) {
		goto out;
	}

	k_msleep(HUB_DIAG_READ_MS);

	ret = imu_read_buf(REG_SENSOR_HUB_1, block, sizeof(block));
	if (ret == 0) {
		hub_status_capture_main_page();
	}

out:
	(void)imu_write(REG_MASTER_CONFIG, 0x00U);
	(void)shub_bank_enable(false);

restore:
	if (pull_up) {
		(void)imu_write(REG_IF_CFG, if_cfg);
	}

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
	/* Base the rest of the run on the netlist strapping; step 3 overrides it if
	 * the other address is the one that answers. The probe leaves bmp388_addr on
	 * whichever address it tried last, which is not a finding.
	 */
	uint8_t bmp_addr = LGPS_BMP388_I2C_ADDR;
	int ret = 0;

	k_mutex_lock(&imu_lock, K_FOREVER);

	/* 1. Control. LIS2MDL sits on the same auxiliary bus and is known to be
	 *    populated, so it answers the question "is the hub or the sensor
	 *    broken": address 0x1e, register 0x4f, value 0x40.
	 */
	ret = hub_single_read(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_WHO_AM_I_REG, block, 1U);
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

		ret = hub_single_read(addr, BMP388_CHIP_ID_REG, &value, 1U);
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
	ret = hub_single_read(LGPS_BMP388_I2C_ADDR, BMP388_CHIP_ID_REG, block,
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
		ret = hub_single_read(bmp_addr, BMP388_CHIP_ID_REG, block, sizeof(block));
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
		ret = hub_single_read(bmp_addr, BMP388_OSR, &value, 1U);
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
	 *    auxiliary pull-ups (IF_CFG bit 6, table 30). A NACK that disappears in
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

	/* 9. Confirm the numop-to-bytes mapping. The LIS2MDL interrupt is never
	 *    enabled by this firmware, so INT_THS_L/H are inert and safe to use as a
	 *    write pattern. The 2026-11-19 run read three bytes from INT_CTRL (0x63)
	 *    and saw A5 at offset 2 but no 5A at offset 3, which says numop counts
	 *    bytes exactly. That verdict only holds if the second pattern write
	 *    really landed, so read INT_THS_H on its own as well.
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

	ret = hub_single_read(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_INT_CTRL_REG, block,
			      sizeof(block));
	if (ret < 0) {
		LOG_ERR("hub diag 9: numop = 3 read failed (%d)", ret);
	} else {
		LOG_INF("hub diag 9: 0x1e[0x63] three bytes -> %02x %02x %02x (A5 at 2 means "
			"0x63..0x65 arrived)", block[0], block[1], block[2]);
	}

	/* The high threshold byte alone: this proves the second pattern write landed,
	 * which is what makes the missing 5A above a byte-count finding and not a
	 * dropped write.
	 */
	ret = hub_diag_read1(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_INT_THS_H_REG, &value,
			     "INT_THS_H readback");
	if (ret == 0) {
		LOG_INF("hub diag 9: INT_THS_H wrote 0x%02x read 0x%02x %s -> numop counts %s",
			LIS2MDL_INT_THS_H_PATTERN, value,
			(value == LIS2MDL_INT_THS_H_PATTERN) ? "OK" : "LOST",
			(value == LIS2MDL_INT_THS_H_PATTERN) ? "bytes" : "something else");
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
		ret = hub_single_read(LGPS_LIS2MDL_I2C_ADDR, LIS2MDL_WHO_AM_I_REG,
				      &mag_id, 1U);
		if (ret < 0) {
			break;
		}

		ret = hub_single_read(bmp388_addr, (uint8_t)BMP388_CHIP_ID_REG, &baro_id,
				      1U);
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

	mag_present = ids->lis2mdl_ok;
	baro_present = ids->bmp388_ok;

	/* The toggle above is a probe tactic, not a finding: when nothing answered,
	 * go back to the netlist strapping so the streaming log and the status frame
	 * do not claim 0x77. */
	if (!baro_present) {
		bmp388_addr = LGPS_BMP388_I2C_ADDR;
	}

	if (ret == 0) {
		LOG_INF("sensor hub probe: LIS2MDL WHO_AM_I=0x%02x (%s) BMP388 CHIP_ID=0x%02x "
			"@0x%02x (%s)", mag_id, ids->lis2mdl_ok ? "ok" : "MISMATCH", baro_id,
			bmp388_addr, ids->bmp388_ok ? "ok" : "MISMATCH");

		if (!baro_present) {
			LOG_WRN("BMP388 (U4) did not acknowledge at either strapping address; "
				"pressure and its temperature stay invalid until it does");
		}
	}

	k_mutex_unlock(&imu_lock);

	return ret;
}

/*
 * Read the BMP388 trimming coefficients (0x31..0x45, table 23) through the hub
 * and turn them into the scaled floats of datasheet section 9.1.
 *
 * The block is 21 bytes and the hub's numop field carries at most seven bytes
 * per slave (HUB_MAX_NUMOP), so it is fetched as three consecutive 7-byte reads.
 * This has to run while the part is still in sleep mode: there is nothing in the
 * datasheet that forbids reading NVM in normal mode, but the conversion engine
 * has no reason to be running during a configuration read.
 */
static int bmp388_read_calib(uint8_t addr)
{
	uint8_t nvm[LGPS_BMP388_NVM_LEN];
	int ret;

	/* IF_CONF (table 39): the NVM copy is the longest hub transaction in this
	 * firmware, and the NVM default is the I2C watchdog with the 1.25 ms short
	 * timeout. Raise it to the 40 ms setting first, which is the same write the
	 * hub diagnostics use to separate a watchdog abort from a dead part.
	 */
	ret = shub_write_byte(addr, BMP388_IF_CONF, BMP388_IF_CONF_WDT_LONG);
	if (ret < 0) {
		LOG_ERR("BMP388 IF_CONF write failed (%d)", ret);
		return ret;
	}

	for (uint8_t chunk = 0U; chunk < 3U; chunk++) {
		const uint8_t offset = (uint8_t)(chunk * HUB_MAX_NUMOP);
		const uint8_t base = (uint8_t)(LGPS_BMP388_NVM_ADDR + offset);

		ret = hub_single_read(addr, base, &nvm[offset], HUB_MAX_NUMOP);
		if (ret < 0) {
			LOG_ERR("BMP388 NVM read at 0x%02x failed (%d)", base, ret);
			return ret;
		}

		/* Keep the raw block in the log: the coefficients can be recomputed from it
		 * offline with section 9.1, which is what a support case needs. Logged per
		 * chunk to stay inside the logger's argument budget.
		 */
		LOG_INF("BMP388 TPG NVM 0x%02x..0x%02x: %02x %02x %02x %02x %02x %02x %02x",
			base, (unsigned int)(base + HUB_MAX_NUMOP - 1U), nvm[offset],
			nvm[offset + 1U], nvm[offset + 2U], nvm[offset + 3U],
			nvm[offset + 4U], nvm[offset + 5U], nvm[offset + 6U]);
	}

	ret = lgps_bmp388_calib_decode(nvm, &baro_calib);
	if (ret < 0) {
		LOG_WRN("BMP388 TPG coefficients unusable (%d): the block reads back all "
			"0x00/0xFF, so pressure and temperature stay raw counts", ret);
		return ret;
	}

	LOG_INF("BMP388 TPG decoded: T1=%.0f T2=%.3e T3=%.3e P1=%.6f (pressure is "
		"compensated from here on)", (double)baro_calib.par_t1,
		(double)baro_calib.par_t2, (double)baro_calib.par_t3,
		(double)baro_calib.par_p1);

	baro_calib_valid = true;

	return 0;
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

	/* BMP388 TPG coefficients first: they are the only way the raw 24-bit values
	 * ever become Pa and degC, and reading them does not disturb the part.
	 */
	if (baro_present) {
		const int calib_ret = bmp388_read_calib(bmp388_addr);

		/* Not fatal: streaming continues and the telemetry keeps reporting raw
		 * counts instead of physical values.
		 */
		if (calib_ret < 0) {
			LOG_WRN("BMP388 TPG compensation disabled (%d): pressure and "
				"temperature stay raw counts", calib_ret);
		}
	} else {
		LOG_WRN("BMP388 never answered, no TPG coefficients: pressure and "
			"temperature stay raw counts");
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

	/* Read mode: 6 bytes from LIS2MDL + 2 bytes of LIS2MDL temperature + 6 bytes
	 * from BMP388.
	 */
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

	ret = imu_write(REG_SLV0_ADD, (uint8_t)((LGPS_LIS2MDL_I2C_ADDR << 1) | SLV0_ADD_RW));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV0_SUBADD, (uint8_t)LIS2MDL_OUTX_L_REG);
	if (ret < 0) {
		goto out;
	}

	/* SHUB_ODR = 30 Hz | numop = 6 (six magnetometer bytes, see HUB_MAX_NUMOP).
	 * The cycle itself is triggered by the accelerometer/gyro data-ready, so the
	 * effective rate is the 30 Hz sample rate whatever this field asks for
	 * (DS13510 table 477, START_CONFIG = 0).
	 */
	ret = imu_write(REG_SLV0_CONFIG, (uint8_t)((SHUB_ODR_30HZ << 5) | HUB_MAG_BYTES));
	if (ret < 0) {
		goto out;
	}

	/* Second slave: LIS2MDL TEMP_OUT, two bytes (the temperature refresh rate is
	 * the magnetometer ODR, so this tracks the 50 Hz setting). Bit 4 of
	 * SLV1_CONFIG must be written as 1 (table 488 note 2).
	 */
	ret = imu_write(REG_SLV1_ADD, (uint8_t)((LGPS_LIS2MDL_I2C_ADDR << 1) | SLV0_ADD_RW));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_SUBADD, LIS2MDL_TEMP_OUT_L_REG);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV1_CONFIG,
			(uint8_t)(SLV1_CONFIG_MUST_ONE | HUB_MAG_TEMP_BYTES));
	if (ret < 0) {
		goto out;
	}

	/* Third slave: BMP388 pressure + temperature. Last, because a device that
	 * does not acknowledge must not sit in front of one that does.
	 */
	ret = imu_write(REG_SLV2_ADD, (uint8_t)((bmp388_addr << 1) | SLV0_ADD_RW));
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV2_SUBADD, (uint8_t)BMP388_PRESS_XLSB);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_SLV2_CONFIG, HUB_BARO_BYTES);
	if (ret < 0) {
		goto out;
	}

	/* AUX_SENS_ON = 10b -> three sensors (table 477), master on. */
	ret = imu_write(REG_MASTER_CONFIG, BIT(1) | MASTER_ON);
	if (ret < 0) {
		goto out;
	}

	hub_streaming_configured = true;

	k_msleep(SHUB_SETTLE_MS);

	uint8_t check[HUB_BLOCK_BYTES] = { 0U };

	if (imu_read_buf(REG_SENSOR_HUB_1, check, sizeof(check)) == 0) {
		LOG_INF("sensor hub streaming: 3 slaves (LIS2MDL mag 6B, LIS2MDL temp 2B, "
			"BMP388 6B), hub cycle gated by the 30 Hz IMU data-ready, %u of 18 bytes "
			"used @0x%02x", (unsigned int)HUB_BLOCK_BYTES, bmp388_addr);
		LOG_INF("sensor hub first burst: mag %02x %02x %02x %02x %02x %02x | mag_T "
			"%02x %02x | baro %02x %02x %02x %02x %02x %02x", check[0], check[1],
			check[2], check[3], check[4], check[5], check[6], check[7], check[8],
			check[9], check[10], check[11], check[12], check[13]);

		if ((check[0] == 0U) && (check[1] == 0U) && (check[2] == 0U)) {
			LOG_ERR("LIS2MDL returned zeros although it answered its WHO_AM_I read: "
				"check the sensor hub block layout and numop");
		}

		if ((check[6] == 0U) && (check[7] == 0U)) {
			LOG_WRN("LIS2MDL TEMP_OUT reads 0: the second slave did not transfer, "
				"check the hub slave order and AUX_SENS_ON");
		}

		if ((check[8] == 0U) && (check[9] == 0U) && (check[10] == 0U)) {
			LOG_WRN("BMP388 pressure reads 0: check PWR_CTRL write path, SDO strapping "
				"(0x76/0x77) and that U4 is populated");
		}

		/* Master is still enabled here, so this is the streaming layout's own
		 * verdict: SLAVE1_NACK set means U4 did not acknowledge its address
		 * (DS13510 table 137). hub_status_log() only prints the snapshot.
		 */
		hub_status_capture_main_page();
		hub_status_log("streaming first burst");
	}

out:
	(void)shub_bank_enable(false);
	k_mutex_unlock(&imu_lock);
	return ret;
}

/*
 * Enable the SFLP game rotation vector and feed it, plus the gravity vector, into
 * the FIFO (DS13510 section 2.8, tables 266/296/324/40). Called from
 * lgps_imu_init() while imu_lock is held.
 *
 * The rate combination is the one ST's own SFLP example uses
 * (lsm6dsv16x_STdC/examples/lsm6dsv16x_sensor_fusion.c): accelerometer and
 * gyroscope at 30 Hz and SFLP_GAME_ODR at 30 Hz. That example is also the source
 * for the FIFO tag -> payload decoding, see lifegps/sflp.h.
 *
 * No accelerometer/gyroscope batching rate is programmed: the FIFO carries the
 * SFLP entries only, so the 14-byte register burst that drives the ZUPT chain
 * stays independent of the FIFO path.
 */
static int sflp_enable(void)
{
	int ret;

	if (!IS_ENABLED(CONFIG_LGPS_IMU_SFLP)) {
		return 0;
	}

	ret = emb_bank_enable(true);
	if (ret < 0) {
		return ret;
	}

	/* SFLP_GAME_EN (EMB_FUNC_EN_A bit 1). */
	ret = imu_update_bits(REG_EMB_FUNC_EN_A, EMB_FUNC_EN_A_SFLP_GAME,
			      EMB_FUNC_EN_A_SFLP_GAME);
	if (ret < 0) {
		goto out;
	}

	/* Batch the quaternion (bit 1) and the gravity vector (bit 4). */
	ret = imu_update_bits(REG_EMB_FUNC_FIFO_EN_A,
			      EMB_FUNC_FIFO_EN_A_SFLP_GAME | EMB_FUNC_FIFO_EN_A_SFLP_GRAVITY,
			      EMB_FUNC_FIFO_EN_A_SFLP_GAME | EMB_FUNC_FIFO_EN_A_SFLP_GRAVITY);
	if (ret < 0) {
		goto out;
	}

	/* SFLP_GAME_ODR = 30 Hz, matching the accelerometer/gyroscope rate. The two
	 * "must be 1" bits outside the field are preserved by the read/modify/write.
	 */
	ret = imu_update_bits(REG_SFLP_ODR, 0x38U, SFLP_GAME_ODR_30HZ);

out:
	{
		const int bank_ret = emb_bank_enable(false);

		if (ret == 0) {
			ret = bank_ret;
		}
	}

	if (ret < 0) {
		return ret;
	}

	/* Continuous mode: if the FIFO fills up, the newest entry overwrites the
	 * oldest, so a stalled reader degrades into lost samples, never into a
	 * blocked device (table 40).
	 */
	ret = imu_update_bits(REG_FIFO_CTRL4, 0x07U, FIFO_MODE_CONTINUOUS);
	if (ret < 0) {
		return ret;
	}

	sflp_configured = true;

	LOG_INF("SFLP enabled: game rotation vector + gravity vector, 30 Hz, FIFO tag "
		"0x%02x/0x%02x", LGPS_FIFO_TAG_SFLP_GAME_ROTATION,
		LGPS_FIFO_TAG_SFLP_GRAVITY);

	return 0;
}

int lgps_imu_read_sflp(struct lgps_imu_sflp *out)
{
	uint8_t status[2] = { 0U, 0U };
	uint8_t entry[LGPS_FIFO_ENTRY_BYTES];
	uint16_t entries;
	bool got = false;
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	/* Clear first: the two halves arrive as separate FIFO entries, so one valid
	 * flag can stay false while the other half is filled in.
	 */
	memset(out, 0, sizeof(*out));

	k_mutex_lock(&imu_lock, K_FOREVER);

	if (!sflp_configured) {
		k_mutex_unlock(&imu_lock);
		return -EAGAIN;
	}

	/* DIFF_FIFO[8:0]: FIFO_STATUS1 plus bit 0 of FIFO_STATUS2 (tables 77, 79).
	 * One entry is TAG + 6 bytes, which is exactly what one loop iteration
	 * consumes below.
	 */
	ret = imu_read_buf(REG_FIFO_STATUS1, status, sizeof(status));
	if (ret < 0) {
		k_mutex_unlock(&imu_lock);
		return ret;
	}

	entries = (uint16_t)((uint16_t)status[0] |
			     (uint16_t)((status[1] & FIFO_STATUS2_DIFF_8) ? 0x100U : 0U));

	if (entries == 0U) {
		k_mutex_unlock(&imu_lock);
		return -EAGAIN;
	}

	if (entries > SFLP_MAX_ENTRIES_PER_READ) {
		entries = SFLP_MAX_ENTRIES_PER_READ;
	}

	for (uint16_t index = 0U; index < entries; index++) {
		ret = imu_read_buf(REG_FIFO_DATA_OUT_TAG, entry, sizeof(entry));
		if (ret < 0) {
			break;
		}

		switch (lgps_fifo_tag(entry[0])) {
		case LGPS_FIFO_TAG_SFLP_GAME_ROTATION:
			lgps_sflp_game_quat(&entry[1], out->quat);
			out->quat_valid = true;
			got = true;
			break;
		case LGPS_FIFO_TAG_SFLP_GRAVITY:
			for (uint8_t axis = 0U; axis < 3U; axis++) {
				const int16_t raw = (int16_t)((uint16_t)entry[1U + 2U * axis] |
							      ((uint16_t)entry[2U + 2U * axis]
							       << 8));

				out->raw_gravity[axis] = raw;
				out->gravity_g[axis] = lgps_sflp_gravity_mg(raw) / 1000.0F;
			}
			out->gravity_valid = true;
			got = true;
			break;
		default:
			/* Empty tag or a virtual sensor this firmware does not consume. */
			break;
		}
	}

	k_mutex_unlock(&imu_lock);

	if (ret < 0) {
		return ret;
	}

	if (!got && !sflp_empty_warned) {
		sflp_empty_warned = true;
		LOG_WRN("FIFO carried %u entries but no SFLP tag 0x%02x/0x%02x: the "
			"quaternion and gravity stay invalid until one arrives",
			(unsigned int)entries, LGPS_FIFO_TAG_SFLP_GAME_ROTATION,
			LGPS_FIFO_TAG_SFLP_GRAVITY);
	}

	return got ? 0 : -EAGAIN;
}

static void decode_sample(const uint8_t raw[IMU_BURST_BYTES],
			  struct lgps_imu_sample *out)
{
	/* One burst from OUT_TEMP_L (0x20): temperature 0x20..0x21, gyro
	 * 0x22..0x27, accelerometer 0x28..0x2D, all little endian.
	 */
	out->raw_temp = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));

	for (uint8_t axis = 0U; axis < 3U; axis++) {
		out->raw_gyro[axis] = (int16_t)((uint16_t)raw[2U + 2U * axis] |
						((uint16_t)raw[3U + 2U * axis] << 8));
		out->raw_accel[axis] = (int16_t)((uint16_t)raw[8U + 2U * axis] |
						 ((uint16_t)raw[9U + 2U * axis] << 8));
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
	uint8_t raw[IMU_BURST_BYTES];
	int ret;

	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&imu_lock, K_FOREVER);
	ret = imu_read_buf(REG_OUT_TEMP_L, raw, sizeof(raw));
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
	uint8_t raw[IMU_BURST_BYTES];
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

	ret = imu_read_buf(REG_OUT_TEMP_L, raw, sizeof(raw));
	k_mutex_unlock(&imu_lock);

	if (ret < 0) {
		return ret;
	}

	decode_sample(raw, out);

	return 0;
}

int lgps_imu_read_hub(struct lgps_imu_mag *mag, struct lgps_imu_baro *baro)
{
	uint8_t raw[HUB_BLOCK_BYTES] = { 0U };
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

	/* Second hub slave: LIS2MDL TEMP_OUT (0x6e/0x6f). */
	mag->raw_temp = (int16_t)((uint16_t)raw[6] | ((uint16_t)raw[7] << 8));

	/* Third hub slave: BMP388 DATA_0..5, unsigned 24-bit little endian each
	 * (datasheet tables 28 and 29).
	 */
	baro->raw_pressure = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8) | ((uint32_t)raw[10] << 16);
	baro->raw_temperature = (uint32_t)raw[11] | ((uint32_t)raw[12] << 8) |
				((uint32_t)raw[13] << 16);

	baro->pressure_pa = 0.0F;
	baro->temperature_c = 0.0F;
	baro->compensated = false;

	if (baro_calib_valid) {
		if (lgps_bmp388_compensate(&baro_calib, baro->raw_pressure, baro->raw_temperature,
					   &baro->pressure_pa, &baro->temperature_c) == 0) {
			baro->compensated = true;

			/* Outside -40..+85 degC / 300..1250 hPa the polynomial result is a
			 * fault signature, so say so once instead of publishing weather that
			 * cannot exist.
			 */
			if (!baro_range_warned &&
			    ((baro->temperature_c < LGPS_BMP388_TEMP_MIN_C) ||
			     (baro->temperature_c > LGPS_BMP388_TEMP_MAX_C) ||
			     (baro->pressure_pa < LGPS_BMP388_PRESS_MIN_PA) ||
			     (baro->pressure_pa > LGPS_BMP388_PRESS_MAX_PA))) {
				baro_range_warned = true;
				LOG_WRN("BMP388 compensated value out of range (%.2f degC, "
					"%.1f Pa from raw %u/%u): check the TPG block above",
					(double)baro->temperature_c,
					(double)baro->pressure_pa, baro->raw_temperature,
					baro->raw_pressure);
			}
		}
	}

	mag->valid = mag_present;
	baro->valid = baro_present;

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

	if (who_am_i != LGPS_LSM6DSV16X_WHO_AM_I) {
		LOG_ERR("WHO_AM_I=0x%02x, expected 0x%02x (LSM6DSV16X)",
			who_am_i, LGPS_LSM6DSV16X_WHO_AM_I);
		ret = -ENODEV;
		goto out;
	}

	detected_who_am_i = who_am_i;
	LOG_INF("LSM6DSV16X detected on SPIM22 (WHO_AM_I=0x%02x, CS=P1.14, 8 MHz)",
		who_am_i);

	/* Software reset, then BDU + IF_INC (CTRL3 reset value is already 0x44, but
	 * the reset above restores it explicitly rather than by assumption).
	 */
	ret = imu_write(REG_CTRL3, CTRL3_SW_RESET);
	if (ret < 0) {
		goto out;
	}

	k_msleep(10);

	ret = imu_write(REG_CTRL3, CTRL3_BDU | CTRL3_IF_INC);
	if (ret < 0) {
		goto out;
	}

	/* +/-1000 dps in CTRL6, +/-4 g in CTRL8: on this part the full-scale bits
	 * are not part of CTRL1/CTRL2 (tables 63 and 68).
	 */
	ret = imu_write(REG_CTRL6, CTRL6_FS_1000DPS);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_CTRL8, CTRL8_FS_4G);
	if (ret < 0) {
		goto out;
	}

	/* 30 Hz high performance on both chains (OP_MODE_[XL|G] = 000b). Written
	 * last so the data-ready pacing the sensor hub needs starts only once every
	 * control register is in place.
	 */
	ret = imu_write(REG_CTRL1, CTRL1_ODR_30HZ | OP_MODE_HIGH_PERFORMANCE);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_CTRL2, CTRL2_ODR_30HZ | OP_MODE_HIGH_PERFORMANCE);
	if (ret < 0) {
		goto out;
	}

	ret = imu_write(REG_INT1_CTRL, INT1_DRDY_XL | INT1_DRDY_G);
	if (ret < 0) {
		goto out;
	}

	LOG_INF("LSM6DSV16X configured: +/-%.0f g (%.3f mg/LSB), +/-%.0f dps (%.0f mdps/LSB), "
		"ODR %.0f Hz, INT1=DRDY_XL|DRDY_G",
		(double)LGPS_IMU_ACCEL_FS_G, (double)LGPS_IMU_ACCEL_MG_LSB,
		(double)LGPS_IMU_GYRO_FS_DPS, (double)LGPS_IMU_GYRO_MDPS_LSB,
		(double)LGPS_IMU_ODR_HZ);

	ret = sflp_enable();
	if (ret < 0) {
		/* The ZUPT/PDR chain does not depend on SFLP, so a failure here is
		 * reported and the attitude filter keeps running on its own.
		 */
		LOG_ERR("SFLP setup failed (%d): firmware attitude stays the only "
			"orientation source", ret);
		ret = 0;
	}

out:
	k_mutex_unlock(&imu_lock);
	return ret;
}

static const struct gpio_dt_spec imu_int1 =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), imu_int1_gpios);
static struct gpio_callback imu_int1_cb;
static lgps_imu_wake_cb_t imu_wake_cb;
static bool imu_low_power;

static void imu_int1_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (imu_wake_cb != NULL) {
		imu_wake_cb();
	}
}

int lgps_imu_wake_irq_init(lgps_imu_wake_cb_t cb)
{
	int ret;

	imu_wake_cb = cb;

	if (!gpio_is_ready_dt(&imu_int1)) {
		LOG_ERR("IMU INT1 GPIO (P0.02) not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&imu_int1, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&imu_int1_cb, imu_int1_isr, BIT(imu_int1.pin));
	ret = gpio_add_callback(imu_int1.port, &imu_int1_cb);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_DISABLE);
	return ret;
}

int lgps_imu_set_low_power(bool low_power)
{
	int ret;

	if (detected_who_am_i == 0U) {
		return -ENODEV;
	}

	if (low_power == imu_low_power) {
		return 0;
	}

	k_mutex_lock(&imu_lock, K_FOREVER);

	if (low_power) {
		/* Stop DRDY pulse on INT1, enable 0.25 g wake-up routed to INT1,
		 * power down gyro (2.6 uA), drop XL to 15 Hz.
		 */
		ret = imu_write(REG_INT1_CTRL, 0x00U);
		if (ret == 0) {
			ret = imu_write(REG_WAKE_UP_DUR, 0x00U);
		}
		if (ret == 0) {
			ret = imu_write(REG_WAKE_UP_THS, WAKE_UP_THS_0P25G);
		}
		if (ret == 0) {
			ret = imu_write(REG_MD1_CFG, MD1_CFG_INT1_WU);
		}
		if (ret == 0) {
			ret = imu_write(REG_FUNCTIONS_ENABLE, FUNCTIONS_ENABLE_INT_EN);
		}
		if (ret == 0) {
			ret = imu_write(REG_CTRL2, CTRL2_ODR_POWER_DOWN);
		}
		if (ret == 0) {
			ret = imu_write(REG_CTRL1, CTRL1_ODR_15HZ);
		}
		if (ret == 0) {
			imu_low_power = true;
			if (gpio_is_ready_dt(&imu_int1)) {
				(void)gpio_pin_interrupt_configure_dt(&imu_int1,
								      GPIO_INT_EDGE_RISING);
			}
			LOG_INF("IMU -> low-power wake mode (XL 15 Hz, gyro PD, INT1 WU 0.25 g)");
		}
	} else {
		if (gpio_is_ready_dt(&imu_int1)) {
			(void)gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_DISABLE);
		}
		ret = imu_write(REG_MD1_CFG, 0x00U);
		if (ret == 0) {
			ret = imu_write(REG_FUNCTIONS_ENABLE, 0x00U);
		}
		if (ret == 0) {
			ret = imu_write(REG_CTRL1, CTRL1_ODR_30HZ | OP_MODE_HIGH_PERFORMANCE);
		}
		if (ret == 0) {
			ret = imu_write(REG_CTRL2, CTRL2_ODR_30HZ | OP_MODE_HIGH_PERFORMANCE);
		}
		if (ret == 0) {
			ret = imu_write(REG_INT1_CTRL, INT1_DRDY_XL | INT1_DRDY_G);
		}
		if (ret == 0) {
			imu_low_power = false;
			LOG_INF("IMU -> active 30 Hz HP mode");
		}
	}

	k_mutex_unlock(&imu_lock);
	return ret;
}

int lgps_imu_power_down(void)
{
	int ret;

	if (detected_who_am_i == 0U) {
		return 0;
	}

	if (gpio_is_ready_dt(&imu_int1)) {
		(void)gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_DISABLE);
	}

	k_mutex_lock(&imu_lock, K_FOREVER);
	(void)imu_write(REG_MD1_CFG, 0x00U);
	(void)imu_write(REG_INT1_CTRL, 0x00U);
	(void)imu_write(REG_CTRL2, CTRL2_ODR_POWER_DOWN);
	ret = imu_write(REG_CTRL1, CTRL1_ODR_POWER_DOWN);
	imu_low_power = false;
	k_mutex_unlock(&imu_lock);

	return ret;
}

int lgps_imu_read_wake_src(uint8_t *src)
{
	int ret;

	if (src == NULL) {
		return -EINVAL;
	}
	if (detected_who_am_i == 0U) {
		return -ENODEV;
	}

	k_mutex_lock(&imu_lock, K_FOREVER);
	ret = imu_read_buf(REG_WAKE_UP_SRC, src, 1U);
	k_mutex_unlock(&imu_lock);

	return ret;
}
