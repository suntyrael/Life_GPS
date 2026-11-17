/*
 * Life_GPS firmware - board level GPIO ownership.
 *
 * Everything in this module is a pin that belongs to no Zephyr subsystem class:
 * the GNSS rail/wakeup lines, the charger handshake and the chip-selects that
 * have no external pull-up. Pin numbers and polarities come from
 * Doc/Hardware/Pinmap_Main_V0.2.md section 2/3 (single source of truth).
 */

#ifndef LIFEGPS_BOARD_IO_H_
#define LIFEGPS_BOARD_IO_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Configure every board GPIO in the safe state: GNSS rail off, charger enabled
 * (device may be on USB), chip-selects deasserted, interrupt inputs ready.
 * Returns 0 on success or a negative errno of the first failing pin.
 */
int lgps_board_io_init(void);

/*
 * Park the six unused pins (P2.00, P2.03, P2.06, P2.08, P2.09, P2.10) as
 * disconnected inputs, which is what the pinmap asks for to keep leakage down.
 */
int lgps_board_io_unused_pins_off(void);

/*
 * GNSS 1.8 V rail (SGM2045-1.8 enable, P1.07).
 * Turning it off must only be done after the module has been put into software
 * standby and the UART lines are idle (pinmap section 3 power-down order).
 */
int lgps_gnss_rail_set(bool on);

/* Pulse GPS_WAKE (P1.09 -> MAX-M10S EXTINT) for @p width_ms milliseconds. */
int lgps_gnss_wake_pulse(uint32_t width_ms);

/* Read SGM4056 /CHG (P0.04). true = charging current is flowing. */
int lgps_charger_charging(bool *charging);

#endif /* LIFEGPS_BOARD_IO_H_ */
