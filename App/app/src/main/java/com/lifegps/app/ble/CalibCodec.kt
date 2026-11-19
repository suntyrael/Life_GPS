package com.lifegps.app.ble

/**
 * Codec for the two calibration commands (`MAG_CAL` 0x41, `ACCEL_CAL` 0x43).
 *
 * Pure Kotlin with no Android types, so the byte offsets are checked in CI by
 * `CalibCodecTest` and nowhere else. The firmware side implements the same
 * layout; a one-sided edit has to fail one of the two suites instead of failing
 * on a phone at night.
 *
 * Wire contract (little endian, payload = status byte + body, the decoders below
 * take the body with the status byte already stripped, like [BleFrame.parseCaps]):
 *
 * MAG_CAL request (1 B)
 *   [0] u8 action
 *
 * MAG_CAL response body (30 B)
 *   [0]  u16 progress_permille      capture progress, 0..1000
 *   [2]  u8  sample_count           saturates at 255
 *   [3]  u8  coverage_pct_x         0..100, how much of that axis range was covered
 *   [4]  u8  coverage_pct_y
 *   [5]  u8  coverage_pct_z
 *   [6]  i16 bias_x_lsb             hard-iron centre, raw LIS2MDL LSB
 *   [8]  i16 bias_y_lsb
 *   [10] i16 bias_z_lsb
 *   [12] i32 scale_x_ppm            soft-iron gain, applied as *(1 + ppm/1e6)
 *   [16] i32 scale_y_ppm
 *   [20] i32 scale_z_ppm
 *   [24] u8  calib_state            [CalibStateCode]
 *   [25] u8  calib_flags            [CalibFlagBits]
 *   [26] u32 last_calib_unix_s      0 when the device has no UTC yet
 *
 * ACCEL_CAL request (2 B)
 *   [0] u8 action
 *   [1] u8 position   [CalibCodec.POSITION_AUTO] = identify the pose from gravity,
 *                     0..5 = force a face (bench only), 6 = single-face quick mode
 *
 * ACCEL_CAL response body (48 B)
 *   [0]  u16 progress_permille
 *   [2]  u8  face_mask              bit n set = face n captured
 *   [3]  u8  face_state             [AccelFaceStateCode], only meaningful while collecting
 *   [4]  u8  rest_pct               0..100, how still the device is right now
 *   [5]  u8  detected_face          face the firmware identified for the last
 *                                   capture, 0xFF = none / too tilted
 *   [6]  i32 bias_x_ug              accelerometer bias in µg (1 g = 1_000_000 µg)
 *   [10] i32 bias_y_ug
 *   [14] i32 bias_z_ug
 *   [18] i32 scale_x_ppm
 *   [22] i32 scale_y_ppm
 *   [26] i32 scale_z_ppm
 *   [30] i32 gyro_bias_x_mdps       zero-rate offset in milli-deg/s
 *   [34] i32 gyro_bias_y_mdps
 *   [38] i32 gyro_bias_z_mdps
 *   [42] u8  calib_state
 *   [43] u8  calib_flags
 *   [44] u32 last_calib_unix_s
 *
 * Biases are stored in engineering units (µg, LSB) rather than in the sensor's
 * `X/Y/Z_OFS_USR` register units on purpose: that register is an int8 in units of
 * 2^-10 g, i.e. ±124 mg with a 0.98 mg quantum, which cannot even round-trip the
 * value it is asked to display. The register value is derived on the firmware
 * side when (and only if) it chooses the in-sensor path.
 */
object CalibCodec {

    /** Request payloads. */
    const val MAG_REQ_SIZE = 1
    const val ACCEL_REQ_SIZE = 2

    /** Response bodies, status byte excluded. */
    const val MAG_RSP_SIZE = 30
    const val ACCEL_RSP_SIZE = 48

    /**
     * Face indices: 0..5 are the six faces, 6 selects the single-face quick mode.
     * A face index is not a user choice: the firmware identifies the pose from the
     * gravity vector, so the App normally sends [POSITION_AUTO].
     */
    const val POSITION_COUNT = 6
    const val POSITION_QUICK = 6

    /** "Identify the pose yourself" in the ACCEL_CAL request. */
    const val POSITION_AUTO = 0xFF

    /** `detected_face` value when the device could not identify a face (too tilted). */
    const val FACE_NONE = 0xFF

    fun encodeMagCal(action: CalibAction): ByteArray = byteArrayOf(action.code.toByte())

    fun encodeAccelCal(action: CalibAction, position: Int = POSITION_AUTO): ByteArray {
        require(position in 0..POSITION_QUICK || position == POSITION_AUTO) {
            "position $position is neither 0..$POSITION_QUICK nor POSITION_AUTO"
        }
        return byteArrayOf(action.code.toByte(), position.toByte())
    }

