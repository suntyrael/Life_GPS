/* SPDX-License-Identifier: MIT */
/*
 * Life_GPS firmware - 128-bit GATT UUIDs as little-endian octet lists.
 *
 * The UUID values are owned by protocol/lifegps_protocol.yaml and reach the
 * firmware as canonical strings in generated/lifegps_protocol.h. Bluetooth puts
 * a 128-bit UUID on the air least-significant byte first, so every UUID needs a
 * byte-reversed copy; writing that reversal by hand at four places is exactly
 * the kind of thing that silently ships a wrong service UUID.
 *
 * Therefore: the octet lists below are the only copy, the GATT table and the
 * unit test both consume them, and tests/lifegps.ble_frame re-derives the bytes
 * from the generated string and compares - a typo here fails twister instead of
 * failing on a phone.
 *
 * Deliberately dependency free (no Zephyr headers) so the test can run on
 * native_sim without a Bluetooth stack.
 */

#ifndef LIFEGPS_BLE_UUID_H_
#define LIFEGPS_BLE_UUID_H_

/*
 * Canonical form -> octets (bit 127 first) -> wire order (bit 0 first):
 *   4C475053-0001-4C47-5053-000000000001
 *   {4C 47 50 53} {00 01} {4C 47} {50 53} {00 00 00 00 00 01}
 *   reversed: 01 00 00 00 00 00 53 50 47 4C 01 00 53 50 47 4C
 * "LGPS" is ASCII 4C 47 50 53, which is why the tail reads 53 50 47 4C when
 * reversed - that is the expected result, not a typo.
 */
#define LGPS_UUID_SERVICE_LE                        \
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x50, \
	0x47, 0x4C, 0x01, 0x00, 0x53, 0x50, 0x47, 0x4C

#define LGPS_UUID_STATUS_LE                         \
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x50, \
	0x47, 0x4C, 0x02, 0x00, 0x53, 0x50, 0x47, 0x4C

#define LGPS_UUID_CONTROL_LE                        \
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x50, \
	0x47, 0x4C, 0x03, 0x00, 0x53, 0x50, 0x47, 0x4C

#define LGPS_UUID_LOG_LE                            \
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x50, \
	0x47, 0x4C, 0x06, 0x00, 0x53, 0x50, 0x47, 0x4C

#endif /* LIFEGPS_BLE_UUID_H_ */
