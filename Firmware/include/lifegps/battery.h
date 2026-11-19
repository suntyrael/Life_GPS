/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - battery voltage sensing (BAT_ADC, P1.11 = AIN4).
 *
 * Hardware (pinmap section 2/3, netlist 2026-11-11): divider R25 top / R26 = 330 k
 * bottom with C14 = 100 nF; the netlist claims R25 = 1 M, i.e. a nominal 0.2481 tap.
 *
 * Bench calibration (2026-11-11), two independent points that agree to 0.04 %:
 *   3.314 V pack -> 0.9483 V at AIN4 (ratio 0.28618)
 *   3.307 V pack -> 0.9460 V at AIN4 (ratio 0.28606)
 * so the divider actually fitted is 1:3.4958, not 1:4.03. That is 15 % above the
 * netlist's 0.2481, so the fitted R25 behaves like ~820 k rather than 1 M (820 k
 * with 330 k gives 0.2870, inside 1 % resistor tolerance of the measurement).
 *
 * Consequence to verify on the bench: if the 0.946 V was read with a high-impedance
 * meter while the SAADC was idle (it is off between the 10 s conversions), then the
 * SAADC input resistance (datasheet section 11.17.1, 735 k typ) still loads R26 in
 * firmware and the tap drops to ~0.217 of the pack, i.e. the readback would be
 * ~24 % low (~2.51 V for a 3.307 V pack). Compare the `pin=` value of the LOG_DBG
 * line against the meter: if pin is far below the 0.946 V the meter shows, the
 * R26 || R_in term has to be restored instead of this flat ratio.
 *
 * Recalibrating only needs the two microvolt values below.
 */

#ifndef LIFEGPS_BATTERY_H_
#define LIFEGPS_BATTERY_H_

#include <stdint.h>

/*
 * Calibration pair from the bench measurement: the pack voltage that was applied
 * and the divider tap voltage that was measured at AIN4 with it. Both in
 * microvolts, so the pack/tap quotient is dimensionless.
 */
#define LGPS_BATT_CAL_PACK_UV 3307000U /* 3.307 V applied to the divider top */
#define LGPS_BATT_CAL_TAP_UV 946000U   /* 0.946 V measured at AIN4 */

/* A Li-ion pack cannot sit below this; used to flag an absent pack / open divider. */
#define LGPS_BATT_MV_PLAUSIBLE_MIN 2500

/* Configure the SAADC channel. */
int lgps_battery_init(void);

/*
 * Read the pack voltage in millivolts. Returns 0 on success, negative errno
 * otherwise. No state of charge is computed: the pack discharge curve has not
 * been measured yet, so a percentage would be a made-up number.
 */
int lgps_battery_read_mv(int32_t *millivolts);

#endif /* LIFEGPS_BATTERY_H_ */
