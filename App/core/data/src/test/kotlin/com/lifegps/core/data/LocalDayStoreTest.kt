// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.TrajectoryRecord
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LocalDayStoreTest {

    private fun loadVector(name: String): ByteArray {
        val stream = requireNotNull(javaClass.classLoader?.getResourceAsStream(name)) {
            "Missing vector $name"
        }
        return stream.use { it.readBytes() }
    }

    @Test
    fun appendLiveRecordsDeduplicatesAndComputesSummary() {
        val tmpDir = Files.createTempDirectory("lgps_store").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val recs = TrajectoryRecord.decodeStream(loadVector("trajectory_basic.bin"))
            val touched1 = store.appendLiveRecords(recs, tzOffsetMinutes = 480)
            assertEquals(1, touched1.size)
            val dayId = touched1.first()

            // Re-appending same seqIds must be idempotent
            val touched2 = store.appendLiveRecords(recs, tzOffsetMinutes = 480)
            assertTrue(touched2.isEmpty())

            val loaded = store.loadDayRecords(dayId)
            assertEquals(3, loaded.size)
            val summary = store.summarizeDay(dayId)
            assertNotNull(summary)
            assertEquals(3, summary!!.recordCount)
            assertFalse(summary.isArchived)
            assertTrue(summary.poiCount >= 1)
        } finally {
            tmpDir.deleteRecursively()
        }
    }

    @Test
    fun commitArchivedGoldenPackageSupersedesLiveAndQuarantinesCorrupt() {
        val tmpDir = Files.createTempDirectory("lgps_arch").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val hsPkg = loadVector("day_package_hs.bin")
            val decoded = store.commitArchivedPackage(20261126, hsPkg)
            assertEquals(8, decoded.records.size)
            assertTrue(store.hasArchivedDay(20261126))
            assertEquals(8, store.loadDayRecords(20261126).size)

            // Corrupted copy must fail and be quarantined
            val bad = hsPkg.copyOf()
            bad[10] = (bad[10].toInt() xor 0xFF).toByte()
            var threw = false
            try {
                store.commitArchivedPackage(20260706, bad)
            } catch (_: Throwable) {
                threw = true
            }
            assertTrue(threw)
            assertEquals(1, store.quarantineFiles().size)
        } finally {
            tmpDir.deleteRecursively()
        }
    }
}
