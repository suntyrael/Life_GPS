package com.lifegps.app.debug

import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.math.abs

/**
 * Calibration domain model for the App.
 *
 * No wire knowledge lives here: [com.lifegps.app.ble.CalibCodec] decodes the
 * bytes and `BleDebugRepository` maps them into these types. Everything in this
 * file is a pure function of its inputs, so the thresholds and the six-face
 * geometry are unit-tested on the JVM (`CalibVerdictTest`) instead of being
 * eyeballed on a phone.
 */

/** What a calibration flow is doing right now. */
enum class CalibPhase {
    /** Firmware answered NOT_SUPPORTED / UNKNOWN_OPCODE for this opcode. */
    UNSUPPORTED,
    IDLE,
    COLLECTING,
    /** Coefficients computed but not written to flash yet. */
    REVIEW,
    /** FINISH_SAVE is in flight. */
    SAVING,
    /** Coefficients persisted; the device is using them. */
    SAVED,
    FAILED,
}

/** Human-readable calibration verdict; `notes` is empty when `ok`. */
data class CalibVerdict(val ok: Boolean, val notes: List<String>)

/** Persisted magnetometer coefficients, in engineering units. */
data class MagCalResult(
    val biasLsb: List<Int>,
    val scalePpm: List<Int>,
    val coveragePct: List<Int>,
    val sampleCount: Int,
    val fromStore: Boolean,
    val lastCalibUnixS: Long,
)

/** Persisted accelerometer/gyroscope coefficients, in engineering units. */
data class AccelCalResult(
    val biasUg: List<Int>,
    val scalePpm: List<Int>,
    val gyroBiasMdps: List<Int>,
    val faceMask: Int,
    val quickMode: Boolean,
    val fromStore: Boolean,
    val lastCalibUnixS: Long,
)

/**
 * Calibration UI state, owned by the repository and rendered by
 * `CalibrationScreen`.
 *
 * The firmware is the single source of truth: the App never advances a phase on
 * its own except for the optimistic COLLECTING/SAVING states that are overwritten
 * by the next response.
 */
data class CalibState(
    val magSupported: Boolean = false,
    val accelSupported: Boolean = false,
    val magPhase: CalibPhase = CalibPhase.IDLE,
    val magProgressPermille: Int = 0,
    val magCoveragePct: List<Int> = ZERO_TRIPLE,
    val magSampleCount: Int = 0,
    val magResult: MagCalResult? = null,
    val magVerdict: CalibVerdict? = null,
    val magMessage: String = "",
    val accelPhase: CalibPhase = CalibPhase.IDLE,
    val accelActivePosition: Int = -1,
    val accelProgressPermille: Int = 0,
    val accelFaceMask: Int = 0,
    val accelFaceState: Int = 0,
    val accelRestPct: Int = 0,
    /** Face the firmware identified for the last capture; [CalibFaces.NONE] if none. */
    val accelDetectedFace: Int = CalibFaces.NONE,
    val accelResult: AccelCalResult? = null,
    val accelVerdict: CalibVerdict? = null,
    val accelMessage: String = "",
    val lastCalibUnixS: Long = 0L,
    /** A command is in flight; the UI disables the buttons instead of queueing. */
    val busy: Boolean = false,
) {
    companion object {
        val ZERO_TRIPLE: List<Int> = listOf(0, 0, 0)
    }
}

/**
 * Six-face geometry.
 *
 * `position` answers one question only: **which body axis points up** when the
 * device rests in that pose.
 *
 *   0 = +X up   1 = -X up   2 = +Y up
 *   3 = -Y up   4 = +Z up   5 = -Z up
 *
 * That definition is chosen because it is directly checkable on screen: at rest
 * the accelerometer measures the reaction to gravity, so the firmware's
 * `gravity_mg` vector reads +1 g on the axis that points up. [gravityMatches]
 * compares the live vector with the pose the user claims to be holding, which
 * makes a mislabelled face a visible warning instead of a silently bad
 * calibration.
 */
object CalibFaces {
    const val COUNT = 6

    /** No face could be identified: the device is too tilted to be sure. */
    const val NONE = -1

