/*
 * Life_GPS firmware - battery voltage sensing (BAT_ADC, P1.11 = AIN4).
 *
 * Hardware (pinmap section 2/3): divider R25 = 1 M top / R26 = 330 k bottom with
 * C14 = 100 nF. The SAADC input resistance (datasheet section 11.17.1, typical
 * 735 k) sits in parallel with R26 and lowers the divider ratio from 0.248 to
 * about 0.186, so the correction is applied here instead of in a comment.
 */

#ifndef LIFEGPS_BATTERY_H_
#define LIFEGPS_BATTERY_H_

#include <stdint.h>

#define LGPS_BATT_R_TOP_OHM 1000000U   /* R25 */
#define LGPS_BATT_R_BOTTOM_OHM 330000U /* R26 */
#define LGPS_BATT_SAADC_RIN_OHM 735000U

/* Configure the SAADC channel. */
int lgps_battery_init(void);

/*
 * Read the pack voltage in millivolts. Returns 0 on success, negative errno
 * otherwise. No state of charge is computed: the pack discharge curve has not
 * been measured yet, so a percentage would be a made-up number.
 */
int lgps_battery_read_mv(int32_t *millivolts);

#endif /* LIFEGPS_BATTERY_H_ */
