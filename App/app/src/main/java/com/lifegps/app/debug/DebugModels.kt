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
    /** NAV-PVT numSV: the satellites that are actually used in the solution. */
    val sats: Int,
    /**
     * pDOP x100 from UBX-NAV-PVT (`/100`), 0.0 when the receiver reports 99.99
     * (= no solution). It is **not** hDOP: the firmware never requests
     * UBX-NAV-DOP, so hDOP/vDOP are simply not on the wire.
     */
    val pdop: Double,
    val latitude: Double,
    val longitude: Double,
    /** Height above mean sea level (NAV-PVT hMSL). The ellipsoid height is not on
     *  the wire, which is why GPSTest's `Alt:` row stays empty. */
    val altitudeM: Double,
    val speedKmh: Double,
    val headingDeg: Double,
    /** `HH:mm:ss` in UTC (the device has no sub-second time on the wire). */
    val utcTime: String,
    /** GPSTest shape: `N sec`, empty until the first fix. */
    val ttff: String,
    val satellites: List<SatelliteInfo>,
    /** NAV-SAT `total`: every satellite the receiver knows about, not only the
     *  ones that fit in one packet (`satellites.size` <= 32). */
    val satTotal: Int = 0,
    /**
     * v1.2 Status block (offsets 100..115): ellipsoid height, the four 1-sigma
     * accuracies and the hDOP/vDOP pair. False on a 0.3.0 firmware, and then every
     * value below stays 0 and the UI prints an empty cell instead of "0.0 m".
     *
     * 0 inside the block also means "the receiver did not report it".
     */
    val hasAccuracy: Boolean = false,
    /** Ellipsoid height (HAE, NAV-PVT `height`), the value GPSTest calls `Alt`. */
    val ellipsoidAltM: Double = 0.0,
    val hAccM: Double = 0.0,
    val vAccM: Double = 0.0,
    val sAccMs: Double = 0.0,
    val bAccDeg: Double = 0.0,
    /** From UBX-NAV-DOP, not derivable from pDOP. 0.0 = not reported. */
    val hdop: Double = 0.0,
    val vdop: Double = 0.0,
)

data class SatelliteInfo(
    val svid: Int,
    val system: String,
    val cno: Int,
    val elevation: Int,
    /** -1 when the receiver does not report an azimuth for this satellite. */
    val azimuth: Int = -1,
    /** GPSTest `CF`: L1 / L2 / L5 / E1 / E5a / B1I / B1C / G1 ...; empty when unknown. */
    val carrier: String = "",
    val used: Boolean = false,
    /** NAV-SAT flags bit 11 / bit 12: the table's `E` and `A` columns. */
    val ephAvail: Boolean = false,
    val almAvail: Boolean = false,
    /** UBX-NAV-SAT `qualityInd` rendered as words: 搜索中 / 已捕获 / 跟踪 / 不可用. */
    val tracking: String = "",
)

/**
 * GNSS receive-path counters from the `0x01` telemetry group.
 *
 * [received] is false until the first packet arrives, so the UI can tell "the
 * firmware does not publish this" from "it publishes zero here".
 */
data class GnssLinkInfo(
    val received: Boolean = false,
    val state: String = "OFF",
    val fixType: Int = 0,
    val satellites: Int = 0,
    val fixValid: Boolean = false,
    val utcValid: Boolean = false,
    val ageMs: Long = 0,
    val rxBytes: Long = 0,
    val ubxFrames: Long = 0,
    val badChecksum: Long = 0,
    val nmeaStarts: Long = 0,
    val rxDropped: Long = 0,
    val baud: Int = 0,
    /** Hot/cold-start context for the TTFF cell; GPSTest has no equivalent field. */
    val ttffNote: String = "",
) {
    /**
     * One-line verdict on the receive path, derived only from the counters.
     *
     * The order matters: a silent module makes every later test meaningless, and
     * "NMEA only" is the shape of a receiver that ignored the configuration.
     */
    val verdict: String
        get() = when {
            !received -> "等待遥测"
            rxBytes == 0L -> "模块没说话（rx=0）：检查 GPS_EN / UART 引脚"
            badChecksum > 0 && ubxFrames == 0L -> "全是坏帧：波特率或接线问题"
            ubxFrames == 0L && nmeaStarts > 0 -> "只收到 NMEA，UBX 未被打开"
            ubxFrames > 0L -> "UBX 在收"
            else -> "收到字节但没有帧同步"
        }
}

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
    /** Status-frame values in the firmware sensor frame (1 Hz). */
    val axSensor: Float,
    val aySensor: Float,
    val azSensor: Float,
    val gxSensor: Float,
    val gySensor: Float,
    val gzSensor: Float,
    val gravityXg: Float,
    val gravityYg: Float,
    val gravityZg: Float,
    val linearXg: Float,
    val linearYg: Float,
    val linearZg: Float,
    val imuTempC: Float,
    val magTempC: Float,
    val gravityValid: Boolean,
    val imuTempValid: Boolean,
    val magTempValid: Boolean,
)

data class MagStatus(
    val mx: Float,
    val my: Float,
    val mz: Float,
    val norm: Float,
    val anomaly: Boolean,
)

/**
 * Barometer state, filled from the `0x04` BARO telemetry packet (1 Hz).
 *
 * The four leading fields are the historical ones; the appended ones carry what
 * the packet makes explicit. [valid] / [tempValid] / [baselineValid] mirror the
 * packet flags, so a zero [pressureHpa] means "no compensated pressure yet" and
 * is drawn as "—" instead of 0.0 hPa. `floor` has no firmware source and stays 0.
 *
 * [relAltM] is the height since the reference was set (0 at that moment);
 * [altMslM] is the absolute altitude above mean sea level and is only meaningful
 * while [mslValid], i.e. after the reference was derived from a GNSS height.
 */
data class BaroStatus(
    val pressureHpa: Float,
    val temperatureC: Float,
    val altitudeM: Float,
    val floor: Int,
    val valid: Boolean = false,
    val tempValid: Boolean = false,
    val baselineHpa: Float = 0f,
    val baselineAuto: Boolean = false,
    val relAltM: Float = 0f,
    val baselineValid: Boolean = false,
    val altMslM: Float = 0f,
    val mslValid: Boolean = false,
)

/**
 * Outcome of one BARO_BASELINE (0x42) interaction, rendered by the wizard.
 *
 * [supported] is false only when the firmware refused the opcode itself
 * (`0x03` NOT_SUPPORTED / `0x02` UNKNOWN_OPCODE): the card then keeps its buttons
 * disabled instead of offering a command that cannot work on this firmware.
 */
data class BaroBaselineResult(
    val ok: Boolean,
    val supported: Boolean,
    val message: String,
)

data class FlashStatus(
    val jedec: String,
    val usedMb: Float,
    val totalMb: Float,
    val recordCount: Int,
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
