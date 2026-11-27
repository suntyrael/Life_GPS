// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.DayPackageCodec
import com.lifegps.core.protocol.TrajectoryRecord
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
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

    @Test
    fun commitArchivedPackagePreservesUncoveredLiveRecordsAndDeletesWhenFullyCovered() {
        val hsPkg = loadVector("day_package_hs.bin")
        val hsRecords = DayPackageCodec.decode(hsPkg).records
        val dayId = LocalDayStore.dayIdFromEpochMs(hsRecords.first().unixEpochMs)

        // Case 1: Live file contains an extra record missing from the archive -> must keep .lgs and merge
        val tmpKeep = Files.createTempDirectory("lgps_keep_lgs").toFile()
        try {
            val store = LocalDayStore(tmpKeep)
            val extraRec = hsRecords.last().copy(seqId = 999L)
            store.appendLiveRecords(hsRecords + extraRec, tzOffsetMinutes = 480)
            assertTrue(store.liveFile(dayId).isFile)

            store.commitArchivedPackage(dayId, hsPkg)
            assertTrue(store.hasArchivedDay(dayId))
            assertTrue("Uncovered live records must preserve .lgs", store.liveFile(dayId).isFile)

            val merged = store.loadDayRecords(dayId)
            assertEquals(hsRecords.size + 1, merged.size)
            assertEquals(999L, merged.last().seqId)
        } finally {
            tmpKeep.deleteRecursively()
        }

        // Case 2: All live records are covered by the archive -> .lgs is cleanly deleted
        val tmpDelete = Files.createTempDirectory("lgps_del_lgs").toFile()
        try {
            val store = LocalDayStore(tmpDelete)
            store.appendLiveRecords(hsRecords, tzOffsetMinutes = 480)
            assertTrue(store.liveFile(dayId).isFile)

            store.commitArchivedPackage(dayId, hsPkg)
            assertTrue(store.hasArchivedDay(dayId))
            assertFalse("Fully covered .lgs must be deleted", store.liveFile(dayId).exists())
            assertEquals(hsRecords.size, store.loadDayRecords(dayId).size)
        } finally {
            tmpDelete.deleteRecursively()
        }
    }

    @Test
    fun hasMatchingArchiveChecksCrc32AndRecordCount() {
        val tmpDir = Files.createTempDirectory("lgps_crc_meta").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val hsPkg = loadVector("day_package_hs.bin")
            val dayId = 20261126

            assertNull(store.readArchivedDayCrc32(dayId))
            assertFalse(store.hasMatchingArchive(dayId, 0xCC798479L, 8L))

            store.commitArchivedPackage(dayId, hsPkg)
            assertEquals(0xCC798479L, store.readArchivedDayCrc32(dayId))
            assertTrue(store.hasMatchingArchive(dayId, 0xCC798479L, 8L))
            assertFalse(store.hasMatchingArchive(dayId, 0x11223344L, 8L))
            assertFalse(store.hasMatchingArchive(dayId, 0xCC798479L, 12L))
        } finally {
            tmpDir.deleteRecursively()
        }
    }
}
