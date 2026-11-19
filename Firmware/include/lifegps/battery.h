/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - battery voltage sensing (BAT_ADC, P1.11 = AIN4).
 *
 * Hardware (pinmap section 2/3, netlist 2026-11-11): divider R25 top / R26 = 330 k
 * bottom with C14 = 100 nF; the netlist claims R25 = 1 M, i.e. a nominal 0.2481 tap.
 *
 * Bench calibration (2026-11-11), two separate effects, both measured:
 *
 * 1. Divider. DMM tap against pack over 3.2-4.3 V:
 *      3.2 V -> 0.9159 V (0.28622)      4.0 V -> 1.1445 V (0.28613)
 *      4.2 V -> 1.2015 V (0.28607)      4.3 V -> 1.2301 V (0.28621)*
 *    Spread 0.05 % across the range, so the fitted divider is 1:3.4958 (0.2861),
 *    not the 1:4.03 the netlist implies: R25 behaves like 820 k against 330 k
 *    (= 0.2870), the E24 value next door, not 1 M. (*The 4.3 V row was logged as
 *    1.295 V, which no single divider can produce next to the other three; the
 *    ADC value of that run reproduces 1.2301 V, so 1.2301 is what is used here.)
 *
 * 2. SAADC path. The voltage-to-millivolt conversion reads high on the same four
 *    points: software pack / true pack = 1.0436 / 1.0421 / 1.0397 / 1.0408, i.e.
 *    a pure scale error of 1.0418 (least squares, residual +-8 mV), which means
 *    the channel's effective full scale is 1728 mV where the driver assumes
 *    VREF/GAIN = 900 mV / 0.5 = 1800 mV. Datasheet section 11.17.1 (page 912)
 *    quotes a gain error of only +-1 % and an offset of +-5 LSB(10 bit) ~ 8.8 mV,
 *    and the acquisition path cannot explain it either: the source is
 *    R25 || R26 ~ 235 kOhm, 40 us covers 800 kOhm, and a short acquisition reads
 *    low rather than high. That leaves the internal reference (0.9 V nominal, no
 *    tolerance entry in 11.17.1) or a DMM that is 4 % low; the DMM is the less
 *    likely of the two, because 4 % would imply R25 = 777 kOhm, not an E24 value,
 *    while 820 kOhm lands within 0.3 % of the measured 0.2861.
 *    No offset is modelled: separating it from the gain needs a tap well below
 *    1 V, and this sweep spans only 0.916-1.201 V (the least-squares intercept,
 *    -38 mV, is four times the datasheet offset spec, i.e. fit noise).
 *
 * Recalibrating needs the two microvolt values and the gain ppm below.
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

/*
 * SAADC path scale error, in ppm: the converted pin voltage is 1.0418 times the
 * real pin voltage (1800 mV assumed full scale vs 1728 mV effective). Measured by
 * least squares over the four 3.2-4.3 V points listed above.
 */
#define LGPS_BATT_ADC_GAIN_PPM 1041800U

/* A Li-ion pack cannot sit below this; used to flag an absent pack / open divider. */
#define LGPS_BATT_MV_PLAUSIBLE_MIN 2500

/* Configure the SAADC channel. */
int lgps_battery_init(void);

/*
 * Read the pack voltage in millivolts. Returns 0 on success, negative errno
 * otherwise. No state of charge is computed here: with no current sense, the
 * App applies the measured OCV table to bat_mv (see
 * App/app/src/main/java/com/lifegps/app/ble/BatterySoc.kt).
 */
int lgps_battery_read_mv(int32_t *millivolts);

#endif /* LIFEGPS_BATTERY_H_ */