    /** Axis index (0 = X, 1 = Y, 2 = Z) that points up in this pose. */
    fun axisOf(position: Int): Int = position / 2

    /** Sign of the measurement on that axis at rest: +1 g on the up axis. */
    fun signOf(position: Int): Int = if (position % 2 == 0) 1 else -1

    private val AXIS_NAMES = listOf("X", "Y", "Z")

    fun label(position: Int): String {
        val axis = AXIS_NAMES[axisOf(position)]
        val sign = if (signOf(position) > 0) "+" else "-"
        return "$sign$axis 轴朝上（第 ${position + 1} 面）"
    }

    fun shortLabel(position: Int): String {
        val axis = AXIS_NAMES[axisOf(position)]
        return if (signOf(position) > 0) "+$axis↑" else "-$axis↑"
    }

    /**
     * Identifies which face is up from the live gravity vector.
     *
     * The App uses this only to *predict* what the firmware will report and to warn
     * the user when the pose is too tilted; the recorded face is always the one in
     * the ACCEL_CAL response, because the firmware decides on 30 Hz samples.
     *
     * @return 0..5, or [NONE] when the dominant axis is too weak or another axis
     *         carries too much of the vector.
     */
    fun detect(gravityG: List<Float>): Int {
        if (gravityG.size < 3) return NONE
        var axis = 0
        var magnitude = abs(gravityG[0])
        for (index in 1 until 3) {
            if (abs(gravityG[index]) > magnitude) {
                axis = index
                magnitude = abs(gravityG[index])
            }
        }
        if (magnitude <= DOMINANT_MIN_G) return NONE
        for (index in 0 until 3) {
            if (index == axis) continue
            if (abs(gravityG[index]) > OFF_AXIS_MAX_G) return NONE
        }
        val up = gravityG[axis] > 0f
        return axis * 2 + if (up) 0 else 1
    }

    /**
     * True when [gravityG] (body-frame X/Y/Z in g) is consistent with claiming
     * [position]. The tolerances allow a roughly level placement on a desk: the
     * expected axis has to dominate, the other two must stay small.
     */
    fun gravityMatches(position: Int, gravityG: List<Float>): Boolean {
        if (position !in 0 until COUNT || gravityG.size < 3) return false
        val axis = axisOf(position)
        val dominant = gravityG[axis] * signOf(position)
        if (dominant <= DOMINANT_MIN_G) return false
        for (index in 0 until 3) {
            if (index == axis) continue
            if (abs(gravityG[index]) > OFF_AXIS_MAX_G) return false
        }
        return true
    }

    private const val DOMINANT_MIN_G = 0.7f
    private const val OFF_AXIS_MAX_G = 0.45f
}

/**
 * Acceptance thresholds.
 *
 * These are the numbers a user sees as "合格 / 建议重做"; they are not the
 * firmware's calibration maths, only a sanity gate so a bad capture is caught in
 * the wizard instead of a week later in the PDR drift. They are deliberately
 * loose: the bench unit with a 2.4 % scale error (the bench finding that started
 * this work) must pass, a device that was waved during capture must not.
 */
object CalibJudge {
    /** 30 mg: the offset register itself cannot resolve better than ~1 mg. */
    const val ACCEL_BIAS_OK_UG = 30_000

    /** 3 %: covers typical part-to-part gain error with margin. */
    const val ACCEL_SCALE_OK_PPM = 30_000

    /** 2 °/s residual zero-rate offset; above this the attitude yaw drifts visibly. */
    const val GYRO_BIAS_OK_MDPS = 2_000

    /** 800 LSB = 120 µT of hard iron: a phone-sized permanent magnet nearby. */
    const val MAG_BIAS_OK_LSB = 800

    /** 15 % axis-to-axis gain mismatch. */
    const val MAG_SCALE_OK_PPM = 150_000

    /** Below this the min/max centre estimate is dominated by noise. */
    const val MAG_COVERAGE_MIN_PCT = 80

    private val AXIS_NAMES = listOf("X", "Y", "Z")

