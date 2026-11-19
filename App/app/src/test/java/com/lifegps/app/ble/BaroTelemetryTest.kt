package com.lifegps.app.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM tests for the barometer path: the `0x04` BARO telemetry payload and the
 * `BARO_BASELINE` (`0x42`) request/response codec.
 *
 * The byte layout is frozen on the firmware side (`LGPS_BLE_TELEM_BARO_PAYLOAD`,
 * `LGPS_BLE_BARO_BASELINE_REQ_SIZE` / `_RSP_SIZE`), so these goldens pin the same
 * numbers `BleFrame.BaroTelemetryOffset` and `CalibCodec` use. A one-sided edit has
 * to fail one of the two suites instead of failing on a phone at night.
 */
class BaroTelemetryTest {

    private fun writeU16(dst: ByteArray, offset: Int, value: Int) {
        dst[offset] = (value and 0xFF).toByte()
        dst[offset + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    private fun writeU32(dst: ByteArray, offset: Int, value: Long) {
        for (index in 0 until 4) {
            dst[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    // --- telemetry offsets ----------------------------------------------------

    @Test
    fun baroOffsetsMatchTheFrozenContract() {
        assertEquals(0, BaroTelemetryOffset.PRESS_PA)
        assertEquals(4, BaroTelemetryOffset.REL_ALT_MM)
        assertEquals(8, BaroTelemetryOffset.TEMP_CC)
        assertEquals(10, BaroTelemetryOffset.FLAGS)
        assertEquals(11, BaroTelemetryOffset.RESERVED)
        assertEquals(12, BaroTelemetryOffset.BASELINE_PA_Q4)
        assertEquals(16, BaroTelemetryOffset.SIZE)

        assertEquals(0x01, BaroTelemetryOffset.FLAG_PRESS_VALID)
        assertEquals(0x02, BaroTelemetryOffset.FLAG_TEMP_VALID)
        assertEquals(0x04, BaroTelemetryOffset.FLAG_BASELINE_VALID)
        assertEquals(0x08, BaroTelemetryOffset.FLAG_BASELINE_AUTO)
    }

    /** 1013.25 hPa, -1000 mm relative altitude, -25.00 °C, 1013.25 hPa baseline. */
    private fun baroPayload(): ByteArray {
        val payload = ByteArray(BaroTelemetryOffset.SIZE)
        writeU32(payload, BaroTelemetryOffset.PRESS_PA, 101_325L)
        writeU32(payload, BaroTelemetryOffset.REL_ALT_MM, 0xFFFFFC18L) // -1000
        writeU16(payload, BaroTelemetryOffset.TEMP_CC, 0xF63C)         // -2500
        payload[BaroTelemetryOffset.FLAGS] = 0x0F
        payload[BaroTelemetryOffset.RESERVED] = 0
        writeU32(payload, BaroTelemetryOffset.BASELINE_PA_Q4, 1_621_200L) // 101325 Pa × 16
        return payload
    }

    // --- telemetry parser -----------------------------------------------------

    @Test
    fun parseBaroTelemetryDecodesEveryField() {
        val payload = baroPayload()

        // Byte-for-byte golden: little endian u32/i32/i16 at 0/4/8, flags at 10,
        // reserved at 11, baseline_pa_q4 at 12.
        // press_pa = 101325 = 0x00018BCD, little endian.
        assertEquals(0xCD, payload[0].toInt() and 0xFF)
        assertEquals(0x8B, payload[1].toInt() and 0xFF)
        assertEquals(0x01, payload[2].toInt() and 0xFF)
        assertEquals(0x00, payload[3].toInt() and 0xFF)
        assertEquals(0x18, payload[4].toInt() and 0xFF)
        assertEquals(0xFC, payload[5].toInt() and 0xFF)
        assertEquals(0xFF, payload[6].toInt() and 0xFF)
        assertEquals(0xFF, payload[7].toInt() and 0xFF)
        assertEquals(0x3C, payload[8].toInt() and 0xFF)
        assertEquals(0xF6, payload[9].toInt() and 0xFF)
        assertEquals(0x0F, payload[10].toInt() and 0xFF)
        assertEquals(0x00, payload[11].toInt() and 0xFF)
        assertEquals(0xD0, payload[12].toInt() and 0xFF)
        assertEquals(0xBC, payload[13].toInt() and 0xFF)
        assertEquals(0x18, payload[14].toInt() and 0xFF)
        assertEquals(0x00, payload[15].toInt() and 0xFF)

        val baro = BleFrame.parseBaroTelemetry(payload)
        assertNotNull(baro)
        baro!!
        assertEquals(101_325L, baro.pressPa)
        assertEquals(-1000, baro.relAltMm)
        assertEquals(-2500, baro.tempCc)
        assertEquals(0x0F, baro.flags)
        assertEquals(1_621_200L, baro.baselinePaQ4)

        assertEquals(1013.25f, baro.pressureHpa, 0.01f)
        assertEquals(-25.0f, baro.temperatureC, 0.01f)
        assertEquals(-1.0f, baro.relAltM, 1e-6f)
        assertEquals(1013.25f, baro.baselineHpa, 0.01f)

        assertTrue(baro.pressValid)
        assertTrue(baro.tempValid)
        assertTrue(baro.baselineValid)
        assertTrue(baro.baselineAuto)
    }

    @Test
    fun parseBaroTelemetryRejectsShortPayloads() {
        val payload = baroPayload()
        assertNull(BleFrame.parseBaroTelemetry(ByteArray(0)))
        assertNull(BleFrame.parseBaroTelemetry(payload.copyOf(11)))
        assertNull(BleFrame.parseBaroTelemetry(payload.copyOf(BaroTelemetryOffset.SIZE - 1)))
        assertNotNull(BleFrame.parseBaroTelemetry(payload))
        // A longer payload is not a truncation: the first 16 bytes stay the contract.
        assertNotNull(BleFrame.parseBaroTelemetry(payload + ByteArray(4)))
    }

    @Test
    fun baroFlagsDecodeBitByBit() {
        val payload = baroPayload()

        payload[BaroTelemetryOffset.FLAGS] = 0x00
        val none = BleFrame.parseBaroTelemetry(payload)
        assertNotNull(none)
        none!!
        assertFalse(none.pressValid)
        assertFalse(none.tempValid)
        assertFalse(none.baselineValid)
        assertFalse(none.baselineAuto)

        payload[BaroTelemetryOffset.FLAGS] = BaroTelemetryOffset.FLAG_PRESS_VALID.toByte()
        val pressOnly = BleFrame.parseBaroTelemetry(payload)
        assertNotNull(pressOnly)
        pressOnly!!
        assertTrue(pressOnly.pressValid)
        assertFalse(pressOnly.tempValid)
        assertFalse(pressOnly.baselineValid)

        payload[BaroTelemetryOffset.FLAGS] = BaroTelemetryOffset.FLAG_TEMP_VALID.toByte()
        val tempOnly = BleFrame.parseBaroTelemetry(payload)
        assertNotNull(tempOnly)
        assertTrue(tempOnly!!.tempValid)
        assertFalse(tempOnly.pressValid)

        payload[BaroTelemetryOffset.FLAGS] =
            (BaroTelemetryOffset.FLAG_BASELINE_VALID or BaroTelemetryOffset.FLAG_BASELINE_AUTO)
                .toByte()
        val autoBaseline = BleFrame.parseBaroTelemetry(payload)
        assertNotNull(autoBaseline)
        assertTrue(autoBaseline!!.baselineValid)
        assertTrue(autoBaseline.baselineAuto)
        assertFalse(autoBaseline.pressValid)
    }

    @Test
    fun unsetBaselineIsZeroOnTheWire() {
        val payload = baroPayload()
        writeU32(payload, BaroTelemetryOffset.BASELINE_PA_Q4, 0L)
        payload[BaroTelemetryOffset.FLAGS] =
            (BaroTelemetryOffset.FLAG_PRESS_VALID or BaroTelemetryOffset.FLAG_TEMP_VALID).toByte()

        val baro = BleFrame.parseBaroTelemetry(payload)
        assertNotNull(baro)
        baro!!
        assertEquals(0L, baro.baselinePaQ4)
        assertEquals(0f, baro.baselineHpa, 0f)
        assertFalse(baro.baselineValid)
        assertFalse(baro.baselineAuto)
    }

    @Test
    fun baroPacketInsideTheTelemetryHeaderIsDemultiplexedById() {
        val payload = baroPayload()
        val raw = ByteArray(TelemetryOffset.PAYLOAD + payload.size)
        raw[TelemetryOffset.ID] = TelemetryId.BARO.toByte()
        raw[TelemetryOffset.VER] = BleFrame.VER.toByte()
        writeU16(raw, TelemetryOffset.SEQ, 0x4242)
        payload.copyInto(raw, TelemetryOffset.PAYLOAD)

        val packet = BleFrame.parseTelemetry(raw)
        assertNotNull(packet)
        packet!!
        assertEquals(TelemetryId.BARO, packet.id)
        assertEquals(0x4242, packet.seq)
        assertEquals(BaroTelemetryOffset.SIZE, packet.payload.size)

        val baro = BleFrame.parseBaroTelemetry(packet.payload)
        assertNotNull(baro)
        assertEquals(101_325L, baro!!.pressPa)
    }

    // --- BARO_BASELINE request ------------------------------------------------

    @Test
    fun baroBaselineRequestIsActionPlusPaQ4() {
        assertEquals(5, CalibCodec.BARO_REQ_SIZE)

        val take = CalibCodec.encodeBaroBaseline(BaroBaselineAction.TAKE_CURRENT)
        assertEquals(CalibCodec.BARO_REQ_SIZE, take.size)
        // action 0 and an all-zero payload: the firmware must not read the value.
        assertEquals(
            listOf<Byte>(0x00, 0x00, 0x00, 0x00, 0x00),
            take.toList(),
        )
        assertEquals(
            take.toList(),
            CalibCodec.encodeBaroBaseline(BaroBaselineAction.TAKE_CURRENT, 0L).toList(),
        )

        // 1013.25 hPa = 101325 Pa = 0x0018BCD0 counts of 1/16 Pa.
        val manual = CalibCodec.encodeBaroBaseline(BaroBaselineAction.MANUAL, 1_621_200L)
        assertEquals(
            listOf<Byte>(0x01, 0xD0.toByte(), 0xBC.toByte(), 0x18, 0x00),
            manual.toList(),
        )
    }

    @Test(expected = IllegalArgumentException::class)
    fun baroBaselineRequestRejectsANegativeValue() {
        CalibCodec.encodeBaroBaseline(BaroBaselineAction.MANUAL, -1L)
    }

    @Test
    fun hPaConversionUsesPaTimesSixteen() {
        assertEquals(1_621_200L, CalibCodec.hpaToPaQ4(1013.25f))
        assertEquals(1013.25f, CalibCodec.paQ4ToHpa(1_621_200L), 0.01f)
        assertEquals(CalibCodec.BARO_PA_MIN * CalibCodec.BARO_Q4, CalibCodec.BARO_PA_Q4_MIN)
        assertEquals(CalibCodec.BARO_PA_MAX * CalibCodec.BARO_Q4, CalibCodec.BARO_PA_Q4_MAX)
        // 30000..125000 Pa (BMP388 full-accuracy range) on the wire.
        assertEquals(480_000L, CalibCodec.BARO_PA_Q4_MIN)
        assertEquals(2_000_000L, CalibCodec.BARO_PA_Q4_MAX)
    }

    // --- BARO_BASELINE response -----------------------------------------------

    @Test
    fun baroBaselineResponseDecodesBaselineAndAltitude() {
        assertEquals(8, CalibCodec.BARO_RSP_SIZE)
        val body = ByteArray(CalibCodec.BARO_RSP_SIZE)
        writeU32(body, 0, 1_621_200L)
        writeU32(body, 4, 0xFFFFFA24L) // rel_alt_mm = -1500

        val response = CalibCodec.decodeBaroBaseline(body)
        assertNotNull(response)
        response!!
        assertEquals(1_621_200L, response.baselinePaQ4)
        assertEquals(-1500, response.relAltMm)
        assertEquals(1013.25f, response.baselineHpa, 0.01f)
        assertEquals(-1.5f, response.relAltM, 1e-6f)
        assertTrue(response.baselineValid)
    }

    @Test
    fun emptyBaselineResponseIsInvalid() {
        val body = ByteArray(CalibCodec.BARO_RSP_SIZE)
        val response = CalibCodec.decodeBaroBaseline(body)
        assertNotNull(response)
        response!!
        assertEquals(0L, response.baselinePaQ4)
        assertEquals(0, response.relAltMm)
        assertFalse(response.baselineValid)
    }

    @Test
    fun truncatedBaroBaselineResponseIsRejected() {
        assertNull(CalibCodec.decodeBaroBaseline(ByteArray(CalibCodec.BARO_RSP_SIZE - 1)))
        assertNull(CalibCodec.decodeBaroBaseline(ByteArray(0)))
    }
}
