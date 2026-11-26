// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.TrajectoryRecord
import kotlin.math.cos
import kotlin.math.hypot

/**
 * Douglas-Peucker trajectory polyline simplifier for responsive map overview rendering.
 * Always preserves endpoints and POI-marked records.
 */
object TrackSimplifier {

    fun simplify(
        records: List<TrajectoryRecord>,
        toleranceMeters: Double = 3.0,
    ): List<TrajectoryRecord> {
        if (records.size <= 2 || toleranceMeters <= 0.0) return records

        val keep = BooleanArray(records.size)
        keep[0] = true
        keep[records.lastIndex] = true
        for (i in records.indices) {
            if (records[i].isPoi) keep[i] = true
        }

        simplifyRange(records, 0, records.lastIndex, toleranceMeters, keep)
        val result = ArrayList<TrajectoryRecord>(records.size)
        for (i in records.indices) {
            if (keep[i]) result.add(records[i])
        }
        return result
    }

    private fun simplifyRange(
        records: List<TrajectoryRecord>,
        start: Int,
        end: Int,
        tolM: Double,
        keep: BooleanArray,
    ) {
        if (end <= start + 1) return

        var maxDist = -1.0
        var maxIdx = -1
        val a = records[start]
        val b = records[end]
        for (i in (start + 1) until end) {
            val d = perpendicularMeters(records[i], a, b)
            if (d > maxDist) {
                maxDist = d
                maxIdx = i
            }
        }

        if (maxDist > tolM && maxIdx > start) {
            keep[maxIdx] = true
            simplifyRange(records, start, maxIdx, tolM, keep)
            simplifyRange(records, maxIdx, end, tolM, keep)
        }
    }

    private fun perpendicularMeters(
        p: TrajectoryRecord,
        a: TrajectoryRecord,
        b: TrajectoryRecord,
    ): Double {
        val cosLat = cos(Math.toRadians((a.latitudeDeg + b.latitudeDeg) * 0.5))
        val ax = a.longitudeDeg * 111_320.0 * cosLat
        val ay = a.latitudeDeg * 111_320.0
        val bx = b.longitudeDeg * 111_320.0 * cosLat
        val by = b.latitudeDeg * 111_320.0
        val px = p.longitudeDeg * 111_320.0 * cosLat
        val py = p.latitudeDeg * 111_320.0

        val dx = bx - ax
        val dy = by - ay
        val lenSq = dx * dx + dy * dy
        if (lenSq < 1e-9) return hypot(px - ax, py - ay)

        val t = (((px - ax) * dx + (py - ay) * dy) / lenSq).coerceIn(0.0, 1.0)
        val projX = ax + t * dx
        val projY = ay + t * dy
        return hypot(px - projX, py - projY)
    }
}
