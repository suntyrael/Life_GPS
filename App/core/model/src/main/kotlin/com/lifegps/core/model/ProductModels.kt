// SPDX-License-Identifier: MIT
package com.lifegps.core.model

enum class CoordSystem {
    WGS84,
    GCJ02,
}

enum class DeviceOperatingMode(val wireCode: Int, val label: String) {
    DEEP_SLEEP(0, "深度休眠"),
    ACTIVE(1, "标准记录"),
    HIGH_PRECISION(2, "高精度"),
    ;

    companion object {
        fun fromWire(code: Int): DeviceOperatingMode =
            entries.firstOrNull { it.wireCode == code } ?: ACTIVE
    }
}

enum class BondState(val label: String) {
    NONE("未配对"),
    BONDING("配对中"),
    BONDED("已配对"),
}

data class LatLng(
    val latitude: Double,
    val longitude: Double,
)

data class DayTrackSummary(
    val dayId: Int,
    val recordCount: Int,
    val distanceMeters: Double,
    val totalSteps: Int,
    val poiCount: Int,
    val isArchived: Boolean,
    val minAltM: Int,
    val maxAltM: Int,
)

data class PoiRecord(
    val id: Long,
    val dayId: Int,
    val seqId: Long,
    val timestampMs: Long,
    val latitude: Double,
    val longitude: Double,
    val altitudeM: Int,
    val floorLevel: Int,
    val title: String,
    val note: String = "",
)

data class SyncProgress(
    val running: Boolean = false,
    val stage: String = "",
    val receivedBytes: Long = 0L,
    val totalBytes: Long = 0L,
    val syncedRecords: Int = 0,
    val lastSyncEpochMs: Long = 0L,
    val error: String? = null,
)
