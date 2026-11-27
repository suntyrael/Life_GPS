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

    private fun stepRecord(seqId: Long, stepCount: Int): TrajectoryRecord = TrajectoryRecord(
        seqId = seqId,
        timestampSec = 200_000_000L + seqId * 10L,
        subsecondMs = 0,
        latitudeE7 = 312_304_160,
        longitudeE7 = 1_214_737_010,
        altitudeM = 12,
        speedCms = 120,
        headingDeciDeg = 900,
        stepCount = stepCount,
        hdopDeci = 10,
        satellites = 10,
        statusFlags = TrajectoryRecord.FLAG_FIX_VALID,
        batteryLevel = 85,
        floorLevel = 0,
    )

    @Test
    fun computeSummaryDeltaStepsHandlesNormalRebootAndRollover() {
        // 1. Normal monotonic steps (starting at 500 carried over from prior day)
        val normalRecs = listOf(
            stepRecord(1L, 500),
            stepRecord(2L, 540),
            stepRecord(3L, 625),
            stepRecord(4L, 625),
            stepRecord(5L, 700),
        )
        assertEquals(200, LocalDayStore.computeSummary(20261126, normalRecs, false).totalSteps)

        // 2. Mid-day device reboot resets step counter to 0 (and then climbs again)
        val rebootRecs = listOf(
            stepRecord(1L, 100),
            stepRecord(2L, 350), // +250
            stepRecord(3L, 0),   // reboot reset (+0)
            stepRecord(4L, 45),  // +45
            stepRecord(5L, 12),  // second reboot with 12 steps already accumulated (+12)
            stepRecord(6L, 62),  // +50
        )
        assertEquals(357, LocalDayStore.computeSummary(20261126, rebootRecs, false).totalSteps)

        // 3. 16-bit counter rollover (65535 -> 10)
        val rolloverRecs = listOf(
            stepRecord(1L, 65500),
            stepRecord(2L, 65535), // +35
            stepRecord(3L, 10),    // +(65536 - 65535) + 10 = +11
            stepRecord(4L, 40),    // +30
        )
        assertEquals(76, LocalDayStore.computeSummary(20261126, rolloverRecs, false).totalSteps)
    }

    @Test
    fun listRawDayFilesAndImportRawFileRejectTraversalAndCorruption() {
        val tmpDir = Files.createTempDirectory("lgps_backup_raw").toFile()
        try {
            val store = LocalDayStore(tmpDir)
            val hsPkg = loadVector("day_package_hs.bin")
            val liveRaw = loadVector("trajectory_basic.bin")

            assertTrue(store.importRawFile("20261126.lgd", hsPkg))
            assertTrue(store.importRawFile("20261127.lgs", liveRaw))

            // Reject traversal, nested paths, wrong extensions, empty, and corrupted archives
            assertFalse(store.importRawFile("../20261128.lgd", hsPkg))
            assertFalse(store.importRawFile("days/20261128.lgd", hsPkg))
            assertFalse(store.importRawFile("20261128.txt", hsPkg))
            assertFalse(store.importRawFile("20261128.lgd", ByteArray(0)))
            val corrupt = hsPkg.copyOf().also { it[10] = (it[10].toInt() xor 0xFF).toByte() }
            assertFalse(store.importRawFile("20261128.lgd", corrupt))

            val listed = store.listRawDayFiles().map { it.name }
            assertEquals(listOf("20261126.lgd", "20261127.lgs"), listed)
            assertEquals(8, store.loadDayRecords(20261126).size)
            assertEquals(3, store.loadDayRecords(20261127).size)
        } finally {
            tmpDir.deleteRecursively()
        }
    }
}
