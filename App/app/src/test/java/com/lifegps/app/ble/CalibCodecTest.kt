package com.lifegps.app.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM tests for the calibration commands (MAG_CAL 0x41 / ACCEL_CAL 0x43).
 *
 * These pin the byte layout the firmware has to implement. `CalibCodec` is the
 * only place in the App that knows the offsets, so a wrong field here fails
 * `:app:testDevDebugUnitTest` instead of showing a plausible-looking wrong number
 * on the phone.
 */
class CalibCodecTest {

    private fun writeU16(dst: ByteArray, offset: Int, value: Int) {
        dst[offset] = (value and 0xFF).toByte()
        dst[offset + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    private fun writeI16(dst: ByteArray, offset: Int, value: Int) = writeU16(dst, offset, value)

    private fun writeU32(dst: ByteArray, offset: Int, value: Long) {
        for (index in 0 until 4) {
            dst[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    private fun writeI32(dst: ByteArray, offset: Int, value: Int) =
        writeU32(dst, offset, value.toLong() and 0xFFFFFFFFL)

    // --- requests -------------------------------------------------------------

    @Test
    fun magRequestIsOneActionByte() {
        assertEquals(CalibCodec.MAG_REQ_SIZE, CalibCodec.encodeMagCal(CalibAction.START).size)
        assertEquals(listOf<Byte>(0x00), CalibCodec.encodeMagCal(CalibAction.START).toList())
        assertEquals(listOf<Byte>(0x01), CalibCodec.encodeMagCal(CalibAction.FINISH_SAVE).toList())
        assertEquals(listOf<Byte>(0x02), CalibCodec.encodeMagCal(CalibAction.CLEAR).toList())
        assertEquals(listOf<Byte>(0x03), CalibCodec.encodeMagCal(CalibAction.QUERY).toList())
        assertEquals(listOf<Byte>(0x04), CalibCodec.encodeMagCal(CalibAction.CANCEL).toList())
    }

    @Test
    fun accelRequestIsActionPlusFace() {
        val request = CalibCodec.encodeAccelCal(CalibAction.START, 3)
        assertEquals(CalibCodec.ACCEL_REQ_SIZE, request.size)
        assertEquals(listOf<Byte>(0x00, 0x03), request.toList())
        // The position byte is only read for START; the other actions carry the
        // default (POSITION_AUTO) because the device already knows the pose.
        assertEquals(
            listOf<Byte>(0x01, 0xFF.toByte()),
            CalibCodec.encodeAccelCal(CalibAction.FINISH_SAVE).toList(),
        )
        assertEquals(
            listOf<Byte>(0x00, 0x06),
            CalibCodec.encodeAccelCal(CalibAction.START, CalibCodec.POSITION_QUICK).toList(),
        )
    }

    @Test
    fun autoPositionIsTheDefaultAndEncodesAsFF() {
        // The App normally sends "identify the pose yourself": the user places the
        // device and never picks a face.
        assertEquals(
            listOf<Byte>(0x00, 0xFF.toByte()),
            CalibCodec.encodeAccelCal(CalibAction.START).toList(),
        )
        assertEquals(
            0xFF,
            CalibCodec.encodeAccelCal(CalibAction.START)[1].toInt() and 0xFF,
        )
    }

    @Test(expected = IllegalArgumentException::class)
    fun accelRequestRejectsAnOutOfRangeFace() {
        CalibCodec.encodeAccelCal(CalibAction.START, 7)
    }

    // --- MAG_CAL response -----------------------------------------------------

    private fun magBody(): ByteArray {
        val body = ByteArray(CalibCodec.MAG_RSP_SIZE)
        writeU16(body, 0, 300)              // progress_permille
        body[2] = 128.toByte()              // sample_count
        body[3] = 100                       // coverage x
        body[4] = 90                        // coverage y
        body[5] = 85                        // coverage z
        writeI16(body, 6, -123)             // bias x
        writeI16(body, 8, 45)               // bias y
        writeI16(body, 10, 7)               // bias z
        writeI32(body, 12, -12_345)         // scale x ppm
        writeI32(body, 16, 2_500)           // scale y ppm
        writeI32(body, 20, 0)               // scale z ppm
        body[24] = CalibStateCode.RESULT_PENDING.toByte()
        body[25] = CalibFlagBits.QUICK_SINGLE_FACE.toByte()
        writeU32(body, 26, 1_765_000_000L)  // last_calib_unix_s
        return body
    }

    @Test
    fun magResponseDecodesEveryField() {
        val response = CalibCodec.decodeMagCal(magBody())
        requireNotNull(response)
        assertEquals(300, response.progressPermille)
        assertEquals(128, response.sampleCount)
        assertEquals(listOf(100, 90, 85), response.coveragePct)
        assertEquals(listOf(-123, 45, 7), response.biasLsb)
        assertEquals(listOf(-12_345, 2_500, 0), response.scalePpm)
        assertEquals(CalibStateCode.RESULT_PENDING, response.state)
        assertEquals(1_765_000_000L, response.lastCalibUnixS)
        assertEquals(85, response.minCoveragePct)
        assertFalse(response.fromStore)
        assertFalse(response.usingDefaults)
        assertFalse(response.saveFailed)
    }

    @Test
    fun magFlagsAreReadFromTheDocumentedBits() {
        val body = magBody()
        body[25] = (CalibFlagBits.FROM_STORE or CalibFlagBits.SAVE_FAILED).toByte()
        val response = CalibCodec.decodeMagCal(body)
        requireNotNull(response)
        assertTrue(response.fromStore)
        assertTrue(response.saveFailed)
        assertFalse(response.usingDefaults)
    }

    @Test
    fun magOnlyRecordDoesNotCalibrateTheAccelerometer() {
        /* Both responses carry the same record flags: a magnetometer-only record.
         * Before the per-sensor bits, this made the IMU row read "已校准" too.
         */
        val flags = CalibFlagBits.FROM_STORE or CalibFlagBits.MAG_VALID
        val mag = CalibCodec.decodeMagCal(magBody().also { it[25] = flags.toByte() })
        val accel = CalibCodec.decodeAccelCal(accelBody().also { it[43] = flags.toByte() })
        requireNotNull(mag)
        requireNotNull(accel)

        assertTrue(mag.fromStore)
        assertTrue(mag.calibrated)
        assertTrue(accel.fromStore)
        assertFalse(accel.calibrated)
    }

    @Test
    fun imuOnlyRecordDoesNotCalibrateTheMagnetometer() {
        val flags = CalibFlagBits.FROM_STORE or CalibFlagBits.IMU_VALID
        val mag = CalibCodec.decodeMagCal(magBody().also { it[25] = flags.toByte() })
        val accel = CalibCodec.decodeAccelCal(accelBody().also { it[43] = flags.toByte() })
        requireNotNull(mag)
        requireNotNull(accel)

        assertFalse(mag.calibrated)
        assertTrue(accel.calibrated)
    }

    @Test
    fun aRecordWithoutSensorBitsFallsBackToFromStore() {
        // 0.4.0 and earlier set bit 0 only; both rows keep the old behaviour.
        val flags = CalibFlagBits.FROM_STORE
        val mag = CalibCodec.decodeMagCal(magBody().also { it[25] = flags.toByte() })
        val accel = CalibCodec.decodeAccelCal(accelBody().also { it[43] = flags.toByte() })
        requireNotNull(mag)
        requireNotNull(accel)

        assertTrue(mag.calibrated)
        assertTrue(accel.calibrated)
    }

    @Test
    fun truncatedMagBodyIsRejected() {
        val short = magBody().copyOf(CalibCodec.MAG_RSP_SIZE - 1)
        assertNull(CalibCodec.decodeMagCal(short))
    }

    // --- ACCEL_CAL response ---------------------------------------------------

    private fun accelBody(): ByteArray {
        val body = ByteArray(CalibCodec.ACCEL_RSP_SIZE)
        writeU16(body, 0, 1_000)           // progress_permille
        body[2] = 0x2A                      // face_mask: faces 1, 3, 5 (bits 1,3,5)
        body[3] = AccelFaceStateCode.FACE_DONE.toByte()
        body[4] = 82                        // rest_pct
        body[5] = 4                         // detected_face: +Z axis up
        writeI32(body, 6, -24_600)          // bias x µg
        writeI32(body, 10, 8_000)           // bias y µg
        writeI32(body, 14, -1_000)          // bias z µg
        writeI32(body, 18, -23_400)         // scale x ppm
        writeI32(body, 22, 500)             // scale y ppm
        writeI32(body, 26, -100)            // scale z ppm
        writeI32(body, 30, -350)            // gyro bias x mdps
        writeI32(body, 34, 120)             // gyro bias y mdps
        writeI32(body, 38, 0)               // gyro bias z mdps
        body[42] = CalibStateCode.IDLE.toByte()
        body[43] = CalibFlagBits.FROM_STORE.toByte()
        writeU32(body, 44, 1_765_000_001L)
        return body
    }

    @Test
    fun accelResponseDecodesEveryField() {
        val response = CalibCodec.decodeAccelCal(accelBody())
        requireNotNull(response)
        assertEquals(1_000, response.progressPermille)
        assertEquals(0x2A, response.faceMask)
        assertEquals(AccelFaceStateCode.FACE_DONE, response.faceState)
        assertEquals(82, response.restPct)
        assertEquals(4, response.detectedFace)
        assertTrue(response.hasDetectedFace)
        assertEquals(listOf(-24_600, 8_000, -1_000), response.biasUg)
        assertEquals(listOf(-23_400, 500, -100), response.scalePpm)
        assertEquals(listOf(-350, 120, 0), response.gyroBiasMdps)
        assertEquals(CalibStateCode.IDLE, response.state)
        assertEquals(1_765_000_001L, response.lastCalibUnixS)
        assertTrue(response.fromStore)
        assertFalse(response.quickMode)
    }

    @Test
    fun faceMaskBitsMatchTheFaceIndices() {
        val response = CalibCodec.decodeAccelCal(accelBody())
        requireNotNull(response)
        assertFalse(response.hasFace(0))
        assertTrue(response.hasFace(1))
        assertFalse(response.hasFace(2))
        assertTrue(response.hasFace(3))
        assertFalse(response.hasFace(4))
        assertTrue(response.hasFace(5))
        assertFalse(response.hasFace(CalibCodec.POSITION_QUICK))
        assertFalse(response.allFaces)
    }

    @Test
    fun truncatedAccelBodyIsRejected() {
        val short = accelBody().copyOf(CalibCodec.ACCEL_RSP_SIZE - 1)
        assertNull(CalibCodec.decodeAccelCal(short))
    }

    @Test
    fun undetectedFaceIsReportedAsFaceNone() {
        val body = accelBody()
        body[5] = CalibCodec.FACE_NONE.toByte()
        val response = CalibCodec.decodeAccelCal(body)
        requireNotNull(response)
        assertFalse(response.hasDetectedFace)
        assertEquals(CalibCodec.FACE_NONE, response.detectedFace)
    }
}
