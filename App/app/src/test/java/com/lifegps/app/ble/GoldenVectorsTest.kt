package com.lifegps.app.ble

import com.lifegps.app.ble.generated.GeneratedProtocol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Cross-language contract tests reading the exact same golden vector binaries
 * under `protocol/vectors/` that `Firmware/tests/lifegps.protogen` validates in C.
 */
class GoldenVectorsTest {

    private fun readVector(name: String): ByteArray {
        val stream = javaClass.classLoader?.getResourceAsStream(name)
        assertNotNull("Missing golden vector resource: $name", stream)
        return stream!!.use { it.readBytes() }
    }

    private fun u16(buf: ByteArray, offset: Int): Int =
        (buf[offset].toInt() and 0xFF) or ((buf[offset + 1].toInt() and 0xFF) shl 8)

    private fun u32(buf: ByteArray, offset: Int): Long =
        (buf[offset].toLong() and 0xFFL) or
            ((buf[offset + 1].toLong() and 0xFFL) shl 8) or
            ((buf[offset + 2].toLong() and 0xFFL) shl 16) or
            ((buf[offset + 3].toLong() and 0xFFL) shl 24)

    @Test
    fun generatedProtocolConstantsMatchContract() {
        assertEquals(0xA5, GeneratedProtocol.CommandFrame.MAGIC)
        assertEquals(0x5A, GeneratedProtocol.DataFrame.MAGIC)
        assertEquals(32, GeneratedProtocol.TrajectoryRecord.SIZE)
        assertEquals(48, GeneratedProtocol.DayPackage.HEADER_SIZE)
        assertEquals(8, GeneratedProtocol.DayPackage.FOOTER_SIZE)
        assertEquals(8, GeneratedProtocol.DayPackage.HS_WINDOW_SZ2)
        assertEquals(4, GeneratedProtocol.DayPackage.HS_LOOKAHEAD_SZ2)
        assertEquals(BleUuids.SERVICE, GeneratedProtocol.Uuids.SERVICE)
        assertEquals(BleUuids.DATA, GeneratedProtocol.Uuids.DATA)
        assertEquals(BleUuids.CONFIG, GeneratedProtocol.Uuids.CONFIG)
        assertEquals(CapsBits.GNSS_DEBUG, GeneratedProtocol.CapsBits.GNSS_DEBUG)
        assertEquals(CapsBits.GNSS_CFG, GeneratedProtocol.CapsBits.GNSS_CFG)
    }

    @Test
    fun standardCrcReferenceVectorMatchesAllThreeAlgorithms() {
        val ref = readVector("crc_reference.bin")
        assertEquals("123456789", String(ref, Charsets.US_ASCII))
        assertEquals(0xF4, BleFrame.crc8(ref))
        assertEquals(0x29B1, BleFrame.crc16CcittFalse(ref))
        assertEquals(0xCBF43926L, BleFrame.crc32IsoHdlc(ref))
    }

    @Test
    fun commandCapsRequestAndResponseGoldenVectorsParseCleanly() {
        val req = readVector("cmd_get_caps_req.bin")
        assertTrue(req.contentEquals(BleFrame.encodeCommand(Opcode.GET_CAPS.code, 1, ByteArray(0))))

        val rsp = readVector("cmd_get_caps_rsp.bin")
        val frame = BleFrame.parseFrame(rsp)
        assertNotNull(frame)
        frame!!
        assertEquals(Opcode.GET_CAPS, frame.opcode)
        assertTrue(frame.isResponse)
        assertEquals(1, frame.seq)
        assertEquals(StatusCode.OK.code, frame.payload[0].toInt() and 0xFF)

        val caps = BleFrame.parseCaps(frame.payload.copyOfRange(1, frame.payload.size))
        assertNotNull(caps)
        caps!!
        assertEquals(247, caps.attMtu)
        assertEquals(235, caps.maxCmdPayload)
        assertEquals(0x00000FFFL, caps.caps)
        assertEquals(0x00000336L, caps.telemetryMask)
        assertEquals(116, caps.statusSize)
    }

    @Test
    fun legacySevenSatelliteGoldenVectorDecodesAsSevenLegacyRows() {
        val payload = readVector("telem_sat_legacy_7sats.bin")
        val sat = BleFrame.parseSatTelemetry(payload)
        assertNotNull(sat)
        sat!!
        assertEquals(7, sat.rows.size)
        assertEquals(10, sat.rows[0].svId)
        assertEquals(16, sat.rows[6].svId)
        assertEquals(-1, sat.rows[0].sigId)
        assertEquals(-1, sat.rows[0].azimuth)
    }

    @Test
    fun trajectoryAndDayPackageGoldenVectorsPassChecksums() {
        val trajBasic = readVector("trajectory_basic.bin")
        assertEquals(3 * GeneratedProtocol.TrajectoryRecord.SIZE, trajBasic.size)
        for (i in 0 until 3) {
            val base = i * GeneratedProtocol.TrajectoryRecord.SIZE
            val expectedCrc = BleFrame.crc8(trajBasic, base, 31)
            assertEquals(expectedCrc, trajBasic[base + 31].toInt() and 0xFF)
            assertEquals(100L + i, u32(trajBasic, base))
        }

        val dataChunk = readVector("data_chunk_trajectory.bin")
        assertEquals(GeneratedProtocol.DataFrame.MAGIC, dataChunk[0].toInt() and 0xFF)
        assertEquals(
            BleFrame.crc16CcittFalse(dataChunk, 0, dataChunk.size - 2),
            u16(dataChunk, dataChunk.size - 2),
        )

        val dayRaw = readVector("day_package_raw.bin")
        assertEquals(
            BleFrame.crc16CcittFalse(dayRaw, 0, GeneratedProtocol.DayPackage.HEADER_CRC16_LEN),
            u16(dayRaw, GeneratedProtocol.DayPackage.HEADER_OFF_HEADER_CRC16),
        )
        assertEquals(
            BleFrame.crc32IsoHdlc(trajBasic),
            u32(dayRaw, dayRaw.size - GeneratedProtocol.DayPackage.FOOTER_SIZE),
        )

        val dayHs = readVector("day_package_hs.bin")
        val trajWrap = readVector("trajectory_delta_wrap.bin")
        assertEquals(
            BleFrame.crc16CcittFalse(dayHs, 0, GeneratedProtocol.DayPackage.HEADER_CRC16_LEN),
            u16(dayHs, GeneratedProtocol.DayPackage.HEADER_OFF_HEADER_CRC16),
        )
        assertEquals(
            BleFrame.crc32IsoHdlc(trajWrap),
            u32(dayHs, dayHs.size - GeneratedProtocol.DayPackage.FOOTER_SIZE),
        )
    }
}
