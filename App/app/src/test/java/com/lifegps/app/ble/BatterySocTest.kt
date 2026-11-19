package com.lifegps.app.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JVM tests for the measured voltage -> state-of-charge tables.
 *
 * The tables come from `1000mAh电池放电.xlsx`; these tests pin the interpolation
 * endpoints, monotonicity and the charge/discharge split. They do not replace a
 * bench check against the real pack, but they stop a table edit from silently
 * turning into a non-monotonic or off-by-one curve.
 */
class BatterySocTest {

    @Test
    fun ocvEndpointsClampToEmptyAndFull() {
        assertEquals(0, BatterySoc.percent(2500, charging = false))
        assertEquals(0, BatterySoc.percent(3230, charging = false))
        assertEquals(100, BatterySoc.percent(4173, charging = false))
        assertEquals(100, BatterySoc.percent(4300, charging = false))
    }

    @Test
    fun ocvUsesTheMeasuredCurve() {
        assertEquals(5, BatterySoc.percent(3427, charging = false))
        assertEquals(40, BatterySoc.percent(3809, charging = false))
        assertEquals(50, BatterySoc.percent(3869, charging = false))
        assertEquals(75, BatterySoc.percent(4029, charging = false))
        assertEquals(95, BatterySoc.percent(4113, charging = false))
    }

    @Test
    fun chargingUsesTheChargeCurve() {
        assertEquals(0, BatterySoc.percent(3281, charging = true))
        assertEquals(50, BatterySoc.percent(3926, charging = true))
        assertEquals(100, BatterySoc.percent(4200, charging = true))

        // At the same terminal voltage the pack is less full while charging.
        assertTrue(
            BatterySoc.percent(3900, charging = true) <
                BatterySoc.percent(3900, charging = false),
        )
    }

    @Test
    fun percentIsMonotonicAndBounded() {
        var previous = -1
        for (mv in 2000..4500 step 10) {
            val pct = BatterySoc.percent(mv, charging = false)
            assertTrue("pct $pct out of range at $mv mV", pct in 0..100)
            assertTrue("pct decreased at $mv mV", pct >= previous)
            previous = pct
        }
    }
}