    fun judgeAccel(result: AccelCalResult): CalibVerdict {
        val notes = mutableListOf<String>()
        for (axis in 0 until 3) {
            val name = AXIS_NAMES[axis]
            if (abs(result.biasUg.getOrElse(axis) { 0 }) > ACCEL_BIAS_OK_UG) {
                notes += "$name 轴零偏 ${CalibFormat.microG(result.biasUg.getOrElse(axis) { 0 })} 超过 ±30 mg"
            }
            if (abs(result.scalePpm.getOrElse(axis) { 0 }) > ACCEL_SCALE_OK_PPM) {
                notes += "$name 轴刻度 ${CalibFormat.ppm(result.scalePpm.getOrElse(axis) { 0 })} 超过 ±3 %"
            }
            if (abs(result.gyroBiasMdps.getOrElse(axis) { 0 }) > GYRO_BIAS_OK_MDPS) {
                notes += "陀螺 $name 轴零偏 " +
                    "${CalibFormat.milliDps(result.gyroBiasMdps.getOrElse(axis) { 0 })} 超过 ±2 °/s"
            }
        }
        if (!result.quickMode && !allSixFaces(result.faceMask)) {
            notes += "六面未采全（face_mask=0x${result.faceMask.toString(16).uppercase(Locale.US)}）"
        }
        return CalibVerdict(notes.isEmpty(), notes)
    }

    fun judgeMag(result: MagCalResult): CalibVerdict {
        val notes = mutableListOf<String>()
        for (axis in 0 until 3) {
            val name = AXIS_NAMES[axis]
            if (abs(result.biasLsb.getOrElse(axis) { 0 }) > MAG_BIAS_OK_LSB) {
                notes += "$name 轴硬铁 " +
                    "${CalibFormat.magLsb(result.biasLsb.getOrElse(axis) { 0 })} 超过 ±800 LSB"
            }
            if (abs(result.scalePpm.getOrElse(axis) { 0 }) > MAG_SCALE_OK_PPM) {
                notes += "$name 轴刻度 " +
                    "${CalibFormat.ppm(result.scalePpm.getOrElse(axis) { 0 })} 超过 ±15 %"
            }
        }
        val coverage = result.coveragePct.minOrNull() ?: 0
        if (coverage < MAG_COVERAGE_MIN_PCT) {
            notes += "轴覆盖不足（最低 $coverage %，需要 ≥ $MAG_COVERAGE_MIN_PCT %）"
        }
        return CalibVerdict(notes.isEmpty(), notes)
    }

    fun allSixFaces(faceMask: Int): Boolean = (faceMask and SIX_FACE_MASK) == SIX_FACE_MASK

    private const val SIX_FACE_MASK = 0x3F
}

/**
 * Presentation helpers. Kept out of the composables so the exact strings the user
 * judges the device by are unit-tested (a wrong unit here is a wrong calibration).
 */
object CalibFormat {
    private val LAST_CALIB_FORMAT: DateTimeFormatter =
        DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm").withZone(ZoneOffset.UTC)

    /** 1 g = 1_000_000 µg; show one decimal of a milligram. */
    fun microG(ug: Int): String = String.format(Locale.US, "%+.1f mg", ug / 1000.0)

    /** Parts per million as a percentage with two decimals. */
    fun ppm(ppm: Int): String = String.format(Locale.US, "%+.2f %%", ppm / 10_000.0)

    /** Milli-degrees per second as degrees per second. */
    fun milliDps(mdps: Int): String = String.format(Locale.US, "%+.2f °/s", mdps / 1000.0)

    /** Raw LIS2MDL counts, with the 0.15 µT/LSB sensitivity in brackets. */
    fun magLsb(lsb: Int): String =
        String.format(Locale.US, "%+d LSB (%+.1f µT)", lsb, lsb * 0.15)

    /**
     * Last-calibration stamp. Zero is not "never": a device that was calibrated
     * before its first GNSS fix has coefficients but no UTC, so the stored
     * timestamp stays 0 and must not be reported as "never calibrated".
     */
    fun lastCalib(unixSeconds: Long, fromStore: Boolean): String = when {
        !fromStore -> "未校准（使用编译期默认值）"
        unixSeconds <= 0L -> "已校准（设备未同步 UTC，无时间戳）"
        else -> "已校准 ${LAST_CALIB_FORMAT.format(Instant.ofEpochSecond(unixSeconds))} UTC"
    }
}
