package com.lifegps.app.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM tests for the v1 wire codec.
 *
 * These run in CI (`:app:testDevDebugUnitTest`) without an emulator or a device,
 * and they are the only place where the App's byte offsets are checked against
 * `Doc/Software/BLE_Wire_v1.md`. The firmware side pins the same numbers in
 * `Firmware/tests/lifegps.ble_frame`, so a one-sided edit fails one of the two
 * suites instead of failing on a phone at night.
 */
class BleFrameTest {

    private fun writeU16(dst: ByteArray, offset: Int, value: Int) {
        dst[offset] = (value and 0xFF).toByte()
        dst[offset + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    private fun writeU32(dst: ByteArray, offset: Int, value: Long) {
        for (index in 0 until 4) {
            dst[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    // --- CRC ------------------------------------------------------------------

    @Test
    fun crc16CcittFalseMatchesTheReferenceVector() {
        // "123456789" -> 0x29B1 is the published check value for
        // CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection).
        val data = "123456789".toByteArray(Charsets.US_ASCII)
        assertEquals(0x29B1, BleFrame.crc16CcittFalse(data))
    }

    @Test
    fun crc16OfEmptyInputIsTheInitValue() {
        assertEquals(0xFFFF, BleFrame.crc16CcittFalse(ByteArray(0)))
    }

    // --- command frames -------------------------------------------------------

    @Test
    fun encodeCommandWritesTheDocumentedHeader() {
        val payload = byteArrayOf(0xDE.toByte(), 0xAD.toByte(), 0xBE.toByte(), 0xEF.toByte())
        val frame = BleFrame.encodeCommand(Opcode.POI_MARK.code, 0x1234, payload)

        assertEquals(BleFrame.HEADER_SIZE + payload.size + BleFrame.TRAILER_SIZE, frame.size)
        assertEquals(0xA5, BleFrame.u8(frame, 0))
        assertEquals(0x01, BleFrame.u8(frame, 1))
        assertEquals(0x12, BleFrame.u8(frame, 2))
        assertEquals(0x00, BleFrame.u8(frame, 3))
        assertEquals(0x1234, BleFrame.u16(frame, 4))
        assertEquals(payload.size, BleFrame.u16(frame, 6))

        val stored = BleFrame.u16(frame, frame.size - 2)
        assertEquals(BleFrame.crc16CcittFalse(frame, 0, frame.size - 2), stored)
    }

    @Test
    fun parseFrameRoundTripsAndRejectsDamage() {
        val payload = byteArrayOf(0x01)
        val frame = BleFrame.encodeCommand(Opcode.LOG_SUBSCRIBE.code, 7, payload)
        val parsed = BleFrame.parseFrame(frame)

        assertNotNull(parsed)
        assertEquals(Opcode.LOG_SUBSCRIBE.code, parsed!!.opcodeCode)
        assertEquals(Opcode.LOG_SUBSCRIBE, parsed.opcode)
        assertEquals(7, parsed.seq)
        assertFalse(parsed.isResponse)
        assertEquals(1, parsed.payload.size)
        assertEquals(0x01, BleFrame.u8(parsed.payload, 0))

        // A single flipped CRC byte must invalidate the frame.
        val badCrc = frame.copyOf()
        badCrc[badCrc.size - 1] = (badCrc[badCrc.size - 1].toInt() xor 0xFF).toByte()
        assertNull(BleFrame.parseFrame(badCrc))

        // Truncated frame: the length field no longer matches the buffer.
        assertNull(BleFrame.parseFrame(frame.copyOf(frame.size - 1)))

        // Empty payload is legal.
        val empty = BleFrame.encodeCommand(Opcode.GET_CAPS.code, 1, ByteArray(0))
        val parsedEmpty = BleFrame.parseFrame(empty)
        assertNotNull(parsedEmpty)
        assertEquals(0, parsedEmpty!!.payload.size)

        // Payload above the protocol maximum is not encodable.
        val tooBig = ByteArray(BleFrame.MAX_PAYLOAD + 1)
        val threw = runCatching { BleFrame.encodeCommand(Opcode.GET_CAPS.code, 1, tooBig) }
        assertTrue(threw.isFailure)
    }

    @Test
    fun responseFlagIsRecognised() {
        val frame = BleFrame.encodeCommand(Opcode.GET_STATUS.code, 9, byteArrayOf(0x00))
        frame[3] = BleFrame.FLAG_RESPONSE.toByte()
        // The CRC no longer matches after patching, but the flag test only needs
        // the parser to see a well-formed frame.
        val crc = BleFrame.crc16CcittFalse(frame, 0, frame.size - 2)
        writeU16(frame, frame.size - 2, crc)

        val parsed = BleFrame.parseFrame(frame)
        assertNotNull(parsed)
        assertTrue(parsed!!.isResponse)
    }

    // --- status ---------------------------------------------------------------

    @Test
    fun statusOffsetsMatchTheWireDocument() {
        // The numbers below are the ones printed in BLE_Wire_v1.md section 4.
        assertEquals(0, StatusOffset.VER)
        assertEquals(1, StatusOffset.FLAGS)
        assertEquals(2, StatusOffset.GNSS_STATE)
        assertEquals(3, StatusOffset.FIX_TYPE)
        assertEquals(4, StatusOffset.SATELLITES)
        assertEquals(5, StatusOffset.KEY_LEVELS)
        assertEquals(6, StatusOffset.SOC_PCT)
        assertEquals(8, StatusOffset.PDOP_1E2)
        assertEquals(10, StatusOffset.SPEED_CMS)
        assertEquals(12, StatusOffset.HEADING_DDEG)
        assertEquals(16, StatusOffset.LAT_E7)
        assertEquals(20, StatusOffset.LON_E7)
        assertEquals(24, StatusOffset.HMSL_MM)
        assertEquals(28, StatusOffset.BAT_MV)
        assertEquals(32, StatusOffset.UTC_EPOCH_S)
        assertEquals(36, StatusOffset.UPTIME_MS)
        assertEquals(40, StatusOffset.GNSS_AGE_MS)
        assertEquals(44, StatusOffset.STEPS)
        assertEquals(48, StatusOffset.ACC_LSB)
        assertEquals(54, StatusOffset.GYRO_LSB)
        assertEquals(60, StatusOffset.MAG_LSB)
        assertEquals(66, StatusOffset.BARO_PRESS_RAW)
        assertEquals(70, StatusOffset.BARO_TEMP_RAW)
        assertEquals(74, StatusOffset.RING_SECTOR_SEQ)
        assertEquals(78, StatusOffset.RING_RECORDS)
        assertEquals(82, StatusOffset.GRAVITY_MG)
        assertEquals(88, StatusOffset.LINEAR_MG)
        assertEquals(94, StatusOffset.IMU_TEMP_CC)
        assertEquals(96, StatusOffset.MAG_TEMP_CC)
        assertEquals(98, StatusOffset.SENSOR_FLAGS)
        assertEquals(82, StatusOffset.BASE_SIZE)
        assertEquals(100, StatusOffset.SIZE)
    }

    @Test
    fun parseStatusDecodesEveryFieldAtItsOffset() {
        val raw = ByteArray(StatusOffset.SIZE)
        raw[StatusOffset.VER] = 1
        raw[StatusOffset.FLAGS] = (StatusFlags.GNSS_FIX_VALID or StatusFlags.CHARGING or
            StatusFlags.ZUPT).toByte()
        raw[StatusOffset.GNSS_STATE] = 2
        raw[StatusOffset.FIX_TYPE] = 3
        raw[StatusOffset.SATELLITES] = 11
        raw[StatusOffset.KEY_LEVELS] = 1
        raw[StatusOffset.SOC_PCT] = 0xFF.toByte()
        writeU16(raw, StatusOffset.PDOP_1E2, 142)
        writeU16(raw, StatusOffset.SPEED_CMS, 564)
        writeU16(raw, StatusOffset.HEADING_DDEG, 1027)
        writeU32(raw, StatusOffset.LAT_E7, 0x0A0B0C0DL)
        writeU32(raw, StatusOffset.LON_E7, 0xFEFDFCFCL) // -0x01020304
        writeU32(raw, StatusOffset.HMSL_MM, 0xFFFFFB2EL) // -1234
        writeU32(raw, StatusOffset.BAT_MV, 4012)
        writeU32(raw, StatusOffset.UTC_EPOCH_S, 0x11223344L)
        writeU32(raw, StatusOffset.UPTIME_MS, 0x55667788L)
        writeU32(raw, StatusOffset.GNSS_AGE_MS, 100)
        writeU32(raw, StatusOffset.STEPS, 1000)
        writeU16(raw, StatusOffset.ACC_LSB, 258)
        writeU16(raw, StatusOffset.ACC_LSB + 2, 0xFFFE) // -2
        writeU16(raw, StatusOffset.ACC_LSB + 4, 0x7FFF)
        writeU16(raw, StatusOffset.GYRO_LSB, 0xFFFC) // -4
        writeU16(raw, StatusOffset.MAG_LSB, 0x0506)
        writeU32(raw, StatusOffset.BARO_PRESS_RAW, 0x00ABCDEFL)
        writeU32(raw, StatusOffset.BARO_TEMP_RAW, 0x000FFFFFL)
        writeU32(raw, StatusOffset.RING_SECTOR_SEQ, 7)
        writeU32(raw, StatusOffset.RING_RECORDS, 0x1234)
        writeU16(raw, StatusOffset.GRAVITY_MG, 0)
        writeU16(raw, StatusOffset.GRAVITY_MG + 2, 0)
        writeU16(raw, StatusOffset.GRAVITY_MG + 4, -1000)
        writeU16(raw, StatusOffset.LINEAR_MG, 25)
        writeU16(raw, StatusOffset.LINEAR_MG + 2, -25)
        writeU16(raw, StatusOffset.LINEAR_MG + 4, 0)
        writeU16(raw, StatusOffset.IMU_TEMP_CC, 3456)
        writeU16(raw, StatusOffset.MAG_TEMP_CC, -500)
        raw[StatusOffset.SENSOR_FLAGS] = 0x0F

        val status = BleFrame.parseStatus(raw)
        assertNotNull(status)
        status!!

        assertEquals(1, status.ver)
        assertTrue(status.gnssFixValid)
        assertTrue(status.charging)
        assertTrue(status.zupt)
        assertFalse(status.imuValid)
        assertEquals(2, status.gnssState)
        assertEquals("FIXED", BleFrame.gnssStateName(status.gnssState))
        assertEquals(3, status.fixType)
        assertEquals(11, status.satellites)
        assertEquals(1, status.keyLevels)
        assertEquals(0xFF, status.socPct)
        assertEquals(142, status.pdop1e2)
        assertEquals(1.42, status.pdop1e2 / 100.0, 1e-9)
        assertEquals(564, status.speedCms)
        assertEquals(1027, status.headingDdeg)
        assertEquals(0x0A0B0C0D, status.latE7)
        assertEquals(-0x01020304, status.lonE7)
        assertEquals(-1234, status.hmslMm)
        assertEquals(4012, status.batMv)
        assertEquals(0x11223344L, status.utcEpochS)
        assertEquals(0x55667788L, status.uptimeMs)
        assertEquals(100L, status.gnssAgeMs)
        assertEquals(1000L, status.steps)
        assertEquals(listOf(258, -2, 0x7FFF), status.accLsb)
        assertEquals(-4, status.gyroLsb[0])
        assertEquals(0x0506, status.magLsb[0])
        assertEquals(0x00ABCDEFL, status.baroPressRaw)
        assertEquals(0x000FFFFFL, status.baroTempRaw)
        assertEquals(7L, status.ringSectorSeq)
        assertEquals(0x1234L, status.ringRecords)
        assertEquals(listOf(0, 0, -1000), status.gravityMg)
        assertEquals(listOf(25, -25, 0), status.linearMg)
        assertEquals(3456, status.imuTempCc)
        assertEquals(-500, status.magTempCc)
        assertEquals(0x0F, status.sensorFlags)
        assertTrue(status.hasMotionFields)
        assertTrue(status.hasTempFields)
        assertTrue(status.gravityValid)
        assertTrue(status.imuTempValid)
        assertTrue(status.magTempValid)
        assertTrue(status.baroTempValid)

        // Old firmware stops after ring_records: the extension stays zero/invalid.
        val baseOnly = BleFrame.parseStatus(raw.copyOf(StatusOffset.BASE_SIZE))
        assertNotNull(baseOnly)
        baseOnly!!
        assertFalse(baseOnly.hasMotionFields)
        assertFalse(baseOnly.hasTempFields)
        assertEquals(listOf(0, 0, 0), baseOnly.gravityMg)
        assertEquals(listOf(0, 0, 0), baseOnly.linearMg)
        assertEquals(0, baseOnly.imuTempCc)
        assertEquals(0, baseOnly.magTempCc)
        assertEquals(0, baseOnly.sensorFlags)
        assertFalse(baseOnly.gravityValid)

        // Anything shorter than the base contract is "missing", never partially parsed.
        assertNull(BleFrame.parseStatus(raw.copyOf(StatusOffset.BASE_SIZE - 1)))
    }

    // --- caps / flash info ----------------------------------------------------

    @Test
    fun parseCapsReadsTheDocumentedFields() {
        val body = ByteArray(CapsOffset.SIZE)
        body[CapsOffset.VER] = 1
        body[CapsOffset.PROTO_MAJOR] = 0
        body[CapsOffset.PROTO_MINOR] = 1
        body[CapsOffset.PROTO_PATCH] = 0
        body[CapsOffset.FW_MAJOR] = 0
        body[CapsOffset.FW_MINOR] = 2
        body[CapsOffset.FW_PATCH] = 0
        writeU16(body, CapsOffset.ATT_MTU, 247)
        writeU16(body, CapsOffset.MAX_CMD_PAYLOAD, 235)
        writeU32(body, CapsOffset.CAPS, 0x3F)
        writeU32(body, CapsOffset.TELEMETRY_MASK, 0)
        writeU16(body, CapsOffset.STATUS_SIZE, 82)

        val caps = BleFrame.parseCaps(body)
        assertNotNull(caps)
        caps!!
        assertEquals(1, caps.ver)
        assertEquals("0.1.0", caps.protocolText)
        assertEquals("0.2.0", caps.firmwareText)
        assertEquals(247, caps.attMtu)
        assertEquals(235, caps.maxCmdPayload)
        assertEquals(0x3FL, caps.caps)
        assertEquals(0L, caps.telemetryMask)
        assertEquals(82, caps.statusSize)

        assertNull(BleFrame.parseCaps(body.copyOf(CapsOffset.SIZE - 1)))
    }

    @Test
    fun parseFlashInfoReadsTheDocumentedFields() {
        val body = ByteArray(FlashInfoOffset.SIZE)
        writeU32(body, FlashInfoOffset.FLASH_BYTES, 0x04000000L)
        writeU32(body, FlashInfoOffset.OTA_SLOT1_BYTES, 0x00100000L)
        writeU32(body, FlashInfoOffset.TRAJ_BYTES, 0x03F00000L)
        writeU32(body, FlashInfoOffset.SECTOR_SEQ, 3)
        writeU32(body, FlashInfoOffset.RECORDS_WRITTEN, 400)
        body[FlashInfoOffset.RECORDS_IN_SECTOR] = 17
        body[FlashInfoOffset.ERASE_COUNT_MAX] = 2
        body[FlashInfoOffset.SECTOR_COUNT] = 16

        val info = BleFrame.parseFlashInfo(body)
        assertNotNull(info)
        info!!
        assertEquals(64L * 1024 * 1024, info.flashBytes)
        assertEquals(1024L * 1024, info.otaSlot1Bytes)
        assertEquals(3L, info.sectorSeq)
        assertEquals(400L, info.recordsWritten)
        assertEquals(17, info.recordsInSector)
        assertEquals(2, info.eraseCountMax)
        assertEquals(16, info.sectorCount)
    }

    // --- log packets ----------------------------------------------------------

    @Test
    fun parseLogSplitsLevelSourceAndMessage() {
        val source = "lgps_ble".toByteArray(Charsets.US_ASCII)
        val message = "advertising as LGPS-3F2A".toByteArray(Charsets.UTF_8)
        val packet = ByteArray(2 + source.size + message.size)
        packet[0] = 2 // INFO
        packet[1] = source.size.toByte()
        source.copyInto(packet, 2)
        message.copyInto(packet, 2 + source.size)

        val parsed = BleFrame.parseLog(packet)
        assertNotNull(parsed)
        parsed!!
        assertEquals(2, parsed.level)
        assertEquals("lgps_ble", parsed.source)
        assertEquals("advertising as LGPS-3F2A", parsed.message)

        assertNull(BleFrame.parseLog(byteArrayOf(0x02)))
        // src_len longer than the buffer must be rejected, not read past the end.
        assertNull(BleFrame.parseLog(byteArrayOf(0x02, 0x7F)))
    }

    @Test
    fun unknownOpcodesStayUnresolved() {
        assertNull(Opcode.fromCode(0x7E))
        assertNull(Opcode.fromName("NOT_A_COMMAND"))
        assertEquals(Opcode.FLASH_INFO, Opcode.fromName("flash_info"))
        assertEquals("NOT_SUPPORTED", StatusCode.labelOf(0x03))
        assertEquals("UNKNOWN_STATUS", StatusCode.labelOf(0x7E))
        assertEquals("UNKNOWN(9)", BleFrame.gnssStateName(9))
    }

    // --- telemetry ------------------------------------------------------------

    private fun telemPacket(id: Int, seq: Int, payload: ByteArray): ByteArray {
        val out = ByteArray(TelemetryOffset.PAYLOAD + payload.size)
        out[TelemetryOffset.ID] = id.toByte()
        out[TelemetryOffset.VER] = BleFrame.VER.toByte()
        writeU16(out, TelemetryOffset.SEQ, seq)
        payload.copyInto(out, TelemetryOffset.PAYLOAD)
        return out
    }

    @Test
    fun parseTelemetrySplitsHeaderAndPayload() {
        val raw = telemPacket(TelemetryId.PDR, 0x1234, ByteArray(12))

        val packet = BleFrame.parseTelemetry(raw)
        assertNotNull(packet)
        packet!!
        assertEquals(TelemetryId.PDR, packet.id)
        assertEquals(BleFrame.VER, packet.ver)
        assertEquals(0x1234, packet.seq)
        assertEquals(12, packet.payload.size)

        // A wrong version byte must be refused: the payload layout may differ.
        val badVersion = raw.copyOf()
        badVersion[TelemetryOffset.VER] = 2
        assertNull(BleFrame.parseTelemetry(badVersion))

        assertNull(BleFrame.parseTelemetry(ByteArray(3)))
    }

    @Test
    fun parseImuTelemetryMatchesTheDocumentedOffsets() {
        val payload = ByteArray(20)
        writeU16(payload, 0, 258)          // acc x = 0x0102
        writeU16(payload, 2, 0xFFFE)       // acc y = -2
        writeU16(payload, 4, 0x7FFF)       // acc z
        writeU16(payload, 6, 0x0304)       // gyro x
        writeU16(payload, 8, 0xFFFC)       // gyro y = -4
        writeU16(payload, 10, 0x7FFE)      // gyro z
        writeU16(payload, 12, 32767)       // qw = 1.0
        writeU16(payload, 14, 0)           // qx
        writeU16(payload, 16, 0xC000)      // qy = -16384
        writeU16(payload, 18, 0x2000)      // qz = 8192

        val imu = BleFrame.parseImuTelemetry(payload)
        assertNotNull(imu)
        imu!!
        assertEquals(listOf(258, -2, 0x7FFF), imu.accLsb)
        assertEquals(listOf(0x0304, -4, 0x7FFE), imu.gyroLsb)
        assertEquals(listOf(32767, 0, -16384, 8192), imu.quatQ15)
        assertEquals(1.0f, imu.qw, 1e-4f)
        assertEquals(-0.5f, imu.qy, 1e-4f)
        assertEquals(0.25f, imu.qz, 1e-3f)

        assertNull(BleFrame.parseImuTelemetry(ByteArray(19)))
    }

    @Test
    fun parsePdrTelemetryMatchesTheDocumentedOffsets() {
        val payload = ByteArray(12)
        writeU32(payload, 0, 100_000L)             // x east, mm
        writeU32(payload, 4, 0xFFFFFF9CL)          // y = -100 mm
        writeU16(payload, 8, 3599)                 // heading, 0.1 deg
        writeU16(payload, 10, 1234)                // steps

        val pdr = BleFrame.parsePdrTelemetry(payload)
        assertNotNull(pdr)
        pdr!!
        assertEquals(100_000, pdr.xMm)
        assertEquals(-100, pdr.yMm)
        assertEquals(3599, pdr.headingDdeg)
        assertEquals(1234, pdr.steps)

        assertNull(BleFrame.parsePdrTelemetry(ByteArray(11)))
    }

    @Test
    fun parseGnssTelemetryMatchesTheDocumentedOffsets() {
        val payload = ByteArray(GnssTelemetryOffset.SIZE)
        payload[GnssTelemetryOffset.STATE] = 2
        payload[GnssTelemetryOffset.FIX_TYPE] = 3
        payload[GnssTelemetryOffset.SATELLITES] = 11
        payload[GnssTelemetryOffset.FLAGS] =
            (GnssTelemetryOffset.FLAG_FIX_VALID or GnssTelemetryOffset.FLAG_UTC_VALID).toByte()
        writeU16(payload, GnssTelemetryOffset.PDOP_1E2, 0x0123)
        writeU32(payload, GnssTelemetryOffset.AGE_MS, 0x11223344L)
        writeU32(payload, GnssTelemetryOffset.RX_BYTES, 0x55667788L)
        writeU32(payload, GnssTelemetryOffset.UBX_FRAMES, 9)
        writeU32(payload, GnssTelemetryOffset.BAD_CHECKSUM, 1)
        writeU32(payload, GnssTelemetryOffset.NMEA_STARTS, 0x42)
        writeU32(payload, GnssTelemetryOffset.RX_DROPPED, 0)
        writeU32(payload, GnssTelemetryOffset.BAUD, 38400)

        val gnss = BleFrame.parseGnssTelemetry(payload)
        assertNotNull(gnss)
        gnss!!
        assertEquals(2, gnss.state)
        assertEquals(3, gnss.fixType)
        assertEquals(11, gnss.satellites)
        assertTrue(gnss.fixValid)
        assertTrue(gnss.utcValid)
        assertEquals(0x0123, gnss.pdop1e2)
        assertEquals(0x11223344L, gnss.ageMs)
        assertEquals(0x55667788L, gnss.rxBytes)
        assertEquals(9L, gnss.ubxFrames)
        assertEquals(1L, gnss.badChecksum)
        assertEquals(0x42L, gnss.nmeaStarts)
        assertEquals(0L, gnss.rxDropped)
        assertEquals(38400, gnss.baud)

        // The packet size is pinned on the firmware side too
        // (LGPS_BLE_TELEM_GNSS_SIZE == 38 -> 34 payload bytes).
        assertEquals(34, GnssTelemetryOffset.SIZE)
        assertNull(BleFrame.parseGnssTelemetry(ByteArray(GnssTelemetryOffset.SIZE - 1)))

        // A flag byte without the fix bit must not report a fix.
        payload[GnssTelemetryOffset.FLAGS] = GnssTelemetryOffset.FLAG_UTC_VALID.toByte()
        val utcOnly = BleFrame.parseGnssTelemetry(payload)
        assertNotNull(utcOnly)
        assertFalse(utcOnly!!.fixValid)
        assertTrue(utcOnly.utcValid)
    }

    @Test
    fun parseSatTelemetryMatchesTheDocumentedOffsets() {
        val payload = ByteArray(SatTelemetry.HEADER + 2 * SatTelemetry.SIZE_ENTRY)
        payload[0] = 2                     // count
        payload[1] = 2                     // total
        payload[SatTelemetry.HEADER + 0] = 0     // gnssId GPS
        payload[SatTelemetry.HEADER + 1] = 7     // svId
        payload[SatTelemetry.HEADER + 2] = 42    // cno
        payload[SatTelemetry.HEADER + 3] = 55    // elev
        payload[SatTelemetry.HEADER + 4] = 0x0C  // quality 4 | used
        val second = SatTelemetry.HEADER + SatTelemetry.SIZE_ENTRY
        payload[second + 0] = 3
        payload[second + 1] = 21
        payload[second + 2] = 28
        payload[second + 3] = 0xF4.toByte()      // -12
        payload[second + 4] = 0x01               // quality 1, not used

        val sat = BleFrame.parseSatTelemetry(payload)
        assertNotNull(sat)
        sat!!
        assertEquals(2, sat.total)
        assertEquals(2, sat.rows.size)
        assertEquals(0, sat.rows[0].gnssId)
        assertEquals(7, sat.rows[0].svId)
        assertEquals(42, sat.rows[0].cno)
        assertEquals(55, sat.rows[0].elevation)
        assertEquals(4, sat.rows[0].quality)
        assertTrue(sat.rows[0].used)
        assertEquals("跟踪", sat.rows[0].qualityLabel)
        assertEquals(-12, sat.rows[1].elevation)
        assertFalse(sat.rows[1].used)
        assertEquals("搜索中", sat.rows[1].qualityLabel)

        // A declared count larger than the payload must not read past the end.
        payload[0] = 9
        val truncated = BleFrame.parseSatTelemetry(payload)
        assertNotNull(truncated)
        assertEquals(2, truncated!!.rows.size)

        assertNull(BleFrame.parseSatTelemetry(ByteArray(3)))
    }

    @Test
    fun parseNmeaTelemetryReadsTheLengthPrefixedText() {
        val line = "\$GNGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*4C"
        val payload = ByteArray(1 + line.length)
        payload[0] = line.length.toByte()
        line.toByteArray(Charsets.US_ASCII).copyInto(payload, 1)

        val nmea = BleFrame.parseNmeaTelemetry(payload)
        assertNotNull(nmea)
        assertEquals(line, nmea!!.text)

        assertNull(BleFrame.parseNmeaTelemetry(ByteArray(1)))
        assertNull(BleFrame.parseNmeaTelemetry(byteArrayOf(9, 0x24)))
    }

    @Test
    fun gnssDebugCodecRoundTrips() {
        val body = GnssCodec.encodeDebug(
            GnssCodec.FLAG_HOLD_POWER or GnssCodec.FLAG_NAV_SAT,
            300,
        )
        assertEquals(3, body.size)
        assertEquals(0x03, body[0].toInt())
        assertEquals(300, (body[1].toInt() and 0xFF) or ((body[2].toInt() and 0xFF) shl 8))

        val state = GnssCodec.decodeDebug(byteArrayOf(0x07, 0x2C, 0x01))
        assertNotNull(state)
        state!!
        assertEquals(7, state.flags)
        assertEquals(300, state.remainingSeconds)
        assertTrue(state.holdPower && state.navSat && state.nmea)

        assertNull(GnssCodec.decodeDebug(ByteArray(2)))
        assertEquals(0, GnssCodec.encodeDebugQuery().size)
    }

    @Test
    fun gnssCfgCodecUsesTheWhitelistedWidths() {
        val get = GnssCodec.encodeCfgGet(GnssCfgKeys.RATE_MEAS.key)
        assertEquals(GnssCodec.ACTION_GET, get[0].toInt())
        assertEquals(5, get.size)

        val set = GnssCodec.encodeCfgSet(GnssCfgKeys.RATE_MEAS.key, 1000, 2, persist = true)
        assertEquals(GnssCodec.ACTION_SET_PERSIST, set[0].toInt())
        assertEquals(7, set.size)
        assertEquals(0xE8, set[5].toInt() and 0xFF)
        assertEquals(0x03, set[6].toInt() and 0xFF)

        // Response body: key u32, len u8, value[len].
        val readback = ByteArray(7)
        writeU32(readback, 0, GnssCfgKeys.RATE_MEAS.key)
        readback[4] = 2
        readback[5] = 0xE8.toByte()
        readback[6] = 0x03
        val value = GnssCodec.decodeCfg(readback)
        assertNotNull(value)
        assertEquals(GnssCfgKeys.RATE_MEAS.key, value!!.key)
        assertEquals(2, value.len)
        assertEquals(1000L, value.value)

        assertNull(GnssCodec.decodeCfg(ByteArray(5)))
        // Every key in the table carries 1, 2 or 4 bytes - never something else.
        GnssCfgKeys.ALL.forEach { key ->
            assertTrue(key.len == 1 || key.len == 2 || key.len == 4)
            assertTrue(key.min <= key.max)
        }
    }
}
