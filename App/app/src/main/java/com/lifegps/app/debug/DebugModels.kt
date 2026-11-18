package com.lifegps.app.debug

enum class LogLevel(val short: String) {
    DEBUG("D"),
    INFO("I"),
    WARN("W"),
    ERROR("E"),
}

enum class LogSource(val label: String) {
    BLE("BLE"),
    USB("USB"),
}

data class LogEntry(
    val time: String,
    val level: LogLevel,
    val source: LogSource,
    val tag: String,
    val message: String,
)

data class DeviceSummary(
    val name: String,
    val connected: Boolean,
    val firmware: String,
    val protocol: String,
    val batteryPct: Int,
    val batteryMv: Int,
    val charging: Boolean,
    val mode: String,
    val uptimeSeconds: Long,
)

data class GnssStatus(
    val state: String,
    val fix: Boolean,
    val sats: Int,
    val hdop: Double,
    val latitude: Double,
    val longitude: Double,
    val altitudeM: Double,
    val speedKmh: Double,
    val headingDeg: Double,
    val utcTime: String,
    val ttff: String,
    val satellites: List<SatelliteInfo>,
)

data class SatelliteInfo(
    val svid: Int,
    val system: String,
    val cno: Int,
    val elevation: Int,
    val azimuth: Int,
    val used: Boolean,
)

data class ImuStatus(
    val ax: Float,
    val ay: Float,
    val az: Float,
    val gx: Float,
    val gy: Float,
    val gz: Float,
    val qw: Float,
    val qx: Float,
    val qy: Float,
    val qz: Float,
    val steps: Int,
    val zupt: Boolean,
)

data class MagStatus(
    val mx: Float,
    val my: Float,
    val mz: Float,
    val norm: Float,
    val anomaly: Boolean,
)

data class BaroStatus(
    val pressureHpa: Float,
    val temperatureC: Float,
    val altitudeM: Float,
    val floor: Int,
)

data class FlashStatus(
    val jedec: String,
    val usedMb: Float,
    val totalMb: Float,
    val recordCount: Int,
    val beaconCount: Int,
    val journal: String,
    val sectors: List<String>,
)

enum class CheckState {
    PASS,
    FAIL,
    PENDING,
    RUNNING,
}

data class CheckItem(
    val name: String,
    val detail: String,
    val state: CheckState,
)

data class ConsoleEntry(
    val time: String,
    val opcode: String,
    val request: String,
    val response: String,
)

/**
 * A BLE peripheral seen during a scan.
 *
 * Only fields that Android actually reports are kept: the broadcast name (may
 * be blank until the scan response arrives), the MAC address used to connect
 * and the last RSSI. Firmware revision is not part of advertising, so it is not
 * stored here.
 */
data class ScannedDevice(
    val name: String,
    val address: String,
    val rssi: Int,
)

data class GeoPoint(
    val latitude: Double,
    val longitude: Double,
    val tick: Long,
)

/**
 * One dead-reckoned trail point.
 *
 * [x]/[y] are metres east/north from the PDR packet; [qw]..[qz] and [headingDeg]
 * are the attitude the firmware estimated when the step completed, so the 3D view
 * can draw an oriented marker per point instead of a bare polyline.
 */
data class TrailPoint(
    val x: Float,
    val y: Float,
    val z: Float,
    val tick: Long,
    val qw: Float = 1f,
    val qx: Float = 0f,
    val qy: Float = 0f,
    val qz: Float = 0f,
    val headingDeg: Float = 0f,
)

/**
 * Attitude from the firmware filter (IMU telemetry packet).
 *
 * Angles follow the firmware convention: roll positive when the right side goes
 * down, pitch positive nose-up, heading clockwise from north. Zeros with
 * [valid] == false mean "no telemetry yet", never "level".
 */
data class AttitudeState(
    val qw: Float = 0f,
    val qx: Float = 0f,
    val qy: Float = 0f,
    val qz: Float = 0f,
    val rollDeg: Float = 0f,
    val pitchDeg: Float = 0f,
    val headingDeg: Float = 0f,
    val samples: Long = 0L,
    val valid: Boolean = false,
)

data class PoiMarker(
    val id: Int,
    val latitude: Double,
    val longitude: Double,
    val label: String,
    val note: String,
    val time: String,
)
