// SPDX-License-Identifier: MIT
package com.lifegps.core.model

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Display-only coordinate converter between WGS-84 and GCJ-02.
 *
 * Internal storage, `.lgd` / `.lgs` files, Room database, and GPX/KML/GeoJSON exports
 * ALWAYS remain strictly WGS-84. Only map rendering overlays may convert to GCJ-02
 * when the user selects a GCJ-02 tile source.
 */
object CoordTransform {

    private const val A = 6378245.0
    private const val EE = 0.00669342162296594323

    fun isOutOfChina(lat: Double, lon: Double): Boolean =
        lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271

    fun wgs84ToGcj02(lat: Double, lon: Double): LatLng {
        if (isOutOfChina(lat, lon)) return LatLng(lat, lon)
        val delta = delta(lat, lon)
        return LatLng(lat + delta.latitude, lon + delta.longitude)
    }

    fun gcj02ToWgs84(lat: Double, lon: Double): LatLng {
        if (isOutOfChina(lat, lon)) return LatLng(lat, lon)
        var wgsLat = lat
        var wgsLon = lon
        repeat(6) {
            val gcj = wgs84ToGcj02(wgsLat, wgsLon)
            val dLat = gcj.latitude - lat
            val dLon = gcj.longitude - lon
            wgsLat -= dLat
            wgsLon -= dLon
            if (abs(dLat) < 1e-9 && abs(dLon) < 1e-9) return LatLng(wgsLat, wgsLon)
        }
        return LatLng(wgsLat, wgsLon)
    }

    fun forDisplay(lat: Double, lon: Double, system: CoordSystem): LatLng = when (system) {
        CoordSystem.WGS84 -> LatLng(lat, lon)
        CoordSystem.GCJ02 -> wgs84ToGcj02(lat, lon)
    }

    private fun delta(lat: Double, lon: Double): LatLng {
        var dLat = transformLat(lon - 105.0, lat - 35.0)
        var dLon = transformLon(lon - 105.0, lat - 35.0)
        val radLat = lat / 180.0 * PI
        var magic = sin(radLat)
        magic = 1.0 - EE * magic * magic
        val sqrtMagic = sqrt(magic)
        dLat = (dLat * 180.0) / ((A * (1.0 - EE)) / (magic * sqrtMagic) * PI)
        dLon = (dLon * 180.0) / (A / sqrtMagic * cos(radLat) * PI)
        return LatLng(dLat, dLon)
    }

    private fun transformLat(x: Double, y: Double): Double {
        var ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(abs(x))
        ret += (20.0 * sin(6.0 * x * PI) + 20.0 * sin(2.0 * x * PI)) * 2.0 / 3.0
        ret += (20.0 * sin(y * PI) + 40.0 * sin(y / 3.0 * PI)) * 2.0 / 3.0
        ret += (160.0 * sin(y / 12.0 * PI) + 320.0 * sin(y * PI / 30.0)) * 2.0 / 3.0
        return ret
    }

    private fun transformLon(x: Double, y: Double): Double {
        var ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(abs(x))
        ret += (20.0 * sin(6.0 * x * PI) + 20.0 * sin(2.0 * x * PI)) * 2.0 / 3.0
        ret += (20.0 * sin(x * PI) + 40.0 * sin(x / 3.0 * PI)) * 2.0 / 3.0
        ret += (150.0 * sin(x / 12.0 * PI) + 300.0 * sin(x / 30.0 * PI)) * 2.0 / 3.0
        return ret
    }
}
