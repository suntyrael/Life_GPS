package com.lifegps.app.ble

/**
 * Voltage-only state-of-charge estimate for the single-cell Li-ion pack.
 *
 * v1 has no current sense and no fuel gauge, so the firmware sends only `bat_mv`
 * (`soc_pct` stays 0xFF). The tables below are fitted from one 0.2 C cycle of the
 * 1000 mAh-class pack in `1000mAh电池放电.xlsx` (test started 2026-10-19, room
 * temperature was not logged):
 *
 *  - CC discharge: 4.1424 V -> 3.000 V, 1.1106 Ah at ~0.200 A.
 *  - CC-CV charge: 3.2812 V -> 4.2000 V, 1.1102 Ah at ~0.200 A.
 *
 * [OCV_MV] is an open-circuit estimate, not the loaded discharge curve. At equal
 * state of charge, OCV is the midpoint of the charge and discharge terminal
 * voltages, which cancels the IR drop and the symmetric part of the polarization.
 * The 0 % point is the rested voltage after the discharge (3.247 V after 30 min,
 * 3.2205 V after 10 min, so 3.23 V is used); 100 % is the rested voltage after the
 * full charge (4.173 V after 30 min). The README power model puts the device at
 * ~0.02-11.5 mA, far below the 200 mA test, so its terminal voltage is close to
 * OCV; using the 0.2 C loaded curve here would under-read the state of charge.
 * Cross-check: the partial discharge before the full charge started at 3.9821 V
 * under 0.2 A (~4.01 V OCV) and delivered 0.7905 Ah; this table predicts about
 * 0.80 Ah to the 0 % anchor, within ~2 %.
 *
 * [CHARGE_MV] is the measured terminal voltage during the 0.2 C CC charge. It is
 * used only while the charger reports current flowing, because the OCV table
 * would jump to 100 % as soon as the CV phase raises the terminal to 4.2 V. The
 * board charges at ~0.35 A, above the 0.2 C test, so during CC the terminal is a
 * little higher than this table expects and the reported charge percentage reads
 * slightly high; that is acceptable while the device is on external power, and
 * the OCV table takes over once the charger deasserts.
 *
 * Limits: one cell, one cycle, no temperature log, no rate sweep. Do not reuse
 * this table for another cell or for a pack with a different protection board.
 * A real fuel gauge (coulomb counter plus OCV correction) would replace this,
 * not extend it.
 */
object BatterySoc {

    /** Resting/OCV table, measured at 0.2 C and anchored to the post-rest endpoints. */
    private val OCV_MV = intArrayOf(
        3230, 3427, 3489, 3553, 3608, 3653, 3704, 3764, 3809, 3842, 3869,
        3894, 3919, 3952, 3994, 4029, 4049, 4068, 4085, 4113, 4173,
    )

    /** Terminal voltage while charging at 0.2 C. */
    private val CHARGE_MV = intArrayOf(
        3281, 3506, 3558, 3620, 3675, 3718, 3766, 3823, 3866, 3898, 3926,
        3955, 3981, 4014, 4058, 4088, 4105, 4125, 4144, 4182, 4200,
    )

    private val PERCENT = intArrayOf(
        0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
        55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
    )

    /**
     * Convert a pack voltage to percent. [charging] selects the charge terminal
     * curve; otherwise the OCV curve is used. Values outside the fitted range are
     * clamped to 0/100 instead of extrapolated.
     */
    fun percent(mv: Int, charging: Boolean): Int {
        val curveMv = if (charging) CHARGE_MV else OCV_MV
        if (mv <= 0) return 0
        if (mv >= curveMv.last()) return 100
        if (mv <= curveMv.first()) return 0
        for (index in 0 until curveMv.size - 1) {
            val lowMv = curveMv[index]
            val highMv = curveMv[index + 1]
            if (mv in lowMv..highMv) {
                val lowPct = PERCENT[index].toFloat()
                val highPct = PERCENT[index + 1].toFloat()
                val span = (highMv - lowMv).toFloat()
                if (span <= 0f) return lowPct.toInt()
                val ratio = (mv - lowMv) / span
                return (lowPct + ratio * (highPct - lowPct)).toInt().coerceIn(0, 100)
            }
        }
        return 0
    }
}
