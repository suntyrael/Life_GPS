// SPDX-License-Identifier: MIT
package com.lifegps.app.data

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import com.lifegps.core.model.DayTrackSummary
import com.lifegps.core.model.PoiRecord

/**
 * Local SQLite (WAL mode) metadata index backing fast timeline browsing,
 * user-editable POI titles/notes, and resumable sync watermarks (`last_synced_seq_id`).
 *
 * Raw trajectory bytes live in binary `.lgd` / `.lgs` files managed by [com.lifegps.core.data.LocalDayStore];
 * this index can be rebuilt cleanly from disk at any time.
 */
class TrackIndexDb(context: Context) : SQLiteOpenHelper(
    context.applicationContext,
    DB_NAME,
    null,
    DB_VERSION,
) {
    init {
        setWriteAheadLoggingEnabled(true)
    }

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS day_summaries (
                day_id INTEGER PRIMARY KEY,
                record_count INTEGER NOT NULL,
                distance_m REAL NOT NULL,
                total_steps INTEGER NOT NULL,
                poi_count INTEGER NOT NULL,
                is_archived INTEGER NOT NULL,
                min_alt_m INTEGER NOT NULL,
                max_alt_m INTEGER NOT NULL
            )
            """.trimIndent(),
        )
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS pois (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                day_id INTEGER NOT NULL,
                seq_id INTEGER NOT NULL,
                timestamp_ms INTEGER NOT NULL,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                altitude_m INTEGER NOT NULL,
                floor_level INTEGER NOT NULL,
                title TEXT NOT NULL,
                note TEXT NOT NULL,
                UNIQUE(day_id, seq_id)
            )
            """.trimIndent(),
        )
        db.execSQL(
            """
            CREATE TABLE IF NOT EXISTS sync_meta (
                key TEXT PRIMARY KEY,
                val TEXT NOT NULL
            )
            """.trimIndent(),
        )
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) = Unit

    fun upsertDaySummary(summary: DayTrackSummary) {
        val cv = ContentValues().apply {
            put("day_id", summary.dayId)
            put("record_count", summary.recordCount)
            put("distance_m", summary.distanceMeters)
            put("total_steps", summary.totalSteps)
            put("poi_count", summary.poiCount)
            put("is_archived", if (summary.isArchived) 1 else 0)
            put("min_alt_m", summary.minAltM)
            put("max_alt_m", summary.maxAltM)
        }
        writableDatabase.insertWithOnConflict("day_summaries", null, cv, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun deleteDaySummary(dayId: Int) {
        writableDatabase.delete("day_summaries", "day_id = ?", arrayOf(dayId.toString()))
        writableDatabase.delete("pois", "day_id = ?", arrayOf(dayId.toString()))
    }

    fun listDaySummaries(): List<DayTrackSummary> {
        val list = ArrayList<DayTrackSummary>()
        readableDatabase.rawQuery(
            "SELECT day_id, record_count, distance_m, total_steps, poi_count, is_archived, min_alt_m, max_alt_m FROM day_summaries ORDER BY day_id DESC",
            null,
        ).use { c ->
            while (c.moveToNext()) {
                list.add(
                    DayTrackSummary(
                        dayId = c.getInt(0),
                        recordCount = c.getInt(1),
                        distanceMeters = c.getDouble(2),
                        totalSteps = c.getInt(3),
                        poiCount = c.getInt(4),
                        isArchived = c.getInt(5) != 0,
                        minAltM = c.getInt(6),
                        maxAltM = c.getInt(7),
                    ),
                )
            }
        }
        return list
    }

    fun upsertPoi(poi: PoiRecord) {
        val existingTitle = findPoiTitleNote(poi.dayId, poi.seqId)
        val cv = ContentValues().apply {
            put("day_id", poi.dayId)
            put("seq_id", poi.seqId)
            put("timestamp_ms", poi.timestampMs)
            put("latitude", poi.latitude)
            put("longitude", poi.longitude)
            put("altitude_m", poi.altitudeM)
            put("floor_level", poi.floorLevel)
            put("title", existingTitle?.first?.ifBlank { poi.title } ?: poi.title)
            put("note", existingTitle?.second ?: poi.note)
        }
        writableDatabase.insertWithOnConflict("pois", null, cv, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun updatePoiText(id: Long, title: String, note: String) {
        val cv = ContentValues().apply {
            put("title", title)
            put("note", note)
        }
        writableDatabase.update("pois", cv, "id = ?", arrayOf(id.toString()))
    }

    fun deletePoi(id: Long) {
        writableDatabase.delete("pois", "id = ?", arrayOf(id.toString()))
    }

    fun listPois(dayId: Int? = null): List<PoiRecord> {
        val list = ArrayList<PoiRecord>()
        val sql = if (dayId == null) {
            "SELECT id, day_id, seq_id, timestamp_ms, latitude, longitude, altitude_m, floor_level, title, note FROM pois ORDER BY timestamp_ms DESC"
        } else {
            "SELECT id, day_id, seq_id, timestamp_ms, latitude, longitude, altitude_m, floor_level, title, note FROM pois WHERE day_id = ? ORDER BY timestamp_ms ASC"
        }
        val args = dayId?.let { arrayOf(it.toString()) }
        readableDatabase.rawQuery(sql, args).use { c ->
            while (c.moveToNext()) {
                list.add(
                    PoiRecord(
                        id = c.getLong(0),
                        dayId = c.getInt(1),
                        seqId = c.getLong(2),
                        timestampMs = c.getLong(3),
                        latitude = c.getDouble(4),
                        longitude = c.getDouble(5),
                        altitudeM = c.getInt(6),
                        floorLevel = c.getInt(7),
                        title = c.getString(8),
                        note = c.getString(9),
                    ),
                )
            }
        }
        return list
    }

    fun getLastSyncedSeqId(): Long = getMeta(KEY_LAST_SEQ)?.toLongOrNull() ?: 0L

    fun setLastSyncedSeqId(seqId: Long) = putMeta(KEY_LAST_SEQ, seqId.toString())

    fun getMeta(key: String): String? {
        readableDatabase.rawQuery(
            "SELECT val FROM sync_meta WHERE key = ?",
            arrayOf(key),
        ).use { c ->
            return if (c.moveToFirst()) c.getString(0) else null
        }
    }

    fun putMeta(key: String, value: String) {
        val cv = ContentValues().apply {
            put("key", key)
            put("val", value)
        }
        writableDatabase.insertWithOnConflict("sync_meta", null, cv, SQLiteDatabase.CONFLICT_REPLACE)
    }

    private fun findPoiTitleNote(dayId: Int, seqId: Long): Pair<String, String>? {
        readableDatabase.rawQuery(
            "SELECT title, note FROM pois WHERE day_id = ? AND seq_id = ?",
            arrayOf(dayId.toString(), seqId.toString()),
        ).use { c ->
            return if (c.moveToFirst()) c.getString(0) to c.getString(1) else null
        }
    }

    companion object {
        private const val DB_NAME = "lifegps_index.db"
        private const val DB_VERSION = 1
        private const val KEY_LAST_SEQ = "last_synced_seq_id"
    }
}
