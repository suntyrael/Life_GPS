// SPDX-License-Identifier: MIT
package com.lifegps.app.ble

import com.lifegps.core.protocol.TrajectoryRecord
import org.junit.Assert.assertEquals
import org.junit.Test

class SetTimePayloadTest {

    @Test
    fun encodesPositiveTimezonePayloadMatchingFirmwareLayout() {
        val epoch2020S = 210_000_123L
        val subsecMs = 789
        val tzOffsetMin = 480 // UTC+8
        val nowMs = (TrajectoryRecord.LIFE_GPS_EPOCH_UNIX_S + epoch2020S) * 1000L + subsecMs

        val payload = BleDebugRepository.buildSetTimePayload(nowMs = nowMs, tzOffsetMin = tzOffsetMin)
        assertEquals(8, payload.size)
        assertEquals(epoch2020S, BleFrame.u32(payload, 0))
        assertEquals(subsecMs, BleFrame.u16(payload, 4))
        assertEquals(tzOffsetMin, BleFrame.i16(payload, 6))
        assertEquals(0xE0, BleFrame.u8(payload, 6))
        assertEquals(0x01, BleFrame.u8(payload, 7))
    }

    @Test
    fun encodesNegativeAndZeroTimezonesAsSignedLittleEndianI16() {
        val epoch2020S = 123_456_789L
        val nowMs = (TrajectoryRecord.LIFE_GPS_EPOCH_UNIX_S + epoch2020S) * 1000L

        val negPayload = BleDebugRepository.buildSetTimePayload(nowMs = nowMs, tzOffsetMin = -300) // UTC-5
        assertEquals(8, negPayload.size)
        assertEquals(epoch2020S, BleFrame.u32(negPayload, 0))
        assertEquals(0, BleFrame.u16(negPayload, 4))
        assertEquals(-300, BleFrame.i16(negPayload, 6))
        assertEquals(0xD4, BleFrame.u8(negPayload, 6))
        assertEquals(0xFE, BleFrame.u8(negPayload, 7))

        val halfHourNeg = BleDebugRepository.buildSetTimePayload(nowMs = nowMs + 999L, tzOffsetMin = -570) // UTC-9:30
        assertEquals(999, BleFrame.u16(halfHourNeg, 4))
        assertEquals(-570, BleFrame.i16(halfHourNeg, 6))

        val utcPayload = BleDebugRepository.buildSetTimePayload(nowMs = nowMs + 42L, tzOffsetMin = 0)
        assertEquals(42, BleFrame.u16(utcPayload, 4))
        assertEquals(0, BleFrame.i16(utcPayload, 6))
    }

    @Test
    fun clampsPre2020TimestampToZeroEpoch() {
        val pre2020Ms = (TrajectoryRecord.LIFE_GPS_EPOCH_UNIX_S - 100L) * 1000L + 250L
        val payload = BleDebugRepository.buildSetTimePayload(nowMs = pre2020Ms, tzOffsetMin = 330)
        assertEquals(8, payload.size)
        assertEquals(0L, BleFrame.u32(payload, 0))
        assertEquals(250, BleFrame.u16(payload, 4))
        assertEquals(330, BleFrame.i16(payload, 6))
    }
}
