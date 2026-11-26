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
 * The little-endian 128-bit UUID octet lists (LGPS_UUID_*_LE) are generated
 * directly from protocol/lifegps_protocol.yaml by tools/protocol-gen/gen_c.py.
 * This header is kept as a thin compatibility wrapper so callers that only
 * include <lifegps/ble_uuid.h> continue to work without duplicating wire bytes.
 */
#include <generated/lifegps_protocol.h>

#endif /* LIFEGPS_BLE_UUID_H_ */
