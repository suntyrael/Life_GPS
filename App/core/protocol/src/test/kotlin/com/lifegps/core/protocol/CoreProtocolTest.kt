// SPDX-License-Identifier: MIT
package com.lifegps.core.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CoreProtocolTest {

    private fun readVector(name: String): ByteArray {
        val stream = javaClass.classLoader?.getResourceAsStream(name)
        assertNotNull("Missing golden vector resource: $name", stream)
        return stream!!.use { it.readBytes() }
    }

    @Test
    fun crcReferenceMatchesCheckValues() {
        val ref = readVector("crc_reference.bin")
        assertEquals(0xF4, ProtocolCrc.crc8(ref))
        assertEquals(0x29B1, ProtocolCrc.crc16CcittFalse(ref))
        assertEquals(0xCBF43926L, ProtocolCrc.crc32IsoHdlc(ref))
    }

    @Test
    fun trajectoryBasicRoundTrip() {
        val bytes = readVector("trajectory_basic.bin")
        val records = TrajectoryRecord.decodeStream(bytes)
        assertEquals(3, records.size)
        assertEquals(100L, records[0].seqId)
        assertEquals(312304160, records[0].latitudeE7)
        assertEquals(0, records[0].floorLevel)
        assertEquals(-338688200, records[1].latitudeE7)
        assertEquals(-12, records[1].altitudeM)
        assertEquals(-2, records[1].floorLevel)
        assertTrue(records[1].isPoi)
        assertEquals(5, records[2].floorLevel)

        val reencoded = ByteArray(bytes.size)
        records.forEachIndexed { idx, rec ->
            rec.encode().copyInto(reencoded, idx * GeneratedProtocol.TrajectoryRecord.SIZE)
        }
        assertArrayEquals(bytes, reencoded)
    }

    @Test
    fun dataFrameGoldenVectorDecodesAndRoundTrips() {
        val raw = readVector("data_chunk_trajectory.bin")
        val frame = DataFrame.decode(raw)
        assertNotNull(frame)
        frame!!
        assertEquals(0, frame.stream)
        assertTrue(frame.isFirst)
        assertEquals(42L, frame.transferId)
        assertEquals(252L, frame.cursor)
        assertEquals(7 * GeneratedProtocol.TrajectoryRecord.SIZE, frame.payload.size)
        val records = TrajectoryRecord.decodeStream(frame.payload)
        assertEquals(7, records.size)
        assertEquals(252L, records.first().seqId)
        assertArrayEquals(raw, DataFrame.encode(frame))
    }

    @Test
    fun dayPackageRawAndHeatshrinkMatchGoldenVectors() {
        val expectedRaw = readVector("trajectory_basic.bin")
        val rawPkg = DayPackageCodec.decode(readVector("day_package_raw.bin"))
        assertEquals(20261125L, rawPkg.header.dayId)
        assertEquals(GeneratedProtocol.DayPackage.CODEC_RAW, rawPkg.header.codecId)
        assertEquals(3, rawPkg.records.size)
        assertArrayEquals(expectedRaw, rawPkg.rawBytes)

        val expectedWrap = readVector("trajectory_delta_wrap.bin")
        val hsPkg = DayPackageCodec.decode(readVector("day_package_hs.bin"))
        assertEquals(20261126L, hsPkg.header.dayId)
        assertEquals(GeneratedProtocol.DayPackage.CODEC_BYTE_DELTA_HEATSHRINK, hsPkg.header.codecId)
        assertEquals(8, hsPkg.records.size)
        assertArrayEquals(expectedWrap, hsPkg.rawBytes)
    }
}
