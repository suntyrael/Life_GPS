package com.lifegps.app.debug

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * JVM tests for the satellite table order.
 *
 * The order is what stopped the table from jumping: it must be used first, then
 * tracked, then everything else, and inside a group the strongest C/N0 must lead.
 * If this regresses, the table re-shuffles itself on every packet again - a bug
 * that is only visible on a phone under a changing sky, never in a compile.
 */
class SatelliteSortTest {

    private fun sat(
        svid: Int,
        system: String = "GPS",
        cno: Int = 30,
        used: Boolean = false,
        quality: Int = 3,
    ) = SatelliteInfo(
        svid = svid,
        system = system,
        cno = cno,
        elevation = 30,
        used = used,
        quality = quality,
    )

    @Test
    fun usedBeforeTrackedBeforeUnused() {
        val rows = listOf(
            sat(1, cno = 45, used = false, quality = 1),
            sat(2, cno = 20, used = false, quality = 5),
            sat(3, cno = 25, used = true, quality = 5),
        )

        assertEquals(listOf(3, 2, 1), sortSatellites(rows).map { it.svid })
    }

    @Test
    fun strongestCnoLeadsInsideAGroup() {
        val rows = listOf(
            sat(10, cno = 22, used = true),
            sat(11, cno = 44, used = true),
            sat(12, cno = 33, used = true),
        )

        assertEquals(listOf(11, 12, 10), sortSatellites(rows).map { it.svid })
    }

    @Test
    fun equalStrengthKeepsAStableOrder() {
        // Two rows of the same group and the same C/N0: svid decides, and it does
        // not depend on the order the receiver happened to list them in.
        val first = listOf(sat(5, cno = 30), sat(7, cno = 30))
        val second = listOf(sat(7, cno = 30), sat(5, cno = 30))

        assertEquals(listOf(5, 7), sortSatellites(first).map { it.svid })
        assertEquals(listOf(5, 7), sortSatellites(second).map { it.svid })
    }

    @Test
    fun unknownQualityIsNotTracked() {
        // -1 means "the receiver reported no qualityInd"; such a row belongs to the
        // last group, not next to the tracked ones.
        val rows = listOf(
            sat(1, cno = 40, quality = -1),
            sat(2, cno = 12, quality = 5),
        )

        assertEquals(listOf(2, 1), sortSatellites(rows).map { it.svid })
    }
}
