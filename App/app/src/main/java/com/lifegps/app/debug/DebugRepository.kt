package com.lifegps.app.debug

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

    fun clearLogs()
    fun clearConsole()
    fun exportLogs()
    fun addLog(level: LogLevel, source: LogSource, tag: String, message: String)
}
