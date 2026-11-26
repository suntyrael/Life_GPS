// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.model.DayTrackSummary
import com.lifegps.core.protocol.DayPackageCodec
import com.lifegps.core.protocol.DecodedDayPackage
import com.lifegps.core.protocol.GeneratedProtocol
import com.lifegps.core.protocol.TrajectoryRecord
import java.io.File
import java.io.FileOutputStream
import java.time.Instant
import java.time.ZoneOffset
import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Local offline-first binary trajectory storage:
 * - `days/<YYYYMMDD>.lgd`: verified archived LGD1 day package (written atomically via `.tmp`)
 * - `days/<YYYYMMDD>.lgs`: live append-only stream of 32-byte WGS-84 trajectory records
 * - `quarantine/`: corrupted packages preserved for diagnosis instead of silently lost
 *
 * Pure JVM (no Android framework dependency) for deterministic CI unit tests.
 */
class LocalDayStore(private val rootDir: File) {

    private val daysDir = File(rootDir, "days")
    private val quarantineDir = File(rootDir, "quarantine")

    init {
        daysDir.mkdirs()
        quarantineDir.mkdirs()
    }

    fun archivedFile(dayId: Int): File = File(daysDir, "$dayId.lgd")
    fun liveFile(dayId: Int): File = File(daysDir, "$dayId.lgs")

    fun hasArchivedDay(dayId: Int): Boolean = archivedFile(dayId).isFile

    /**
     * Append live synced [records] to their local calendar `.lgs` files, deduplicating
     * by `seqId` per day. Returns the set of affected `dayId`s (YYYYMMDD).
     */
    @Synchronized
    fun appendLiveRecords(
        records: List<TrajectoryRecord>,
        tzOffsetMinutes: Int = 480,
    ): Set<Int> {
        if (records.isEmpty()) return emptySet()
        val zone = ZoneOffset.ofTotalSeconds(tzOffsetMinutes * 60)
        val grouped = records.groupBy { rec -> dayIdFromEpochMs(rec.unixEpochMs, zone) }
        val touched = LinkedHashSet<Int>()

        for ((dayId, dayRecs) in grouped) {
            if (hasArchivedDay(dayId)) continue
            val existingSeqs = loadDayRecords(dayId).mapTo(HashSet()) { it.seqId }
            val fresh = dayRecs
                .filter { it.seqId !in existingSeqs }
                .distinctBy { it.seqId }
                .sortedBy { it.seqId }
            if (fresh.isEmpty()) continue

            val file = liveFile(dayId)
            FileOutputStream(file, true).use { fos ->
                for (rec in fresh) {
                    fos.write(rec.encode())
                }
                fos.fd.sync()
            }
            touched.add(dayId)
        }
        return touched
    }

    /**
     * Validate a full LGD1 archive package (header CRC-16 + payload CRC-32 + per-record CRC-8),
     * atomically commit to `days/<dayId>.lgd`, and supersede any `.lgs` live file for that day.
     * On corruption, quarantines the raw bytes into `quarantine/` and throws.
     */
    @Synchronized
    fun commitArchivedPackage(dayId: Int, lgdBytes: ByteArray): DecodedDayPackage {
        val decoded = try {
            DayPackageCodec.decode(lgdBytes)
        } catch (t: Throwable) {
            quarantineBytes(dayId, lgdBytes)
            throw t
        }

        val target = archivedFile(dayId)
        val tmp = File(daysDir, "$dayId.lgd.tmp")
        FileOutputStream(tmp).use { fos ->
            fos.write(lgdBytes)
            fos.fd.sync()
        }
        if (target.exists() && !target.delete()) {
            tmp.delete()
            throw IllegalStateException("Cannot replace existing archive ${target.name}")
        }
        if (!tmp.renameTo(target)) {
            tmp.delete()
            throw IllegalStateException("Atomic rename failed for ${target.name}")
        }
        liveFile(dayId).delete()
        return decoded
    }