    /** Returns null when the body is shorter than [MAG_RSP_SIZE]; never throws. */
    fun decodeMagCal(body: ByteArray): MagCalResponse? {
        if (body.size < MAG_RSP_SIZE) return null
        return MagCalResponse(
            progressPermille = BleFrame.u16(body, 0),
            sampleCount = BleFrame.u8(body, 2),
            coveragePct = listOf(
                BleFrame.u8(body, 3),
                BleFrame.u8(body, 4),
                BleFrame.u8(body, 5),
            ),
            biasLsb = listOf(
                BleFrame.i16(body, 6),
                BleFrame.i16(body, 8),
                BleFrame.i16(body, 10),
            ),
            scalePpm = listOf(
                BleFrame.i32(body, 12),
                BleFrame.i32(body, 16),
                BleFrame.i32(body, 20),
            ),
            state = BleFrame.u8(body, 24),
            flags = BleFrame.u8(body, 25),
            lastCalibUnixS = BleFrame.u32(body, 26),
        )
    }

    /** Returns null when the body is shorter than [ACCEL_RSP_SIZE]; never throws. */
    fun decodeAccelCal(body: ByteArray): AccelCalResponse? {
        if (body.size < ACCEL_RSP_SIZE) return null
        return AccelCalResponse(
            progressPermille = BleFrame.u16(body, 0),
            faceMask = BleFrame.u8(body, 2),
            faceState = BleFrame.u8(body, 3),
            restPct = BleFrame.u8(body, 4),
            detectedFace = BleFrame.u8(body, 5),
            biasUg = listOf(
                BleFrame.i32(body, 6),
                BleFrame.i32(body, 10),
                BleFrame.i32(body, 14),
            ),
            scalePpm = listOf(
                BleFrame.i32(body, 18),
                BleFrame.i32(body, 22),
                BleFrame.i32(body, 26),
            ),
            gyroBiasMdps = listOf(
                BleFrame.i32(body, 30),
                BleFrame.i32(body, 34),
                BleFrame.i32(body, 38),
            ),
            state = BleFrame.u8(body, 42),
            flags = BleFrame.u8(body, 43),
            lastCalibUnixS = BleFrame.u32(body, 44),
        )
    }
}

/**
 * Calibration action byte. `START` is "begin this capture", `FINISH_SAVE`
 * computes the coefficients and writes them to the calibration record,
 * `CLEAR` drops the stored record, `QUERY` has no side effect (used to poll
 * progress and to re-sync after a reconnect) and `CANCEL` aborts a running
 * capture while leaving the stored record untouched.
 */
enum class CalibAction(val code: Int) {
    START(0),
    FINISH_SAVE(1),
    CLEAR(2),
    QUERY(3),
    CANCEL(4),
}

/** `calib_state` byte: what the firmware is currently doing. */
object CalibStateCode {
    const val IDLE = 0
    const val COLLECTING = 1
    const val RESULT_PENDING = 2
}

/** `calib_flags` bits. */
object CalibFlagBits {
    /** The reported coefficients come from the persisted record. */
    const val FROM_STORE = 0x01

    /** No valid record: the reported coefficients are the compiled-in defaults. */
    const val DEFAULTS = 0x02

    /** Accelerometer result was produced by the single-face quick mode (low accuracy). */
    const val QUICK_SINGLE_FACE = 0x04

    /** The coefficients took effect but the record could not be written to flash. */
    const val SAVE_FAILED = 0x08
}

/** `face_state` byte of the accelerometer capture. */
object AccelFaceStateCode {
    const val WAIT_REST = 0
    const val COLLECTING = 1
    const val FACE_DONE = 2
    const val JITTER_TIMEOUT = 3
}

/** Decoded MAG_CAL body. */
data class MagCalResponse(
    val progressPermille: Int,
    val sampleCount: Int,
    val coveragePct: List<Int>,
    val biasLsb: List<Int>,
    val scalePpm: List<Int>,
    val state: Int,
    val flags: Int,
    val lastCalibUnixS: Long,
) {
    val fromStore: Boolean get() = flags and CalibFlagBits.FROM_STORE != 0
    val usingDefaults: Boolean get() = flags and CalibFlagBits.DEFAULTS != 0
    val saveFailed: Boolean get() = flags and CalibFlagBits.SAVE_FAILED != 0

    /** Lowest of the three axis coverage values; the App gates "finish" on it. */
    val minCoveragePct: Int get() = coveragePct.min()
}

/** Decoded ACCEL_CAL body. */
data class AccelCalResponse(
    val progressPermille: Int,
    val faceMask: Int,
    val faceState: Int,
    val restPct: Int,
    val detectedFace: Int,
    val biasUg: List<Int>,
    val scalePpm: List<Int>,
    val gyroBiasMdps: List<Int>,
    val state: Int,
    val flags: Int,
    val lastCalibUnixS: Long,
) {
    val fromStore: Boolean get() = flags and CalibFlagBits.FROM_STORE != 0
    val usingDefaults: Boolean get() = flags and CalibFlagBits.DEFAULTS != 0
    val quickMode: Boolean get() = flags and CalibFlagBits.QUICK_SINGLE_FACE != 0
    val saveFailed: Boolean get() = flags and CalibFlagBits.SAVE_FAILED != 0

    fun hasFace(position: Int): Boolean =
        position in 0 until CalibCodec.POSITION_COUNT && (faceMask and (1 shl position)) != 0

    /** True when the firmware identified a concrete face for the last capture. */
    val hasDetectedFace: Boolean get() = detectedFace in 0 until CalibCodec.POSITION_COUNT

    val allFaces: Boolean get() = (faceMask and SIX_FACE_MASK) == SIX_FACE_MASK

    private companion object {
        const val SIX_FACE_MASK = 0x3F
    }
}
