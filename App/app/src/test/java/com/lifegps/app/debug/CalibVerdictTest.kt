package com.lifegps.app.debug

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM tests for the calibration acceptance rules, the presentation helpers and the
 * six-face geometry.
 *
 * The thresholds in [CalibJudge] are what the user reads as "合格 / 建议重做", so
 * they are checked here rather than trusted: a wrong unit or an off-by-10x
 * constant would otherwise only show up as a bad PDR heading weeks later.
 */
class CalibVerdictTest {

    private fun accelResult(
        biasUg: List<Int> = listOf(-24_600, 8_000, -1_000),
        scalePpm: List<Int> = listOf(-23_400, 500, -100),
        gyroBiasMdps: List<Int> = listOf(-350, 120, 0),
        faceMask: Int = 0x3F,
        quickMode: Boolean = false,
    ) = AccelCalResult(
        biasUg = biasUg,
        scalePpm = scalePpm,
        gyroBiasMdps = gyroBiasMdps,
        faceMask = faceMask,
        quickMode = quickMode,
        fromStore = false,
        lastCalibUnixS = 0L,
    )

    private fun magResult(
        biasLsb: List<Int> = listOf(120, -80, 40),
        scalePpm: List<Int> = listOf(12_000, -9_000, 3_500),
        coveragePct: List<Int> = listOf(100, 95, 88),
    ) = MagCalResult(
        biasLsb = biasLsb,
        scalePpm = scalePpm,
        coveragePct = coveragePct,
        sampleCount = 300,
        fromStore = false,
        lastCalibUnixS = 0L,
    )

    // --- accelerometer --------------------------------------------------------

    @Test
    fun accelResultFromTheBenchFindingPasses() {
        // -24.6 mg bias and a -2.34 % scale error is roughly what a real
        // LSM6DSR showed on the bench (the 1.024 g reading that started this work).
        val verdict = CalibJudge.judgeAccel(accelResult())
        assertTrue(verdict.notes.toString(), verdict.ok)
        assertEquals(emptyList<String>(), verdict.notes)
    }

    @Test
    fun accelBiasAtTheLimitStillPasses() {
        val verdict = CalibJudge.judgeAccel(
            accelResult(biasUg = listOf(CalibJudge.ACCEL_BIAS_OK_UG, 0, 0)),
        )
        assertTrue(verdict.notes.toString(), verdict.ok)
    }

