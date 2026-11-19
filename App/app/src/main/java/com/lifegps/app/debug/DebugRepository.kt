package com.lifegps.app.debug

import com.lifegps.app.ble.GnssCfgValue
import com.lifegps.app.ble.GnssDebugState
import kotlinx.coroutines.flow.StateFlow

/**
 * UI-facing contract of the debug data source.
 *
 * The debug screens only depend on this interface; the BLE implementation lives
 * in `com.lifegps.app.ble.BleDebugRepository` and is wired in `LifeGpsApp`.
 * Every flow reflects data actually received from the device (GATT notification
 * or command response) — there is no synthetic telemetry behind this API.
 */
interface DebugRepository {
    val device: StateFlow<DeviceSummary>
    val gnss: StateFlow<GnssStatus>

    /** GNSS receive-path counters (telemetry group 0x01); the solution stays in
     *  [gnss]. [GnssLinkInfo.received] is false until the first packet arrives.
     */
    val gnssLink: StateFlow<GnssLinkInfo>
    val imu: StateFlow<ImuStatus>
    val mag: StateFlow<MagStatus>
    val baro: StateFlow<BaroStatus>

    /** Attitude from the firmware filter; [AttitudeState.valid] is false until the
     *  first IMU telemetry packet arrives.
     */
    val attitude: StateFlow<AttitudeState>
    val flash: StateFlow<FlashStatus>
    val checks: StateFlow<List<CheckItem>>
    val checkRunning: StateFlow<Boolean>
    val logs: StateFlow<List<LogEntry>>
    val console: StateFlow<List<ConsoleEntry>>
    val devices: StateFlow<List<ScannedDevice>>
    val scanning: StateFlow<Boolean>

    /** Map trail: GNSS fixes sampled while live tracking is enabled. */
    val geoTrail: StateFlow<List<GeoPoint>>
    val liveTracking: StateFlow<Boolean>

    /** Local PDR displacement estimate (see BleDebugRepository for the caveat). */
    val trail: StateFlow<List<TrailPoint>>
    val trailRunning: StateFlow<Boolean>

    val pois: StateFlow<List<PoiMarker>>

    /** Sensor calibration state; the firmware owns it, the App only mirrors it. */
    val calib: StateFlow<CalibState>

    // --- GNSS debugging (0x36 session, 0x35 settings, 0x08/0x09 telemetry) ----

    /** Last known debug session; [GnssDebugState.flags] == 0 means "off". */
    val gnssDebug: StateFlow<GnssDebugState>

    /** Raw NMEA sentences, newest last, bounded by the implementation. */
    val nmea: StateFlow<List<String>>

    /** Whitelisted receiver settings, keyed by configuration key. */
    val gnssCfg: StateFlow<Map<Long, GnssCfgValue>>

    /** Enables/changes the debug session; [ttlSeconds] == 0 keeps the default. */
    fun setGnssDebug(flags: Int, ttlSeconds: Int)

    /** Re-reads the session state from the device. */
    fun refreshGnssDebug()

    fun clearNmea()

    /** Re-reads every whitelisted setting (one command each). */
    fun refreshGnssCfg()

    /**
     * Writes one setting and stores the value the receiver reports afterwards.
     * [persistToFlash] writes the receiver's flash layer, which costs an erase
     * cycle, so the UI asks for it explicitly.
     */
    fun setGnssCfg(key: Long, value: Long, persistToFlash: Boolean = false)

    fun runSelfTest()
    fun connect(device: ScannedDevice)
    fun disconnect()
    fun scan()
    fun markPoi()
    fun addPoi(latitude: Double, longitude: Double, note: String = "")
    fun removePoi(id: Int)
    fun setLiveTracking(enabled: Boolean)
    fun clearGeoTrail()
    fun setTrailRunning(enabled: Boolean)
    fun clearTrail()

    /** Queues a command frame; the returned entry is updated with the real response. */
    fun sendCommand(opcode: String, payload: String): ConsoleEntry

    // --- calibration (MAG_CAL 0x41 / ACCEL_CAL 0x43) -------------------------

    /**
     * Re-syncs both calibration states from the device. Called when the wizard
     * opens and after a reconnect: the session may already be collecting because
     * the firmware keeps running while the App was in the background.
     */
    fun refreshCalibration()

    fun startMagCal()

    /** Computes the coefficients and writes the calibration record. */
    fun finishMagCal()

    /** Aborts a running capture; the stored record is left untouched. */
    fun cancelMagCal()

    fun clearMagCal()

    /**
     * Captures the pose the device is resting in right now. The face is identified
     * on the device from the gravity vector - the user never selects a face, they
     * just place the device on another side and press again.
     */
    fun captureAccelPose()

    /** Single-face quick mode: low accuracy, only usable for one pose. */
    fun captureAccelQuick()

    fun cancelAccelFace()

    fun finishAccelCal()

    fun clearAccelCal()

    // --- barometric baseline (BARO_BASELINE 0x42) ----------------------------

    /**
     * Re-renders [baro] from the last `0x04` packet. No BLE traffic: the telemetry
     * stream is the source of truth and `0x42` action 0 has a side effect (it
     * freezes the current pressure), so it must never be used as a query.
     */
    suspend fun refreshBaroBaseline(): BaroBaselineResult

    /** Freezes the pressure the device measures right now as the baseline. */
    suspend fun takeBaroBaseline(): BaroBaselineResult

    /** Sets the baseline to [paQ4] (Pa × 16), validated against 30000..125000 Pa. */
    suspend fun setBaroBaselineManual(paQ4: Long): BaroBaselineResult

    fun clearLogs()
    fun clearConsole()
    fun exportLogs()
    fun addLog(level: LogLevel, source: LogSource, tag: String, message: String)
}