    @Synchronized
    fun loadDayRecords(dayId: Int): List<TrajectoryRecord> {
        val lgd = archivedFile(dayId)
        if (lgd.isFile) {
            return DayPackageCodec.decode(lgd.readBytes()).records.sortedBy { it.seqId }
        }
        val lgs = liveFile(dayId)
        if (!lgs.isFile) return emptyList()
        val raw = lgs.readBytes()
        val stride = GeneratedProtocol.TrajectoryRecord.SIZE
        val count = raw.size / stride
        val bySeq = LinkedHashMap<Long, TrajectoryRecord>(count)
        for (i in 0 until count) {
            val rec = TrajectoryRecord.decode(raw, i * stride) ?: continue
            bySeq[rec.seqId] = rec
        }
        return bySeq.values.sortedBy { it.seqId }
    }

    @Synchronized
    fun summarizeDay(dayId: Int): DayTrackSummary? {
        val records = loadDayRecords(dayId)
        if (records.isEmpty()) return null
        return computeSummary(dayId, records, isArchived = hasArchivedDay(dayId))
    }

    @Synchronized
    fun listDays(): List<DayTrackSummary> {
        val ids = LinkedHashSet<Int>()
        daysDir.listFiles()?.forEach { file ->
            val name = file.name
            if (name.endsWith(".lgd") || name.endsWith(".lgs")) {
                name.substringBefore('.').toIntOrNull()?.let { ids.add(it) }
            }
        }
        return ids
            .sortedDescending()
            .mapNotNull { summarizeDay(it) }
    }

    @Synchronized
    fun deleteDay(dayId: Int): Boolean {
        val a = archivedFile(dayId).let { !it.exists() || it.delete() }
        val b = liveFile(dayId).let { !it.exists() || it.delete() }
        return a && b
    }

    fun quarantineFiles(): List<File> =
        quarantineDir.listFiles()?.sortedBy { it.name } ?: emptyList()

    private fun quarantineBytes(dayId: Int, bytes: ByteArray) {
        val dst = File(quarantineDir, "${dayId}_${System.currentTimeMillis()}.lgd")
        dst.writeBytes(bytes)
    }

    companion object {
        fun dayIdFromEpochMs(epochMs: Long, zone: ZoneOffset = ZoneOffset.ofHours(8)): Int {
            val zdt = Instant.ofEpochMilli(epochMs).atOffset(zone)
            return zdt.year * 10_000 + zdt.monthValue * 100 + zdt.dayOfMonth
        }

        fun computeSummary(
            dayId: Int,
            records: List<TrajectoryRecord>,
            isArchived: Boolean,
        ): DayTrackSummary {
            var distM = 0.0
            var prevValid: TrajectoryRecord? = null
            var minAlt = Int.MAX_VALUE
            var maxAlt = Int.MIN_VALUE
            var poiCount = 0

            for (r in records) {
                if (r.isPoi) poiCount++
                if (r.altitudeM < minAlt) minAlt = r.altitudeM
                if (r.altitudeM > maxAlt) maxAlt = r.altitudeM
                if (r.isFixValid) {
                    prevValid?.let { p ->
                        val seg = haversineMeters(p.latitudeDeg, p.longitudeDeg, r.latitudeDeg, r.longitudeDeg)
                        if (seg in 0.5..5000.0) {
                            distM += seg
                        }
                    }
                    prevValid = r
                }
            }

            val firstSteps = records.first().stepCount
            val lastSteps = records.last().stepCount
            val steps = if (lastSteps >= firstSteps) lastSteps - firstSteps else lastSteps

            return DayTrackSummary(
                dayId = dayId,
                recordCount = records.size,
                distanceMeters = distM,
                totalSteps = steps,
                poiCount = poiCount,
                isArchived = isArchived,
                minAltM = if (minAlt == Int.MAX_VALUE) 0 else minAlt,
                maxAltM = if (maxAlt == Int.MIN_VALUE) 0 else maxAlt,
            )
        }

        fun haversineMeters(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
            val r = 6_371_000.0
            val dLat = Math.toRadians(lat2 - lat1)
            val dLon = Math.toRadians(lon2 - lon1)
            val a = sin(dLat / 2.0).pow(2) +
                cos(Math.toRadians(lat1)) * cos(Math.toRadians(lat2)) * sin(dLon / 2.0).pow(2)
            return 2.0 * r * asin(sqrt(a.coerceIn(0.0, 1.0)))
        }
    }
}