    @Test
    fun accelBiasOneMicrogramOverTheLimitFails() {
        val verdict = CalibJudge.judgeAccel(
            accelResult(biasUg = listOf(CalibJudge.ACCEL_BIAS_OK_UG + 1, 0, 0)),
        )
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.first().startsWith("X 轴零偏"))
    }

    @Test
    fun accelScaleOverThreePercentFails() {
        val verdict = CalibJudge.judgeAccel(
            accelResult(scalePpm = listOf(0, CalibJudge.ACCEL_SCALE_OK_PPM + 1, 0)),
        )
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.first().startsWith("Y 轴刻度"))
    }

    @Test
    fun gyroBiasOverTwoDegreesPerSecondFails() {
        val verdict = CalibJudge.judgeAccel(
            accelResult(gyroBiasMdps = listOf(0, 0, CalibJudge.GYRO_BIAS_OK_MDPS + 1)),
        )
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.first().startsWith("陀螺 Z 轴零偏"))
    }

    @Test
    fun missingFacesFailTheSixFaceSweep() {
        val verdict = CalibJudge.judgeAccel(accelResult(faceMask = 0x2F))
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.any { it.startsWith("六面未采全") })
    }

    @Test
    fun quickModeIsNotJudgedOnTheFaceMask() {
        val verdict = CalibJudge.judgeAccel(accelResult(faceMask = 0x02, quickMode = true))
        assertTrue(verdict.notes.toString(), verdict.ok)
    }

    // --- magnetometer ---------------------------------------------------------

    @Test
    fun magResultWithFullCoveragePasses() {
        val verdict = CalibJudge.judgeMag(magResult())
        assertTrue(verdict.notes.toString(), verdict.ok)
    }

    @Test
    fun magCoverageAtTheLimitPasses() {
        val limit = CalibJudge.MAG_COVERAGE_MIN_PCT
        val verdict = CalibJudge.judgeMag(magResult(coveragePct = listOf(100, 100, limit)))
        assertTrue(verdict.notes.toString(), verdict.ok)
    }

    @Test
    fun magCoverageBelowTheLimitFails() {
        val verdict = CalibJudge.judgeMag(
            magResult(coveragePct = listOf(100, 100, CalibJudge.MAG_COVERAGE_MIN_PCT - 1)),
        )
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.any { it.startsWith("轴覆盖不足") })
    }

    @Test
    fun magHardIronOverEightHundredLsbFails() {
        val verdict = CalibJudge.judgeMag(
            magResult(biasLsb = listOf(0, CalibJudge.MAG_BIAS_OK_LSB + 1, 0)),
        )
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.first().startsWith("Y 轴硬铁"))
    }

    @Test
    fun magScaleOverFifteenPercentFails() {
        val verdict = CalibJudge.judgeMag(
            magResult(scalePpm = listOf(CalibJudge.MAG_SCALE_OK_PPM + 1, 0, 0)),
        )
        assertFalse(verdict.ok)
        assertTrue(verdict.notes.first().startsWith("X 轴刻度"))
    }

    // --- presentation ---------------------------------------------------------

    @Test
    fun formatsUseTheUnitsTheUserIsAskedToJudge() {
        assertEquals("+24.6 mg", CalibFormat.microG(24_600))
        assertEquals("-1.0 mg", CalibFormat.microG(-1_000))
        assertEquals("+2.46 %", CalibFormat.ppm(24_600))
        assertEquals("-0.35 %", CalibFormat.ppm(-3_500))
        assertEquals("-0.35 °/s", CalibFormat.milliDps(-350))
        assertEquals("+100 LSB (+15.0 µT)", CalibFormat.magLsb(100))
    }

    @Test
    fun lastCalibDistinguishesNeverCalibratedFromNoUtc() {
        assertEquals("未校准（使用编译期默认值）", CalibFormat.lastCalib(0L, false))
        assertEquals("已校准（设备未同步 UTC，无时间戳）", CalibFormat.lastCalib(0L, true))
        assertTrue(CalibFormat.lastCalib(1_765_000_000L, true).startsWith("已校准 20"))
    }

    // --- six-face geometry ----------------------------------------------------

    @Test
    fun positionMapsToTheBodyAxisThatPointsUp() {
        assertEquals(0, CalibFaces.axisOf(0))
        assertEquals(0, CalibFaces.axisOf(1))
        assertEquals(1, CalibFaces.axisOf(2))
        assertEquals(2, CalibFaces.axisOf(4))
        assertEquals(2, CalibFaces.axisOf(5))
        assertEquals(1, CalibFaces.signOf(0))
        assertEquals(-1, CalibFaces.signOf(1))
        assertEquals("+X 轴朝上（第 1 面）", CalibFaces.label(0))
        assertEquals("-X↑", CalibFaces.shortLabel(1))
    }

    @Test
    fun detectedFaceFollowsTheDominantAxis() {
        // Auto-identification: the device (and the App's local preview) derive the
        // face from the gravity vector, so the user never selects one.
        assertEquals(0, CalibFaces.detect(listOf(1f, 0f, 0f)))
        assertEquals(1, CalibFaces.detect(listOf(-1f, 0f, 0f)))
        assertEquals(2, CalibFaces.detect(listOf(0.05f, 0.98f, -0.03f)))
        assertEquals(3, CalibFaces.detect(listOf(0f, -0.95f, 0.1f)))
        assertEquals(4, CalibFaces.detect(listOf(0f, 0f, 1f)))
        assertEquals(5, CalibFaces.detect(listOf(0f, 0f, -0.92f)))
    }

    @Test
    fun ambiguousPoseIsNotDetected() {
        // Two axes sharing the vector means the device is on an edge, not a face:
        // recording "a face" there would poison the six-face maths.
        assertEquals(CalibFaces.NONE, CalibFaces.detect(listOf(0.6f, 0.6f, 0f)))
        assertEquals(CalibFaces.NONE, CalibFaces.detect(listOf(0.2f, 0.2f, 0.2f)))
        assertEquals(CalibFaces.NONE, CalibFaces.detect(listOf(1f, 0f)))
    }

    @Test
    fun liveGravityConfirmsThePoseTheUserClaims() {
        assertTrue(CalibFaces.gravityMatches(4, listOf(0f, 0f, 1f)))
        assertTrue(CalibFaces.gravityMatches(4, listOf(0.05f, -0.08f, 0.99f)))
        assertTrue(CalibFaces.gravityMatches(1, listOf(-1f, 0f, 0f)))
        assertTrue(CalibFaces.gravityMatches(1, listOf(-0.9f, 0.1f, 0.05f)))
    }

    @Test
    fun liveGravityRejectsTheWrongPose() {
        // Claiming +Z up while +Y is the axis carrying gravity is the classic
        // "picked the wrong face" mistake the wizard has to catch.
        assertFalse(CalibFaces.gravityMatches(4, listOf(0f, 1f, 0f)))
        assertFalse(CalibFaces.gravityMatches(1, listOf(1f, 0f, 0f)))
        // A 30° tilt is still fine, a 60° one is not.
        assertFalse(CalibFaces.gravityMatches(1, listOf(-0.8f, 0.5f, 0f)))
    }

    @Test
    fun liveGravityRejectsUnknownFacesAndShortVectors() {
        assertFalse(CalibFaces.gravityMatches(6, listOf(0f, 0f, 1f)))
        assertFalse(CalibFaces.gravityMatches(0, listOf(1f, 0f)))
    }
}
