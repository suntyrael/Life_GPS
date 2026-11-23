package com.lifegps.app.ble

import android.content.Context
import android.os.SystemClock
import com.lifegps.app.debug.AccelCalResult
import com.lifegps.app.debug.AttitudeState
import com.lifegps.app.debug.BaroBaselineResult
import com.lifegps.app.debug.BaroStatus
import com.lifegps.app.debug.CalibFaces
import com.lifegps.app.debug.CalibFormat
import com.lifegps.app.debug.CalibJudge
import com.lifegps.app.debug.CalibPhase
import com.lifegps.app.debug.CalibState
import com.lifegps.app.debug.CheckItem
import com.lifegps.app.debug.CheckState
import com.lifegps.app.debug.ConsoleEntry
import com.lifegps.app.debug.DebugRepository
import com.lifegps.app.debug.DeviceSummary
import com.lifegps.app.debug.FlashStatus
import com.lifegps.app.debug.GeoPoint
import com.lifegps.app.debug.GnssLinkInfo
import com.lifegps.app.debug.GnssRestartState
import com.lifegps.app.debug.GnssStatus
import com.lifegps.app.debug.ImuStatus
import com.lifegps.app.debug.LogEntry
import com.lifegps.app.debug.LogLevel
import com.lifegps.app.debug.LogSource
import com.lifegps.app.debug.MagCalResult
import com.lifegps.app.debug.MagStatus
import com.lifegps.app.debug.PoiMarker
import com.lifegps.app.debug.SatelliteInfo
import com.lifegps.app.debug.ScannedDevice
import com.lifegps.app.debug.TrailPoint
import com.lifegps.app.debug.sortSatellites
import java.io.File
import java.time.Instant
import java.time.LocalTime
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.math.asin
import kotlin.math.atan2
import kotlin.math.sqrt
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Debug data source backed by the real GATT link.
 *
 * Every number exposed to the UI is derived from a Status notification, a
 * command response or a Log notification. When the firmware does not publish a
 * quantity (quaternions, hPa, per-satellite C/N0, TTFF) the corresponding field
 * stays zero / empty and the screens render "—", never a substitute value.
 */
class BleDebugRepository(context: Context) : DebugRepository {

    private val transport = BleTransport(context)
    private val appContext: Context = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val clock = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")

    /** Last UTC the device reported (Life_GPS epoch seconds); 0 = never synced. */
    @Volatile
    private var clockSyncEpochS = 0L

    /** [android.os.SystemClock.elapsedRealtime] at the moment of that report. */
    @Volatile
    private var clockSyncAtMs = 0L

    @Volatile
    private var permissionsGranted = false

    @Volatile
    private var capsInfo: CapsInfo? = null

    @Volatile
    private var flashInfo: FlashInfo? = null

    @Volatile
    private var lastStatus: StatusFrame? = null

    private var lastLink: LinkState = LinkState.DISCONNECTED
    private var wasScanning = false
    private var handshakeJob: Job? = null

    /** Last `0x04` BARO packet; [refreshBaroBaseline] re-renders [baro] from it. */
    @Volatile
    private var lastBaro: BaroTelemetry? = null

    private var incompatibleReported = false

    // Latest telemetry: the firmware owns the attitude filter and the PDR, this
    // side only renders what it publishes.
    /** TTFF as text, refreshed by telemetry 0x01 at 1 Hz and kept across Status frames. */
    private var latestTtff = ""

    private var latestQw = 1f
    private var latestQx = 0f
    private var latestQy = 0f
    private var latestQz = 0f
    private var latestHeadingDeg = 0f
    private var attitudeSamples = 0L
    private var lastAttitudeEmitMs = 0L

    // Command sequencing / response matching.
    private val pendingLock = Any()
    private val pending = mutableMapOf<Int, CompletableDeferred<CommandResult>>()
    private var sequence = 0

    private val _device = MutableStateFlow(initialDevice())
    override val device: StateFlow<DeviceSummary> = _device.asStateFlow()

    private val _gnss = MutableStateFlow(initialGnss())
    override val gnss: StateFlow<GnssStatus> = _gnss.asStateFlow()

    private val _gnssLink = MutableStateFlow(GnssLinkInfo())
    override val gnssLink: StateFlow<GnssLinkInfo> = _gnssLink.asStateFlow()

    private val _gnssDebug = MutableStateFlow(GnssDebugState())
    override val gnssDebug: StateFlow<GnssDebugState> = _gnssDebug.asStateFlow()

    private val _gnssRestart = MutableStateFlow(GnssRestartState())
    override val gnssRestart: StateFlow<GnssRestartState> = _gnssRestart.asStateFlow()

    private val _nmea = MutableStateFlow<List<String>>(emptyList())
    override val nmea: StateFlow<List<String>> = _nmea.asStateFlow()

    private val _gnssCfg = MutableStateFlow<Map<Long, GnssCfgValue>>(emptyMap())
    override val gnssCfg: StateFlow<Map<Long, GnssCfgValue>> = _gnssCfg.asStateFlow()

    private val _imu = MutableStateFlow(initialImu())
    override val imu: StateFlow<ImuStatus> = _imu.asStateFlow()

    private val _mag = MutableStateFlow(initialMag())
    override val mag: StateFlow<MagStatus> = _mag.asStateFlow()

    private val _baro = MutableStateFlow(initialBaro())
    override val baro: StateFlow<BaroStatus> = _baro.asStateFlow()

    private val _flash = MutableStateFlow(initialFlash())
    override val flash: StateFlow<FlashStatus> = _flash.asStateFlow()

    private val _attitude = MutableStateFlow(AttitudeState())
    override val attitude: StateFlow<AttitudeState> = _attitude.asStateFlow()

    private val _checks = MutableStateFlow(pendingChecks())
    override val checks: StateFlow<List<CheckItem>> = _checks.asStateFlow()

    private val _checkRunning = MutableStateFlow(false)
    override val checkRunning: StateFlow<Boolean> = _checkRunning.asStateFlow()

    private val _logs = MutableStateFlow<List<LogEntry>>(emptyList())
    override val logs: StateFlow<List<LogEntry>> = _logs.asStateFlow()

    private val _console = MutableStateFlow<List<ConsoleEntry>>(emptyList())
    override val console: StateFlow<List<ConsoleEntry>> = _console.asStateFlow()

    private val _devices = MutableStateFlow<List<ScannedDevice>>(emptyList())
    override val devices: StateFlow<List<ScannedDevice>> = _devices.asStateFlow()

    private val _scanning = MutableStateFlow(false)
    override val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    private val _geoTrail = MutableStateFlow<List<GeoPoint>>(emptyList())
    override val geoTrail: StateFlow<List<GeoPoint>> = _geoTrail.asStateFlow()

    private val _liveTracking = MutableStateFlow(true)
    override val liveTracking: StateFlow<Boolean> = _liveTracking.asStateFlow()

    private val _trail = MutableStateFlow<List<TrailPoint>>(emptyList())
    override val trail: StateFlow<List<TrailPoint>> = _trail.asStateFlow()

    private val _trailRunning = MutableStateFlow(true)
    override val trailRunning: StateFlow<Boolean> = _trailRunning.asStateFlow()

    private val _pois = MutableStateFlow<List<PoiMarker>>(emptyList())
    override val pois: StateFlow<List<PoiMarker>> = _pois.asStateFlow()

    private val _calib = MutableStateFlow(CalibState())
    override val calib: StateFlow<CalibState> = _calib.asStateFlow()

    /** Polls QUERY while a capture runs; the firmware owns the progress counter. */
    private var calibPollJob: Job? = null

    private var nextPoiId = 1

    init {
        transport.onStatus = { bytes -> handleStatusBytes(bytes) }
        transport.onLog = { bytes -> handleLogPacket(bytes) }
        transport.onFrame = { bytes -> handleResponseFrame(bytes) }
        transport.onTelemetry = { bytes -> handleTelemetry(bytes) }

        scope.launch {
            transport.state.collect { link -> handleLinkState(link) }
        }
        scope.launch {
            transport.scanning.collect { active ->
                _scanning.value = active
                if (wasScanning && !active) {
                    addLog(LogLevel.INFO, LogSource.BLE, "SCAN", "扫描结束，发现 ${_devices.value.size} 个设备")
                }
                wasScanning = active
            }
        }
        scope.launch {
            transport.scanError.collect { message ->
                if (message != null) {
                    addLog(LogLevel.ERROR, LogSource.BLE, "SCAN", message)
                }
            }
        }
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "APP",
            "调试数据源已切换为真实 BLE GATT（Service ${BleUuids.SERVICE}）",
        )
    }

    /** Called by the UI once the runtime permission gate allows scanning. */
    fun setPermissionsGranted(granted: Boolean) {
        if (permissionsGranted == granted) return
        permissionsGranted = granted
        if (granted) {
            addLog(LogLevel.INFO, LogSource.BLE, "PERM", "蓝牙权限已授予")
        } else {
            addLog(LogLevel.WARN, LogSource.BLE, "PERM", "蓝牙权限未授予：扫描与连接不可用")
        }
    }

    /** Releases the GATT client; called when the composition goes away. */
    fun close() {
        transport.close()
        scope.cancel()
    }

    // --- link lifecycle -------------------------------------------------------

    private fun handleLinkState(link: LinkState) {
        if (link == lastLink) return
        lastLink = link
        when (link) {
            LinkState.DISCONNECTED -> {
                resetTelemetry(keepName = true)
                // A capture cannot continue without the link, and the App must not
                // keep showing a progress bar that will never move again.
                dropCalibSession("链路已断开，校准会话结束")
                addLog(LogLevel.WARN, LogSource.BLE, "LINK", "链路已断开")
            }

            LinkState.CONNECTING -> {
                _device.update { it.copy(connected = false) }
                addLog(LogLevel.INFO, LogSource.BLE, "LINK", "正在建立 GATT 连接…")
            }

            LinkState.CONNECTED -> {
                addLog(LogLevel.INFO, LogSource.BLE, "LINK", "GATT 已连接，协商 MTU 并发现服务…")
            }

            LinkState.READY -> {
                _device.update { it.copy(connected = true) }
                incompatibleReported = false
                addLog(LogLevel.INFO, LogSource.BLE, "LINK", "Status/Log/Control 通知已订阅，链路就绪")
                startHandshake()
                // The device may already be calibrating (the App was in the
                // background): re-sync instead of assuming an idle session.
                refreshCalibration()
            }

            is LinkState.ERROR -> {
                resetTelemetry(keepName = true)
                addLog(LogLevel.ERROR, LogSource.BLE, "LINK", link.message)
            }
        }
    }

    private fun startHandshake() {
        handshakeJob?.cancel()
        handshakeJob = scope.launch {
            runAutoCommand(Opcode.GET_CAPS)
            runAutoCommand(Opcode.GET_STATUS)
            runAutoCommand(Opcode.FLASH_INFO)
        }
    }

    private suspend fun runAutoCommand(opcode: Opcode) {
        val result = execute(opcode, ByteArray(0))
        val text = describeResult(opcode, result)
        appendConsole(opcode.label, "-", text)
        addLog(
            if (result?.status == StatusCode.OK.code) LogLevel.INFO else LogLevel.WARN,
            LogSource.BLE,
            "CMD",
            "${opcode.label} -> $text",
        )
    }

    private fun resetTelemetry(keepName: Boolean) {
        _device.update {
            it.copy(
                name = if (keepName) it.name else "未连接",
                connected = false,
                firmware = "—",
                protocol = "—",
                batteryPct = 0,
                batteryMv = 0,
                charging = false,
                mode = "—",
                uptimeSeconds = 0,
            )
        }
        _gnss.value = initialGnss()
        _gnssLink.value = GnssLinkInfo()
        _gnssDebug.value = GnssDebugState()
        _nmea.value = emptyList()
        _gnssCfg.value = emptyMap()
        _imu.value = initialImu()
        _mag.value = initialMag()
        _baro.value = initialBaro()
        lastBaro = null
        _flash.value = initialFlash()
        _checks.value = pendingChecks()
        capsInfo = null
        flashInfo = null
        lastStatus = null
        latestQw = 1f
        latestQx = 0f
        latestQy = 0f
        latestQz = 0f
        latestHeadingDeg = 0f
        attitudeSamples = 0L
        _attitude.value = AttitudeState()
    }

    // --- status / log notifications -------------------------------------------

    private fun handleStatusBytes(bytes: ByteArray) {
        val frame = BleFrame.parseStatus(bytes)
        if (frame == null) {
            addLog(LogLevel.WARN, LogSource.BLE, "STATUS", "Status 帧长度 ${bytes.size} B < ${StatusOffset.BASE_SIZE}，已忽略")
            return
        }
        if (frame.ver != BleFrame.VER) {
            rejectIncompatible("Status ver=${frame.ver}，App 支持 ${BleFrame.VER}")
            return
        }
        onStatusFrame(frame)
    }

    private fun onStatusFrame(frame: StatusFrame) {
        lastStatus = frame
        val caps = capsInfo

        val batteryMv = frame.batMv
        _device.update {
            it.copy(
                connected = true,
                firmware = caps?.let { c -> "fw ${c.firmwareText}" } ?: it.firmware,
                protocol = caps?.let { c -> "proto ${c.protocolText}" } ?: it.protocol,
                batteryMv = batteryMv,
                batteryPct = BatterySoc.fromStatus(frame.socPct, batteryMv, frame.charging),
                charging = frame.charging,
                // v1 has no "work mode" field in the Status frame.
                mode = "—",
                uptimeSeconds = frame.uptimeMs / 1000L,
            )
        }

        val latitude = frame.latE7 / 1e7
        val longitude = frame.lonE7 / 1e7
        syncClock(frame.utcEpochS, frame.utcValid)
        _gnss.value = GnssStatus(
            state = BleFrame.gnssStateName(frame.gnssState),
            fix = frame.gnssFixValid,
            sats = frame.satellites,
            pdop = frame.pdop1e2 / 100.0,
            latitude = latitude,
            longitude = longitude,
            altitudeM = frame.hmslMm / 1000.0,
            speedKmh = frame.speedCms * 0.036,
            headingDeg = frame.headingDdeg / 10.0,
            utcTime = formatUtc(frame.utcEpochS),
            ttff = latestTtff,
            // BLE_Wire_v1 §4: the Status frame has no per-satellite data. It is
            // published 10x per second while the satellite table (telemetry 0x09)
            // arrives once per second, so the last known rows are kept instead of
            // being blanked - otherwise the table flickers between rows and empty.
            satellites = _gnss.value.satellites,
            satTotal = _gnss.value.satTotal,
            // v1.2 block. A 0.3.0 firmware sends 100 bytes and every value below
            // stays 0 with hasAccuracy = false, so the UI blanks the cell instead of
            // claiming "0.0 m" accuracy.
            hasAccuracy = frame.hasAccuracyFields,
            ellipsoidAltM = frame.heightMm / 1000.0,
            hAccM = frame.hAccCm / 100.0,
            vAccM = frame.vAccCm / 100.0,
            sAccMs = frame.sAccMms / 1000.0,
            bAccDeg = frame.headAccCdd / 100.0,
            hdop = frame.hdop1e2 / 100.0,
            vdop = frame.vdop1e2 / 100.0,
        )

        val acc = frame.accLsb
        val gyro = frame.gyroLsb
        val gravity = frame.gravityMg
        val linear = frame.linearMg
        // Status carries the SENSOR frame; body-frame ax/ay/az stay owned by
        // onImuTelemetry() so the two rates no longer overwrite each other.
        _imu.update {
            it.copy(
                axSensor = acc[0] * ACC_G_PER_LSB,
                aySensor = acc[1] * ACC_G_PER_LSB,
                azSensor = acc[2] * ACC_G_PER_LSB,
                gxSensor = gyro[0] * GYRO_DPS_PER_LSB,
                gySensor = gyro[1] * GYRO_DPS_PER_LSB,
                gzSensor = gyro[2] * GYRO_DPS_PER_LSB,
                steps = frame.steps.toInt().coerceAtLeast(0),
                zupt = frame.zupt,
                gravityXg = gravity[0] / 1000f,
                gravityYg = gravity[1] / 1000f,
                gravityZg = gravity[2] / 1000f,
                linearXg = linear[0] / 1000f,
                linearYg = linear[1] / 1000f,
                linearZg = linear[2] / 1000f,
                imuTempC = frame.imuTempCc / 100f,
                magTempC = frame.magTempCc / 100f,
                gravityValid = frame.gravityValid,
                imuTempValid = frame.imuTempValid,
                magTempValid = frame.magTempValid,
            )
        }

        val mag = frame.magLsb
        val mx = mag[0] * MAG_UT_PER_LSB
        val my = mag[1] * MAG_UT_PER_LSB
        val mz = mag[2] * MAG_UT_PER_LSB
        _mag.value = MagStatus(
            mx = mx,
            my = my,
            mz = mz,
            norm = sqrt(mx * mx + my * my + mz * mz),
            // No anomaly flag exists in the v1 frame.
            anomaly = false,
        )

        // Pressure and temperature come from the 0x04 BARO telemetry packet, never
        // from this frame: baro_press_raw / baro_temp_raw are uncompensated 24-bit
        // counts, and converting them here would invent a plausible wrong hPa. The
        // raw counts stay out of the UI; [baro] keeps what the last packet said.

        if (frame.gnssFixValid && _liveTracking.value) {
            val tick = frame.uptimeMs / 1000L
            _geoTrail.update { points ->
                (points + GeoPoint(latitude, longitude, tick)).takeLast(MAX_GEO_POINTS)
            }
        }
        rebuildChecks()
    }

    /** Telemetry characteristic: IMU packets carry the attitude, PDR packets the
     *  dead-reckoned position, GNSS packets the receiver link counters, BARO the
     *  compensated pressure. Anything else is a group this app does not draw yet.
     */
    private fun handleTelemetry(bytes: ByteArray) {
        val packet = BleFrame.parseTelemetry(bytes) ?: return

        when (packet.id) {
            TelemetryId.GNSS -> BleFrame.parseGnssTelemetry(packet.payload)?.let { onGnssTelemetry(it) }
            TelemetryId.IMU -> BleFrame.parseImuTelemetry(packet.payload)?.let { onImuTelemetry(it) }
            TelemetryId.BARO -> BleFrame.parseBaroTelemetry(packet.payload)?.let { onBaroTelemetry(it) }
            TelemetryId.PDR -> BleFrame.parsePdrTelemetry(packet.payload)?.let { onPdrTelemetry(it) }
            TelemetryId.SAT -> BleFrame.parseSatTelemetry(packet.payload)?.let { onSatTelemetry(it) }
            TelemetryId.NMEA -> BleFrame.parseNmeaTelemetry(packet.payload)?.let { onNmea(it) }
            else -> Unit
        }
    }

    /**
     * GNSS link counters, 1 Hz.
     *
     * Deliberately kept apart from [gnss]: this carries the receive path, not the
     * solution, and the two must never be mixed into one number on screen.
     */
    private fun onGnssTelemetry(telemetry: GnssTelemetry) {
        latestTtff = formatTtff(telemetry)
        _gnssLink.value = GnssLinkInfo(
            received = true,
            state = BleFrame.gnssStateName(telemetry.state),
            fixType = telemetry.fixType,
            satellites = telemetry.satellites,
            fixValid = telemetry.fixValid,
            utcValid = telemetry.utcValid,
            ageMs = telemetry.ageMs,
            rxBytes = telemetry.rxBytes,
            ubxFrames = telemetry.ubxFrames,
            badChecksum = telemetry.badChecksum,
            nmeaStarts = telemetry.nmeaStarts,
            rxDropped = telemetry.rxDropped,
            baud = telemetry.baud,
            ttffNote = ttffNote(telemetry),
        )
    }

    /**
     * TTFF in GPSTest's shape (`N sec`, empty until the first fix). The power-off
     * time stays available as [ttffNote]: a few seconds after a 3 minute break is a
     * hot start (V_BCKP kept the almanac), 30 s after two hours away is as good as
     * a cold start.
     */
    private fun formatTtff(telemetry: GnssTelemetry): String {
        if (telemetry.ttffMs == 0L) return ""
        // GPSTest prints whole seconds only: `formatTtff()` = "N sec".
        return "${telemetry.ttffMs / 1000L} sec"
    }

    /** Which kind of start the TTFF above belongs to; GPSTest has no such field. */
    private fun ttffNote(telemetry: GnssTelemetry): String = when {
        telemetry.ttffMs == 0L -> "等待首次定位"
        telemetry.powerOffMs == GnssTelemetry.UINT32_MAX -> "上电后首次"
        telemetry.powerOffMs < 30 * 60_000L -> "热启动，断电 ${formatSpan(telemetry.powerOffMs)}"
        else -> "冷启动，断电 ${formatSpan(telemetry.powerOffMs)}"
    }

    private fun formatSpan(ms: Long): String = when {
        ms < 60_000L -> "${ms / 1000L} 秒"
        ms < 3_600_000L -> "${ms / 60_000L} 分钟"
        else -> "%.1f 小时".format(ms / 3_600_000.0)
    }

    /**
     * Satellite table (1 Hz while the debug session collects NAV-SAT).
     *
     * The Status frame carries only the number of satellites used, so this is the
     * only place the per-satellite rows come from. Azimuth and ephemeris flags are
     * not published by this firmware version and stay unset instead of zero-filled.
     */
    private fun onSatTelemetry(telemetry: SatTelemetry) {
        val rows = sortSatellites(
            telemetry.rows.map { row ->
                SatelliteInfo(
                    svid = row.svId,
                    system = gnssSystemName(row.gnssId),
                    cno = row.cno,
                    elevation = row.elevation,
                    azimuth = row.azimuth,
                    used = row.used,
                    ephAvail = row.ephAvail,
                    almAvail = row.almAvail,
                    carrier = carrierLabel(row.gnssId, row.sigId),
                    tracking = row.qualityLabel,
                    quality = row.quality,
                )
            },
        )

        /* `sats` keeps coming from the Status frame (NAV-PVT numSV, i.e. the
         * satellites actually used); the table lists every satellite the receiver
         * reports, which is a larger number. Overwriting both would make the
         * header pill flip between the two rates.
         */
        _gnss.update { it.copy(satellites = rows, satTotal = telemetry.total) }
    }

    private fun onNmea(telemetry: NmeaTelemetry) {
        _nmea.update { lines -> (lines + telemetry.text).takeLast(MAX_NMEA_LINES) }
    }

    /**
     * BMP388 telemetry, 1 Hz while the Telemetry characteristic is subscribed.
     *
     * The device compensates pressure and die temperature itself, so the App only
     * rescales (Pa -> hPa, 0.01 °C -> °C, mm -> m). A clear flag means "no value",
     * not zero: the screen then draws "—".
     */
    private fun onBaroTelemetry(telemetry: BaroTelemetry) {
        lastBaro = telemetry
        _baro.value = baroStatusOf(telemetry)
    }

    /** Maps one BARO packet onto the UI model; the flags decide what is real. */
    private fun baroStatusOf(telemetry: BaroTelemetry): BaroStatus {
        val hasBaseline = telemetry.baselineValid
        return BaroStatus(
            pressureHpa = if (telemetry.pressValid) telemetry.pressureHpa else 0f,
            temperatureC = if (telemetry.tempValid) telemetry.temperatureC else 0f,
            // v1 has no floor estimator, so "altitude" means "above the baseline".
            altitudeM = if (hasBaseline) telemetry.relAltM else 0f,
            floor = 0,
            valid = telemetry.pressValid,
            tempValid = telemetry.tempValid,
            baselineHpa = if (hasBaseline) telemetry.baselineHpa else 0f,
            baselineAuto = hasBaseline && telemetry.baselineAuto,
            relAltM = if (hasBaseline) telemetry.relAltM else 0f,
            baselineValid = hasBaseline,
            altMslM = if (telemetry.mslValid) telemetry.altMslM else 0f,
            mslValid = telemetry.mslValid,
        )
    }

    private fun gnssSystemName(gnssId: Int): String = when (gnssId) {
        0 -> "GPS"
        1 -> "SBAS"
        2 -> "GAL"
        3 -> "BDS"
        5 -> "QZSS"
        6 -> "GLO"
        else -> "ID$gnssId"
    }

    /**
     * Carrier-frequency label for the GPSTest `CF` column.
     *
     * The wire carries the u-blox signal id (interface description §1.5.4), not a
     * frequency, because the receiver is what knows which signal a C/N0 belongs
     * to. Labels match GPSTest's `CarrierFreqUtils` for the same signal.
     * `-1` (no NAV-SIG row) and unknown ids render as "".
     */
    private fun carrierLabel(gnssId: Int, sigId: Int): String = when (gnssId) {
        // GPS: 0 L1C/A, 3 L2 CL, 4 L2 CM, 6 L5 I, 7 L5 Q
        0 -> when (sigId) {
            0 -> "L1"
            3, 4 -> "L2"
            6, 7 -> "L5"
            else -> ""
        }
        // SBAS: L1C/A only
        1 -> if (sigId == 0) "L1" else ""
        // Galileo: 0 E1 C, 1 E1 B, 3/4 E5a, 5/6 E5b
        2 -> when (sigId) {
            0, 1 -> "E1"
            3, 4 -> "E5a"
            5, 6 -> "E5b"
            else -> ""
        }
        // BeiDou: 0/1 B1I, 2/3 B2I, 5/6 B1C, 7/8 B2a
        3 -> when (sigId) {
            0, 1 -> "B1I"
            2, 3 -> "B2I"
            5, 6 -> "B1C"
            7, 8 -> "B2a"
            else -> ""
        }
        // QZSS: 0 L1C/A, 1 L1S, 4/5 L2, 8/9 L5
        5 -> when (sigId) {
            0, 1 -> "L1"
            4, 5 -> "L2"
            8, 9 -> "L5"
            else -> ""
        }
        // GLONASS: 0 L1OF, 2 L2OF
        6 -> when (sigId) {
            0 -> "L1"
            2 -> "L2"
            else -> ""
        }
        // NavIC: 0 L5 A
        7 -> if (sigId == 0) "L5" else ""
        else -> ""
    }

    // --- GNSS debug session and receiver settings ------------------------------

    override fun setGnssDebug(flags: Int, ttlSeconds: Int) {
        scope.launch {
            val result = request(Opcode.GNSS_DEBUG, GnssCodec.encodeDebug(flags, ttlSeconds))
            val body = result?.payload
            val decoded = body?.let { GnssCodec.decodeDebug(it) }
            _gnssDebug.value = when {
                result == null -> GnssDebugState(error = "链路未就绪或超时")
                result.status != 0 -> GnssDebugState(error = describeStatus(result.status))
                decoded == null -> GnssDebugState(error = "响应无法解析")
                else -> decoded
            }
        }
    }

    override fun refreshGnssDebug() {
        scope.launch {
            val result = request(Opcode.GNSS_DEBUG, GnssCodec.encodeDebugQuery())
            val body = result?.payload
            val decoded = body?.let { GnssCodec.decodeDebug(it) }
            _gnssDebug.value = when {
                result == null -> GnssDebugState(error = "链路未就绪或超时")
                result.status != 0 -> GnssDebugState(error = describeStatus(result.status))
                decoded == null -> GnssDebugState(error = "响应无法解析")
                else -> decoded
            }
        }
    }

    /**
     * Cold / warm / hot start (opcode 0x37).
     *
     * The device answers as soon as the request is handed to its GNSS thread, not
     * when the receiver is back: a controlled software reset keeps the module off
     * its UART for about a second, and the firmware re-applies the RAM-layer
     * message rates afterwards. What the App can honestly show is "accepted", and
     * the TTFF of the next cycle is the evidence.
     */
    override fun restartGnss(mode: Int) {
        if (_gnssRestart.value.busy) return
        scope.launch {
            _gnssRestart.update { it.copy(busy = true, error = null) }
            val result = request(Opcode.GNSS_RESTART, GnssCodec.encodeRestart(mode))
            val accepted = result?.payload?.let { GnssCodec.decodeRestart(it) }
            _gnssRestart.value = when {
                result == null -> GnssRestartState(
                    mode = mode,
                    error = "链路未就绪或超时",
                )
                result.status != 0 -> GnssRestartState(
                    mode = mode,
                    error = describeStatus(result.status),
                )
                accepted == null -> GnssRestartState(mode = mode, error = "响应无法解析")
                else -> GnssRestartState(
                    mode = accepted,
                    message = "${GnssRestartMode.label(accepted)}已下发：接收机约 1 s 后重启，" +
                        "TTFF 重新从 0 开始计时",
                )
            }
        }
    }

    override fun clearNmea() {
        _nmea.value = emptyList()
    }

    override fun refreshGnssCfg() {
        scope.launch {
            GnssCfgKeys.ALL.forEach { key ->
                val result = request(Opcode.GNSS_CFG, GnssCodec.encodeCfgGet(key.key))
                val body = result?.payload
                val decoded = body?.let { GnssCodec.decodeCfg(it) }
                val value = when {
                    result == null -> GnssCfgValue(key.key, key.len, 0, "链路未就绪或超时")
                    result.status != 0 -> GnssCfgValue(key.key, key.len, 0, describeGnssCfgStatus(result.status))
                    decoded == null -> GnssCfgValue(key.key, key.len, 0, "响应无法解析")
                    else -> decoded
                }
                _gnssCfg.update { it + (key.key to value) }
            }
        }
    }

    override fun setGnssCfg(key: Long, value: Long, persistToFlash: Boolean) {
        val desc = GnssCfgKeys.byKey(key) ?: return
        scope.launch {
            val result = request(
                Opcode.GNSS_CFG,
                GnssCodec.encodeCfgSet(key, value, desc.len, persistToFlash),
                GNSS_CFG_TIMEOUT_MS,
            )
            val body = result?.payload
            val decoded = body?.let { GnssCodec.decodeCfg(it) }
            val store = when {
                result == null -> GnssCfgValue(key, desc.len, 0, "链路未就绪或超时")
                result.status != 0 -> GnssCfgValue(key, desc.len, 0, describeGnssCfgStatus(result.status))
                decoded == null -> GnssCfgValue(key, desc.len, 0, "响应无法解析")
                else -> decoded
            }
            _gnssCfg.update { it + (key to store) }
        }
    }

    /** Protocol status code as words (`BLE_Wire_v1.md` §3.1). */
    private fun describeStatus(status: Int): String = when (status) {
        0x01 -> "参数无效（固件拒绝）"
        0x02 -> "未知 opcode"
        0x03 -> "固件不支持"
        0x04 -> "忙：GNSS 模组当前未上电或未应答"
        0x06 -> "需要先 DEBUG_UNLOCK"
        else -> "状态 0x%02X".format(status)
    }

    /**
     * The same codes for the GNSS configuration path, where `0x03` has a precise
     * meaning: the firmware only returns -ENOTSUP after the receiver answered
     * UBX-ACK-NAK, and `0x04` also covers the power-up profile occupying the UART.
     */
    private fun describeGnssCfgStatus(status: Int): String = when (status) {
        0x03 -> "接收机 NAK：键或星座组合被拒（见 §6.6）"
        0x04 -> "忙：模组未上电 / 未应答 / 上电画像进行中"
        else -> describeStatus(status)
    }

    /**
     * IMU telemetry arrives at the IMU rate (30 Hz). The filter output is stored
     * on every packet but only pushed into Compose at ~15 Hz: recomposing a 3D view
     * 30 times per second buys nothing and costs battery.
     */
    private fun onImuTelemetry(imu: ImuTelemetry) {
        latestQw = imu.qw
        latestQx = imu.qx
        latestQy = imu.qy
        latestQz = imu.qz
        attitudeSamples++

        val euler = eulerDeg(imu.qw, imu.qx, imu.qy, imu.qz)

        latestHeadingDeg = euler[2]

        val now = System.currentTimeMillis()

        if (now - lastAttitudeEmitMs < ATTITUDE_UI_PERIOD_MS) return
        lastAttitudeEmitMs = now

        _attitude.value = AttitudeState(
            qw = imu.qw,
            qx = imu.qx,
            qy = imu.qy,
            qz = imu.qz,
            rollDeg = euler[0],
            pitchDeg = euler[1],
            headingDeg = euler[2],
            samples = attitudeSamples,
            valid = true,
        )
        _imu.update {
            it.copy(
                ax = imu.accLsb[0] * ACC_G_PER_LSB,
                ay = imu.accLsb[1] * ACC_G_PER_LSB,
                az = imu.accLsb[2] * ACC_G_PER_LSB,
                gx = imu.gyroLsb[0] * GYRO_DPS_PER_LSB,
                gy = imu.gyroLsb[1] * GYRO_DPS_PER_LSB,
                gz = imu.gyroLsb[2] * GYRO_DPS_PER_LSB,
                qw = imu.qw,
                qx = imu.qx,
                qy = imu.qy,
                qz = imu.qz,
            )
        }
    }

    /** PDR telemetry: append one oriented point per completed step (or heartbeat). */
    private fun onPdrTelemetry(pdr: PdrTelemetry) {
        if (!_trailRunning.value) return

        val point = TrailPoint(
            x = pdr.xMm / 1000f,
            y = pdr.yMm / 1000f,
            z = 0f,
            tick = pdr.steps.toLong(),
            qw = latestQw,
            qx = latestQx,
            qy = latestQy,
            qz = latestQz,
            headingDeg = pdr.headingDdeg / 10f,
        )
        _trail.update { points ->
            if (points.isNotEmpty() && points.last().x == point.x && points.last().y == point.y) {
                points
            } else {
                (points + point).takeLast(MAX_TRAIL_POINTS)
            }
        }
    }

    /** Quaternion to (roll, pitch, heading) in degrees, same convention as the
     *  firmware: ZYX order, heading clockwise from north.
     */
    private fun eulerDeg(qw: Float, qx: Float, qy: Float, qz: Float): FloatArray {
        val r20 = 2f * ((qx * qz) - (qw * qy))
        val r21 = 2f * ((qy * qz) + (qw * qx))
        val r22 = 1f - (2f * ((qx * qx) + (qy * qy)))
        val r01 = 2f * ((qx * qy) - (qw * qz))
        val r11 = 1f - (2f * ((qx * qx) + (qz * qz)))

        val roll = Math.toDegrees(atan2(-r20.toDouble(), r22.toDouble())).toFloat()
        val pitch = Math.toDegrees(asin(r21.coerceIn(-1f, 1f).toDouble())).toFloat()
        var heading = Math.toDegrees(atan2(r01.toDouble(), r11.toDouble())).toFloat()

        if (heading < 0f) heading += 360f

        return floatArrayOf(roll, pitch, heading)
    }

    private fun handleLogPacket(bytes: ByteArray) {
        val packet = BleFrame.parseLog(bytes) ?: return
        val level = when (packet.level) {
            0 -> LogLevel.ERROR
            1 -> LogLevel.WARN
            3 -> LogLevel.DEBUG
            else -> LogLevel.INFO
        }
        addLog(level, LogSource.BLE, packet.source.ifBlank { "FW" }, packet.message.take(240))
    }

    private fun rejectIncompatible(reason: String) {
        if (incompatibleReported) return
        incompatibleReported = true
        addLog(LogLevel.ERROR, LogSource.BLE, "COMPAT", "拒绝不兼容的固件线格式：$reason")
        transport.disconnect()
    }

    // --- command transport ----------------------------------------------------

    private suspend fun execute(
        opcode: Opcode,
        payload: ByteArray,
        timeoutMs: Long = COMMAND_TIMEOUT_MS,
    ): CommandResult? {
        val result = request(opcode, payload, timeoutMs)
        applyResult(opcode, result)
        return result
    }

    private suspend fun request(
        opcode: Opcode,
        payload: ByteArray,
        timeoutMs: Long = COMMAND_TIMEOUT_MS,
    ): CommandResult? {
        if (transport.state.value != LinkState.READY) return null
        val seq = nextSequence()
        val deferred = CompletableDeferred<CommandResult>()
        synchronized(pendingLock) { pending[seq] = deferred }
        transport.send(BleFrame.encodeCommand(opcode.code, seq, payload))
        val result = try {
            withTimeoutOrNull(timeoutMs) { deferred.await() }
        } finally {
            synchronized(pendingLock) { pending.remove(seq) }
        }
        return result
    }

    private fun nextSequence(): Int = synchronized(pendingLock) {
        sequence = (sequence + 1) and 0xFFFF
        if (sequence == 0) sequence = 1
        sequence
    }

    private fun handleResponseFrame(bytes: ByteArray) {
        val frame = BleFrame.parseFrame(bytes)
        if (frame == null) {
            addLog(LogLevel.WARN, LogSource.BLE, "FRAME", "收到无法解析的 Control 帧（${bytes.size} B）")
            return
        }
        if (!frame.isResponse) return
        val status = if (frame.payload.isNotEmpty()) BleFrame.u8(frame.payload, 0) else -1
        val body = if (frame.payload.size > 1) {
            frame.payload.copyOfRange(1, frame.payload.size)
        } else {
            ByteArray(0)
        }
        val deferred = synchronized(pendingLock) { pending.remove(frame.seq) }
        if (deferred != null) {
            deferred.complete(CommandResult(status, body))
            return
        }
        // Late response after the 2 s window: still fold a Status body into the UI.
        if (frame.opcode == Opcode.GET_STATUS && status == StatusCode.OK.code) {
            BleFrame.parseStatus(body)?.let { onStatusFrame(it) }
        }
    }

    private fun applyResult(opcode: Opcode, result: CommandResult?) {
        if (result == null || result.status != StatusCode.OK.code) return
        when (opcode) {
            Opcode.GET_CAPS -> {
                val caps = BleFrame.parseCaps(result.payload) ?: return
                if (caps.ver != BleFrame.VER) {
                    rejectIncompatible("GET_CAPS ver=${caps.ver}，App 支持 ${BleFrame.VER}")
                    return
                }
                if (caps.statusSize < StatusOffset.BASE_SIZE) {
                    rejectIncompatible("status_size=${caps.statusSize} < ${StatusOffset.BASE_SIZE}")
                    return
                }
                capsInfo = caps
                _calib.update {
                    it.copy(
                        magSupported = caps.caps and CapsBits.MAG_CAL != 0L,
                        accelSupported = caps.caps and CapsBits.ACCEL_CAL != 0L,
                    )
                }
                _device.update {
                    it.copy(
                        firmware = "fw ${caps.firmwareText}",
                        protocol = "proto ${caps.protocolText}",
                    )
                }
                rebuildChecks()
            }

            Opcode.GET_STATUS -> BleFrame.parseStatus(result.payload)?.let { onStatusFrame(it) }

            Opcode.FLASH_INFO -> BleFrame.parseFlashInfo(result.payload)?.let { info ->
                flashInfo = info
                updateFlash(info)
                rebuildChecks()
            }

            Opcode.POI_MARK -> {
                val written = if (result.payload.isNotEmpty()) BleFrame.u8(result.payload, 0) else -1
                addLog(
                    LogLevel.INFO,
                    LogSource.BLE,
                    "POI",
                    if (written == 1) "固件已写入轨迹环" else "固件未写入轨迹环（written=$written）",
                )
            }

            else -> Unit
        }
    }

    private fun calibrationCheckItem(): CheckItem {
        val state = _calib.value
        val caps = capsInfo
        val supported = state.magSupported || state.accelSupported ||
            (caps != null && caps.caps and (CapsBits.MAG_CAL or CapsBits.ACCEL_CAL) != 0L)
        if (!supported) {
            return CheckItem("传感器校准", "固件未声明 MAG_CAL / ACCEL_CAL 能力", CheckState.PENDING)
        }
        val result = state.magResult
        return if (result != null && result.fromStore) {
            CheckItem(
                "传感器校准",
                CalibFormat.lastCalib(result.lastCalibUnixS, true),
                CheckState.PASS,
            )
        } else {
            CheckItem("传感器校准", "支持校准，尚未保存系数", CheckState.PENDING)
        }
    }

    private fun updateFlash(info: FlashInfo) {
        val sectors = if (info.sectorCount > 0) {
            List(info.sectorCount) { index ->
                when {
                    index.toLong() < info.sectorSeq -> "ARCHIVED"
                    index.toLong() == info.sectorSeq -> "WRITING"
                    else -> "ERASED"
                }
            }
        } else {
            emptyList()
        }
        _flash.value = FlashStatus(
            // FLASH_INFO reports capacity/records only; no JEDEC, used bytes or
            // journal state exists in the v1 response.
            jedec = "",
            usedMb = 0f,
            totalMb = info.flashBytes / (1024f * 1024f),
            recordCount = info.recordsWritten.toInt(),
            journal = "",
            sectors = sectors,
        )
    }

    private fun rebuildChecks() {
        if (_checkRunning.value) return
        val caps = capsInfo
        val status = lastStatus
        val flash = flashInfo
        val items = mutableListOf<CheckItem>()

        items += if (caps == null) {
            CheckItem("MCU", "等待 GET_CAPS（0x01）", CheckState.PENDING)
        } else {
            CheckItem("MCU", "fw ${caps.firmwareText} / proto ${caps.protocolText}", CheckState.PASS)
        }

        items += if (flash == null || flash.flashBytes <= 0L) {
            CheckItem("外部 Flash", "等待 FLASH_INFO（0x60）", CheckState.PENDING)
        } else {
            CheckItem(
                "外部 Flash",
                "${flash.flashBytes / (1024L * 1024L)} MB（固件 SFDP 实测）",
                CheckState.PASS,
            )
        }

        items += if (status == null) {
            CheckItem("IMU", "等待 Status 帧", CheckState.PENDING)
        } else {
            CheckItem(
                "IMU",
                "Status flags.bit3 = ${if (status.imuValid) 1 else 0}",
                if (status.imuValid) CheckState.PASS else CheckState.PENDING,
            )
        }

        items += if (status == null) {
            CheckItem("磁力计", "等待 Status 帧", CheckState.PENDING)
        } else {
            CheckItem(
                "磁力计",
                "Status flags.bit4 = ${if (status.magValid) 1 else 0}",
                if (status.magValid) CheckState.PASS else CheckState.PENDING,
            )
        }

        // Calibration is gated on the GET_CAPS capability bits, not on a version
        // number: a firmware can speak the opcode yet refuse to persist anything.
        items += calibrationCheckItem()

        items += if (status == null) {
            CheckItem("气压计", "等待 Status 帧", CheckState.PENDING)
        } else {
            CheckItem(
                "气压计",
                "Status flags.bit5 = ${if (status.baroValid) 1 else 0}",
                if (status.baroValid) CheckState.PASS else CheckState.PENDING,
            )
        }

        items += if (status == null) {
            CheckItem("GNSS 模组", "等待 Status 帧", CheckState.PENDING)
        } else {
            CheckItem(
                "GNSS 模组",
                "gnss_state=${BleFrame.gnssStateName(status.gnssState)} / fix_type=${status.fixType}",
                if (status.gnssState != 0) CheckState.PASS else CheckState.PENDING,
            )
        }

        // No single-satellite C/N0 is on the wire, so antenna quality cannot be
        // judged by the app; keep it PENDING instead of guessing.
        items += CheckItem("GNSS 天线", "固件 v1 未提供（无单星 C/N0）", CheckState.PENDING)

        items += if (status == null) {
            CheckItem("电池 ADC", "等待 Status 帧", CheckState.PENDING)
        } else if (status.batMv > 0) {
            CheckItem("电池 ADC", "${status.batMv} mV", CheckState.PASS)
        } else {
            CheckItem("电池 ADC", "bat_mv 无效（${status.batMv}）", CheckState.PENDING)
        }

        items += if (status == null) {
            CheckItem("充电状态", "等待 Status 帧", CheckState.PENDING)
        } else {
            CheckItem(
                "充电状态",
                "flags.bit1 = ${if (status.charging) 1 else 0}（${if (status.charging) "充电中" else "未充电"}）",
                CheckState.PASS,
            )
        }

        items += if (status == null) {
            CheckItem("按键", "等待 Status 帧", CheckState.PENDING)
        } else {
            CheckItem(
                "按键",
                "KEY1=${status.keyLevels and 0x01} KEY2=${(status.keyLevels shr 1) and 0x01}",
                CheckState.PASS,
            )
        }

        items += CheckItem("LED", "固件 v1 未提供 LED 状态", CheckState.PENDING)

        items += if (caps == null) {
            CheckItem("BLE", "等待 GET_CAPS（0x01）", CheckState.PENDING)
        } else {
            CheckItem("BLE", "MTU ${caps.attMtu} / max payload ${caps.maxCmdPayload}", CheckState.PASS)
        }

        items += CheckItem("SELFTEST", "固件 v1 返回 NOT_SUPPORTED", CheckState.PENDING)

        if (_checks.value != items) {
            _checks.value = items
        }
    }

    // --- UI actions -----------------------------------------------------------

    override fun scan() {
        if (!permissionsGranted) {
            addLog(LogLevel.ERROR, LogSource.BLE, "SCAN", "缺少蓝牙扫描权限，未启动扫描")
            return
        }
        if (_scanning.value) return
        _devices.value = emptyList()
        addLog(LogLevel.DEBUG, LogSource.BLE, "SCAN", "按 Service UUID 扫描（10 s，超时自动停止）")
        transport.scan { device ->
            _devices.update { current ->
                val index = current.indexOfFirst { it.address == device.address }
                when {
                    index < 0 -> current + device
                    device.rssi > current[index].rssi ||
                        (current[index].name.isBlank() && device.name.isNotBlank()) -> {
                        val copy = current.toMutableList()
                        copy[index] = device
                        copy
                    }

                    else -> current
                }
            }
        }
    }

    override fun connect(device: ScannedDevice) {
        if (!permissionsGranted) {
            addLog(LogLevel.ERROR, LogSource.BLE, "BLE", "缺少蓝牙权限，未发起连接")
            return
        }
        val label = device.name.ifBlank { device.address }
        _device.update { it.copy(name = label, connected = false) }
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "BLE",
            "连接 $label（${device.address}，RSSI ${device.rssi} dBm）",
        )
        transport.connect(device.address)
    }

    override fun disconnect() {
        addLog(LogLevel.INFO, LogSource.BLE, "BLE", "请求断开连接")
        transport.disconnect()
    }

    override fun markPoi() {
        val current = _gnss.value
        if (!current.fix) {
            addLog(LogLevel.WARN, LogSource.BLE, "POI", "GNSS 无有效定位，未打点")
            return
        }
        sendCommand(Opcode.POI_MARK.label, "")
        addPoi(current.latitude, current.longitude, "快捷打点")
    }

    override fun addPoi(latitude: Double, longitude: Double, note: String) {
        if (!latitude.isFinite() || !longitude.isFinite()) {
            addLog(LogLevel.WARN, LogSource.BLE, "POI", "坐标无效，未打点")
            return
        }
        if (latitude == 0.0 && longitude == 0.0) {
            addLog(LogLevel.WARN, LogSource.BLE, "POI", "坐标为 0,0（无定位），未打点")
            return
        }
        val id = nextPoiId++
        val marker = PoiMarker(
            id = id,
            latitude = latitude,
            longitude = longitude,
            label = "P$id",
            note = note,
            time = now(),
        )
        _pois.update { it + marker }
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "POI",
            "%s @ %.6f, %.6f".format(Locale.US, marker.label, latitude, longitude),
        )
    }

    override fun removePoi(id: Int) {
        _pois.update { list -> list.filterNot { it.id == id } }
        addLog(LogLevel.INFO, LogSource.BLE, "POI", "removed P$id")
    }

    override fun setLiveTracking(enabled: Boolean) {
        _liveTracking.value = enabled
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "MAP",
            if (enabled) "real-time trajectory resumed" else "real-time trajectory paused",
        )
    }

    override fun clearGeoTrail() {
        _geoTrail.value = emptyList()
        addLog(LogLevel.INFO, LogSource.BLE, "MAP", "geo trail cleared")
    }

    override fun setTrailRunning(enabled: Boolean) {
        _trailRunning.value = enabled
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "PDR",
            if (enabled) "spatial trail resumed" else "spatial trail paused",
        )
    }

    override fun clearTrail() {
        _trail.value = emptyList()
        addLog(LogLevel.INFO, LogSource.BLE, "PDR", "spatial trail cleared")
    }

    override fun sendCommand(opcode: String, payload: String): ConsoleEntry {
        val label = opcode.trim()
        val entry = ConsoleEntry(
            time = now(),
            opcode = label.ifBlank { "UNKNOWN" },
            request = payload.ifBlank { "-" },
            response = PENDING_RESPONSE,
        )
        _console.update { (it + entry).takeLast(MAX_CONSOLE_ENTRIES) }

        val parsedOpcode = Opcode.fromName(label)
        if (parsedOpcode == null) {
            finishConsole(entry, "-- UNKNOWN OPCODE（协议 §6 无此命令，未发送）")
            return entry
        }
        val bytes = parseHex(payload)
        if (bytes == null) {
            finishConsole(entry, "-- INVALID HEX（payload 必须是偶数个十六进制字符）")
            return entry
        }
        if (bytes.size > BleFrame.MAX_PAYLOAD) {
            finishConsole(entry, "-- PAYLOAD TOO LARGE（${bytes.size} B > ${BleFrame.MAX_PAYLOAD} B）")
            return entry
        }
        if (transport.state.value != LinkState.READY) {
            finishConsole(entry, "-- NOT CONNECTED（链路未就绪，命令未发送）")
            return entry
        }

        scope.launch {
            val result = execute(parsedOpcode, bytes)
            finishConsole(entry, describeResult(parsedOpcode, result))
        }
        return entry
    }

    override fun runSelfTest() {
        if (_checkRunning.value) return
        if (transport.state.value != LinkState.READY) {
            addLog(LogLevel.WARN, LogSource.BLE, "SELFTEST", "链路未就绪，未发送 SELFTEST")
            return
        }
        _checkRunning.value = true
        _checks.update { items -> items.map { it.copy(state = CheckState.RUNNING) } }
        addLog(LogLevel.INFO, LogSource.BLE, "SELFTEST", "发送 SELFTEST (0x40)")

        scope.launch {
            val result = execute(Opcode.SELFTEST, ByteArray(0))
            val text = describeResult(Opcode.SELFTEST, result)
            appendConsole(Opcode.SELFTEST.label, "-", text)
            if (result == null) {
                _checks.update { items ->
                    items.map { it.copy(state = CheckState.PENDING, detail = "SELFTEST 超时（2 s）") }
                }
                addLog(LogLevel.WARN, LogSource.BLE, "SELFTEST", "2 s 内未收到 SELFTEST 响应")
                _checkRunning.value = false
            } else if (result.status == StatusCode.NOT_SUPPORTED.code) {
                _checks.update { items ->
                    items.map { it.copy(state = CheckState.PENDING, detail = "固件 v1 未实现 SELFTEST") }
                }
                addLog(
                    LogLevel.WARN,
                    LogSource.BLE,
                    "SELFTEST",
                    "固件 v1 未实现 SELFTEST（03 NOT_SUPPORTED），检查项保持 PENDING",
                )
                _checkRunning.value = false
            } else if (result.status == StatusCode.OK.code) {
                addLog(
                    LogLevel.INFO,
                    LogSource.BLE,
                    "SELFTEST",
                    "SELFTEST 返回 00 OK，payload=${hexOrDash(result.payload)}",
                )
                _checkRunning.value = false
                rebuildChecks()
            } else {
                _checks.update { items ->
                    items.map { it.copy(state = CheckState.PENDING, detail = text) }
                }
                addLog(LogLevel.WARN, LogSource.BLE, "SELFTEST", text)
                _checkRunning.value = false
            }
        }
    }

    // --- calibration (MAG_CAL 0x41 / ACCEL_CAL 0x43) -------------------------

    override fun refreshCalibration() {
        if (!calibLinkReady("CALIB")) return
        scope.launch {
            applyMagResponse(request(Opcode.MAG_CAL, CalibCodec.encodeMagCal(CalibAction.QUERY)))
            applyAccelResponse(
                request(Opcode.ACCEL_CAL, CalibCodec.encodeAccelCal(CalibAction.QUERY)),
            )
            resyncCalibPolling()
        }
    }

    override fun startMagCal() {
        if (!calibLinkReady("MAG_CAL")) return
        scope.launch {
            _calib.update {
                it.copy(
                    magPhase = CalibPhase.COLLECTING,
                    magProgressPermille = 0,
                    magCoveragePct = CalibState.ZERO_TRIPLE,
                    magSampleCount = 0,
                    magResult = null,
                    magVerdict = null,
                    magMessage = "正在采集：缓慢画 8 字，三条轴都要覆盖",
                    busy = true,
                )
            }
            val result = request(Opcode.MAG_CAL, CalibCodec.encodeMagCal(CalibAction.START))
            _calib.update { it.copy(busy = false) }
            applyMagResponse(result)
            resyncCalibPolling()
        }
    }

    override fun finishMagCal() {
        if (!calibLinkReady("MAG_CAL")) return
        cancelCalibPoll()
        scope.launch {
            _calib.update {
                it.copy(magPhase = CalibPhase.SAVING, magMessage = "正在计算并写入 flash…", busy = true)
            }
            val result = request(
                Opcode.MAG_CAL,
                CalibCodec.encodeMagCal(CalibAction.FINISH_SAVE),
                CALIB_SAVE_TIMEOUT_MS,
            )
            _calib.update { it.copy(busy = false) }
            applyMagResponse(result)
            resyncCalibPolling()
        }
    }

    override fun cancelMagCal() {
        if (!calibLinkReady("MAG_CAL")) return
        cancelCalibPoll()
        scope.launch {
            applyMagResponse(request(Opcode.MAG_CAL, CalibCodec.encodeMagCal(CalibAction.CANCEL)))
            resyncCalibPolling()
        }
    }

    override fun clearMagCal() {
        if (!calibLinkReady("MAG_CAL")) return
        cancelCalibPoll()
        scope.launch {
            applyMagResponse(
                request(
                    Opcode.MAG_CAL,
                    CalibCodec.encodeMagCal(CalibAction.CLEAR),
                    CALIB_CLEAR_TIMEOUT_MS,
                ),
            )
            resyncCalibPolling()
        }
    }

    override fun captureAccelPose() = captureAccel(CalibCodec.POSITION_AUTO)

    override fun captureAccelQuick() = captureAccel(CalibCodec.POSITION_QUICK)

    /**
     * Starts one capture. [position] is normally [CalibCodec.POSITION_AUTO]: the
     * device identifies the face itself, so the user only has to place the phone on
     * another side and press again. A concrete face index stays available for bench
     * work where a known pose must be forced.
     */
    private fun captureAccel(position: Int) {
        if (!calibLinkReady("ACCEL_CAL")) return
        scope.launch {
            _calib.update {
                it.copy(
                    accelPhase = CalibPhase.COLLECTING,
                    accelActivePosition = position,
                    accelProgressPermille = 0,
                    accelRestPct = 0,
                    accelFaceState = AccelFaceStateCode.WAIT_REST,
                    accelMessage = if (position == CalibCodec.POSITION_QUICK) {
                        "单面快速模式：保持设备完全静止"
                    } else {
                        "保持静止，设备会自己识别当前是第几面"
                    },
                    busy = true,
                )
            }
            val result = request(
                Opcode.ACCEL_CAL,
                CalibCodec.encodeAccelCal(CalibAction.START, position),
            )
            _calib.update { it.copy(busy = false) }
            applyAccelResponse(result)
            resyncCalibPolling()
        }
    }

    override fun cancelAccelFace() {
        if (!calibLinkReady("ACCEL_CAL")) return
        cancelCalibPoll()
        scope.launch {
            applyAccelResponse(
                request(Opcode.ACCEL_CAL, CalibCodec.encodeAccelCal(CalibAction.CANCEL)),
            )
            resyncCalibPolling()
        }
    }

    override fun finishAccelCal() {
        if (!calibLinkReady("ACCEL_CAL")) return
        cancelCalibPoll()
        scope.launch {
            _calib.update {
                it.copy(
                    accelPhase = CalibPhase.SAVING,
                    accelMessage = "正在计算并写入 flash…",
                    busy = true,
                )
            }
            val result = request(
                Opcode.ACCEL_CAL,
                CalibCodec.encodeAccelCal(CalibAction.FINISH_SAVE),
                CALIB_SAVE_TIMEOUT_MS,
            )
            _calib.update { it.copy(busy = false) }
            applyAccelResponse(result)
            resyncCalibPolling()
        }
    }

    override fun clearAccelCal() {
        if (!calibLinkReady("ACCEL_CAL")) return
        cancelCalibPoll()
        scope.launch {
            applyAccelResponse(
                request(
                    Opcode.ACCEL_CAL,
                    CalibCodec.encodeAccelCal(CalibAction.CLEAR),
                    CALIB_CLEAR_TIMEOUT_MS,
                ),
            )
            resyncCalibPolling()
        }
    }

    // --- barometric baseline (BARO_BASELINE 0x42) -----------------------------

    /**
     * Re-renders [baro] from the last `0x04` packet. Deliberately sends nothing:
     * `0x42` action 0 is "take the current pressure as baseline", not a query.
     */
    override suspend fun refreshBaroBaseline(): BaroBaselineResult {
        val telemetry = lastBaro
            ?: return BaroBaselineResult(
                ok = false,
                supported = true,
                message = "还没有收到 BARO 遥测（Telemetry 0x04）",
            )
        _baro.value = baroStatusOf(telemetry)
        return BaroBaselineResult(
            ok = telemetry.pressValid,
            supported = true,
            message = when {
                !telemetry.pressValid -> "BARO 遥测没有有效气压（flags.bit0 = 0）"
                !telemetry.baselineValid -> "尚未设置基准"
                telemetry.baselineAuto -> "基准由固件自动建立（GNSS 静止）"
                else -> "基准来自取当前值或手动输入"
            },
        )
    }

    override suspend fun takeBaroBaseline(): BaroBaselineResult =
        sendBaroBaseline(BaroBaselineAction.TAKE_CURRENT, 0L, "取当前气压为基准")

    override suspend fun setBaroBaselineManual(paQ4: Long): BaroBaselineResult {
        if (paQ4 < CalibCodec.BARO_PA_Q4_MIN || paQ4 > CalibCodec.BARO_PA_Q4_MAX) {
            val text = String.format(
                Locale.US,
                "%.1f hPa 超出 %d..%d Pa，固件会回 INVALID_ARGUMENT，未发送",
                CalibCodec.paQ4ToHpa(paQ4),
                CalibCodec.BARO_PA_MIN,
                CalibCodec.BARO_PA_MAX,
            )
            addLog(LogLevel.WARN, LogSource.BLE, "BARO", "手动基准 $text")
            return BaroBaselineResult(ok = false, supported = true, message = text)
        }
        return sendBaroBaseline(BaroBaselineAction.MANUAL, paQ4, "手动设置基准")
    }

    /**
     * `0x42` action 2: the device reads its own GNSS fix and derives the sea-level
     * pressure from that height, which turns `alt_msl_mm` into an absolute altitude
     * that survives a later GNSS outage. The value field of the request is ignored
     * by the firmware, so 0 is sent.
     */
    override suspend fun calibrateBaroFromGnss(): BaroBaselineResult =
        sendBaroBaseline(
            BaroBaselineAction.GNSS,
            0L,
            "用 GNSS 海拔校准",
            invalidArgumentHint = "GNSS 垂直精度不足 / 卫星少 / 在移动，" +
                "或算不出合理的海平面气压",
        )

    /** One BARO_BASELINE round trip; the response is the new firmware state. */
    private suspend fun sendBaroBaseline(
        action: BaroBaselineAction,
        paQ4: Long,
        what: String,
        invalidArgumentHint: String = "当前气压不在 30000..125000 Pa",
    ): BaroBaselineResult {
        if (transport.state.value != LinkState.READY) {
            addLog(LogLevel.WARN, LogSource.BLE, "BARO", "链路未就绪，$what 未发送")
            return BaroBaselineResult(ok = false, supported = true, message = "链路未就绪，命令未发送")
        }
        val result = request(Opcode.BARO_BASELINE, CalibCodec.encodeBaroBaseline(action, paQ4))
        return applyBaroBaselineResult(result, what, invalidArgumentHint)
    }

    private fun applyBaroBaselineResult(
        result: CommandResult?,
        what: String,
        invalidArgumentHint: String = "当前气压不在 30000..125000 Pa",
    ): BaroBaselineResult {
        if (result == null) {
            addLog(LogLevel.WARN, LogSource.BLE, "BARO", "$what：2 s 内没有响应")
            return BaroBaselineResult(ok = false, supported = true, message = "$what：设备没有响应")
        }
        if (result.status == StatusCode.NOT_SUPPORTED.code ||
            result.status == StatusCode.UNKNOWN_OPCODE.code
        ) {
            addLog(
                LogLevel.WARN,
                LogSource.BLE,
                "BARO",
                "$what -> ${StatusCode.labelOf(result.status)}（BMP388 未应答或 TPG 补偿不可用）",
            )
            return BaroBaselineResult(
                ok = false,
                supported = false,
                message = "固件不支持 BARO_BASELINE（${StatusCode.labelOf(result.status)}）：" +
                    "BMP388 未应答或 TPG 补偿不可用",
            )
        }
        if (result.status != StatusCode.OK.code) {
            val text = "$what 失败：${StatusCode.labelOf(result.status)}（$invalidArgumentHint）"
            addLog(LogLevel.WARN, LogSource.BLE, "BARO", text)
            return BaroBaselineResult(ok = false, supported = true, message = text)
        }
        val response = CalibCodec.decodeBaroBaseline(result.payload)
        if (response == null) {
            val text = "$what：响应体 ${result.payload.size} B，短于约定的 ${CalibCodec.BARO_RSP_SIZE} B"
            addLog(LogLevel.WARN, LogSource.BLE, "BARO", text)
            return BaroBaselineResult(ok = false, supported = true, message = text)
        }
        // The response is newer than the last 1 Hz packet, so mirror it right away
        // instead of waiting up to a second for the telemetry to catch up. `0x42`
        // always stores a user baseline, so baseline_auto is cleared; only the
        // firmware's own GNSS-triggered baseline sets it.
        _baro.update {
            it.copy(
                baselineHpa = response.baselineHpa,
                baselineValid = response.baselineValid,
                baselineAuto = false,
                relAltM = if (response.baselineValid) response.relAltM else 0f,
                altitudeM = if (response.baselineValid) response.relAltM else 0f,
            )
        }
        val text = if (response.baselineValid) {
            String.format(
                Locale.US,
                "%s：基准 %.1f hPa，相对高度 %+.1f m",
                what,
                response.baselineHpa,
                response.relAltM,
            )
        } else {
            "$what：固件返回空基准"
        }
        addLog(LogLevel.INFO, LogSource.BLE, "BARO", text)
        return BaroBaselineResult(ok = response.baselineValid, supported = true, message = text)
    }

    private fun calibLinkReady(tag: String): Boolean {
        if (transport.state.value == LinkState.READY) return true
        addLog(LogLevel.WARN, LogSource.BLE, tag, "链路未就绪，校准命令未发送")
        _calib.update { it.copy(magMessage = "链路未就绪", accelMessage = "链路未就绪") }
        return false
    }

    private fun cancelCalibPoll() {
        calibPollJob?.cancel()
        calibPollJob = null
    }

    /** Ends a running capture locally; used when the link goes away. */
    private fun dropCalibSession(message: String) {
        cancelCalibPoll()
        _calib.update {
            val magStopped =
                it.magPhase == CalibPhase.COLLECTING || it.magPhase == CalibPhase.SAVING
            val accelStopped =
                it.accelPhase == CalibPhase.COLLECTING || it.accelPhase == CalibPhase.SAVING
            it.copy(
                magPhase = if (magStopped) CalibPhase.IDLE else it.magPhase,
                accelPhase = if (accelStopped) CalibPhase.IDLE else it.accelPhase,
                magMessage = if (magStopped) message else it.magMessage,
                accelMessage = if (accelStopped) message else it.accelMessage,
                busy = false,
            )
        }
    }

    /**
     * Keeps exactly one QUERY loop alive while a capture runs. The firmware owns
     * the progress counter, so polling is the only way to see it without growing
     * the Status frame.
     */
    private fun resyncCalibPolling() {
        val state = _calib.value
        val magCollecting = state.magPhase == CalibPhase.COLLECTING
        val accelCollecting = state.accelPhase == CalibPhase.COLLECTING
        if (!magCollecting && !accelCollecting) {
            cancelCalibPoll()
            return
        }
        if (calibPollJob?.isActive == true) return
        calibPollJob = scope.launch {
            while (isActive) {
                delay(CALIB_POLL_PERIOD_MS)
                val snapshot = _calib.value
                if (snapshot.magPhase == CalibPhase.COLLECTING) {
                    applyMagResponse(
                        request(Opcode.MAG_CAL, CalibCodec.encodeMagCal(CalibAction.QUERY)),
                    )
                }
                if (snapshot.accelPhase == CalibPhase.COLLECTING) {
                    applyAccelResponse(
                        request(Opcode.ACCEL_CAL, CalibCodec.encodeAccelCal(CalibAction.QUERY)),
                    )
                }
                val after = _calib.value
                if (after.magPhase != CalibPhase.COLLECTING &&
                    after.accelPhase != CalibPhase.COLLECTING
                ) {
                    break
                }
            }
            calibPollJob = null
        }
    }

    private fun calibPhaseOf(state: Int, flags: Int): CalibPhase = when (state) {
        CalibStateCode.COLLECTING -> CalibPhase.COLLECTING
        CalibStateCode.RESULT_PENDING -> CalibPhase.REVIEW
        else -> if (flags and CalibFlagBits.FROM_STORE != 0) CalibPhase.SAVED else CalibPhase.IDLE
    }

    private fun applyMagResponse(result: CommandResult?) {
        if (result == null) {
            _calib.update {
                it.copy(magPhase = CalibPhase.FAILED, magMessage = "MAG_CAL 超时：设备没有响应")
            }
            return
        }
        when (result.status) {
            StatusCode.OK.code -> applyMagBody(result.payload)
            StatusCode.NOT_SUPPORTED.code, StatusCode.UNKNOWN_OPCODE.code -> _calib.update {
                it.copy(
                    magSupported = false,
                    magPhase = CalibPhase.UNSUPPORTED,
                    magMessage = "固件不支持 MAG_CAL（0x41 返回 ${StatusCode.labelOf(result.status)}）",
                )
            }
            else -> _calib.update {
                it.copy(
                    magPhase = CalibPhase.FAILED,
                    magMessage = "MAG_CAL 失败：${StatusCode.labelOf(result.status)}",
                )
            }
        }
    }

    private fun applyMagBody(body: ByteArray) {
        val response = CalibCodec.decodeMagCal(body)
        if (response == null) {
            _calib.update {
                it.copy(
                    magPhase = CalibPhase.FAILED,
                    magMessage = "MAG_CAL 响应体 ${body.size} B，短于约定的 " +
                        "${CalibCodec.MAG_RSP_SIZE} B",
                )
            }
            return
        }
        val phase = calibPhaseOf(response.state, response.flags)
        val result = MagCalResult(
            biasLsb = response.biasLsb,
            scalePpm = response.scalePpm,
            coveragePct = response.coveragePct,
            sampleCount = response.sampleCount,
            /* Per-sensor: the record holds both halves, so the magnetometer row
             * asks for the magnetometer bit (see CalibFlagBits.MAG_VALID).
             */
            fromStore = response.calibrated,
            lastCalibUnixS = response.lastCalibUnixS,
        )
        val hasCoefficients = phase == CalibPhase.REVIEW || phase == CalibPhase.SAVED
        _calib.update {
            it.copy(
                magSupported = true,
                magPhase = phase,
                magProgressPermille = response.progressPermille,
                magCoveragePct = response.coveragePct,
                magSampleCount = response.sampleCount,
                magResult = if (hasCoefficients) result else it.magResult,
                magVerdict = if (hasCoefficients) CalibJudge.judgeMag(result) else null,
                magMessage = magMessageFor(response, phase),
                lastCalibUnixS = if (response.lastCalibUnixS > 0L) {
                    response.lastCalibUnixS
                } else {
                    it.lastCalibUnixS
                },
            )
        }
    }

    private fun magMessageFor(response: MagCalResponse, phase: CalibPhase): String = when {
        response.saveFailed -> "系数已生效，但写入 flash 失败（重启后会回到上一次的值）"
        phase == CalibPhase.COLLECTING -> "正在采集：缓慢画 8 字，三条轴都要覆盖"
        phase == CalibPhase.REVIEW -> "已计算，尚未保存到设备"
        phase == CalibPhase.SAVED -> CalibFormat.lastCalib(response.lastCalibUnixS, true)
        response.usingDefaults -> "未校准：设备当前使用编译期默认值"
        else -> ""
    }

    private fun applyAccelResponse(result: CommandResult?) {
        if (result == null) {
            _calib.update {
                it.copy(
                    accelPhase = CalibPhase.FAILED,
                    accelMessage = "ACCEL_CAL 超时：设备没有响应",
                )
            }
            return
        }
        when (result.status) {
            StatusCode.OK.code -> applyAccelBody(result.payload)
            StatusCode.NOT_SUPPORTED.code, StatusCode.UNKNOWN_OPCODE.code -> _calib.update {
                it.copy(
                    accelSupported = false,
                    accelPhase = CalibPhase.UNSUPPORTED,
                    accelMessage = "固件不支持 ACCEL_CAL（0x43 返回 " +
                        "${StatusCode.labelOf(result.status)}）",
                )
            }
            else -> _calib.update {
                it.copy(
                    accelPhase = CalibPhase.FAILED,
                    accelMessage = "ACCEL_CAL 失败：${StatusCode.labelOf(result.status)}",
                )
            }
        }
    }

    private fun applyAccelBody(body: ByteArray) {
        val response = CalibCodec.decodeAccelCal(body)
        if (response == null) {
            _calib.update {
                it.copy(
                    accelPhase = CalibPhase.FAILED,
                    accelMessage = "ACCEL_CAL 响应体 ${body.size} B，短于约定的 " +
                        "${CalibCodec.ACCEL_RSP_SIZE} B",
                )
            }
            return
        }
        val phase = calibPhaseOf(response.state, response.flags)
        // A completed face ends the session on its own: the user places the device
        // on the next side and presses again, there is no "next face" command.
        val effectivePhase = if (phase == CalibPhase.COLLECTING &&
            response.faceState == AccelFaceStateCode.FACE_DONE
        ) {
            CalibPhase.IDLE
        } else {
            phase
        }
        val detectedFace = if (response.hasDetectedFace) response.detectedFace else CalibFaces.NONE
        val result = AccelCalResult(
            biasUg = response.biasUg,
            scalePpm = response.scalePpm,
            gyroBiasMdps = response.gyroBiasMdps,
            faceMask = response.faceMask,
            quickMode = response.quickMode,
            fromStore = response.calibrated,
            lastCalibUnixS = response.lastCalibUnixS,
        )
        val hasCoefficients = phase == CalibPhase.REVIEW || phase == CalibPhase.SAVED
        _calib.update {
            it.copy(
                accelSupported = true,
                accelPhase = effectivePhase,
                accelProgressPermille = response.progressPermille,
                accelFaceMask = response.faceMask,
                accelFaceState = response.faceState,
                accelRestPct = response.restPct,
                accelDetectedFace = detectedFace,
                accelResult = if (hasCoefficients) result else it.accelResult,
                accelVerdict = if (hasCoefficients) CalibJudge.judgeAccel(result) else null,
                accelMessage = accelMessageFor(response, effectivePhase),
                accelActivePosition = if (effectivePhase == CalibPhase.COLLECTING) {
                    it.accelActivePosition
                } else {
                    -1
                },
                lastCalibUnixS = if (response.lastCalibUnixS > 0L) {
                    response.lastCalibUnixS
                } else {
                    it.lastCalibUnixS
                },
            )
        }
    }

    private fun accelMessageFor(response: AccelCalResponse, phase: CalibPhase): String = when {
        response.saveFailed -> "系数已生效，但写入 flash 失败（重启后会回到上一次的值）"
        // A finished face is reported before the phase, because after a capture the
        // device sits in IDLE (or SAVED when an older record exists) and the phase
        // alone cannot tell the user what just happened.
        response.faceState == AccelFaceStateCode.FACE_DONE && response.hasDetectedFace ->
            "已识别并采集：${CalibFaces.label(response.detectedFace)}"
        response.faceState == AccelFaceStateCode.JITTER_TIMEOUT ->
            "本次没采到：设备没有静止，或姿态不够平（无法识别是哪一面）"
        phase == CalibPhase.COLLECTING -> when (response.faceState) {
            AccelFaceStateCode.WAIT_REST -> "等待设备静止…（静止余量 ${response.restPct} %）"
            AccelFaceStateCode.COLLECTING -> "正在采集本面（${response.progressPermille / 10} %）"
            else -> "采集中"
        }
        phase == CalibPhase.REVIEW -> "已计算，尚未保存到设备"
        phase == CalibPhase.SAVED -> CalibFormat.lastCalib(response.lastCalibUnixS, true)
        response.usingDefaults -> "未校准：设备当前使用编译期默认值"
        else -> ""
    }

    override fun clearLogs() {
        _logs.value = emptyList()
    }

    override fun clearConsole() {
        _console.value = emptyList()
    }

    override fun exportLogs() {
        val directory = appContext.getExternalFilesDir(null)
        if (directory == null) {
            addLog(LogLevel.ERROR, LogSource.USB, "EXPORT", "外部文件目录不可用（getExternalFilesDir 返回 null），未导出")
            return
        }
        scope.launch {
            val outcome = withContext(Dispatchers.IO) {
                runCatching {
                    if (!directory.exists() && !directory.mkdirs()) {
                        error("无法创建目录 ${directory.absolutePath}")
                    }
                    val target = File(directory, "life-gps-log-${System.currentTimeMillis()}.txt")
                    target.bufferedWriter(Charsets.UTF_8).use { writer ->
                        writer.write("# Life_GPS debug log\n")
                        _logs.value.forEach { entry ->
                            writer.write(
                                "${entry.time} ${entry.level.short} ${entry.source.label} ${entry.tag}: ${entry.message}\n",
                            )
                        }
                    }
                    target
                }
            }
            outcome.fold(
                onSuccess = { file ->
                    addLog(LogLevel.INFO, LogSource.USB, "EXPORT", "日志已写入 ${file.absolutePath}")
                },
                onFailure = { error ->
                    addLog(
                        LogLevel.ERROR,
                        LogSource.USB,
                        "EXPORT",
                        "写入失败：${error.message ?: error.javaClass.simpleName}",
                    )
                },
            )
        }
    }

    override fun addLog(level: LogLevel, source: LogSource, tag: String, message: String) {
        val entry = LogEntry(now(), level, source, tag, message)
        _logs.update { (it + entry).takeLast(MAX_LOG_ENTRIES) }
    }

    // --- helpers --------------------------------------------------------------

    private fun appendConsole(opcode: String, request: String, response: String) {
        val entry = ConsoleEntry(now(), opcode, request.ifBlank { "-" }, response)
        _console.update { (it + entry).takeLast(MAX_CONSOLE_ENTRIES) }
    }

    private fun finishConsole(entry: ConsoleEntry, response: String) {
        _console.update { list ->
            list.map { if (it === entry) it.copy(response = response) else it }
        }
        addLog(
            if (response.startsWith("00")) LogLevel.INFO else LogLevel.WARN,
            LogSource.BLE,
            "CMD",
            "${entry.opcode} -> $response",
        )
    }

    private fun describeResult(opcode: Opcode, result: CommandResult?): String {
        if (result == null) return "-- TIMEOUT（2 s 内未收到 Control 通知）"
        if (result.status < 0) return "-- RESPONSE WITHOUT STATUS BYTE"
        val head = String.format(Locale.US, "%02X %s", result.status, StatusCode.labelOf(result.status))
        if (result.status != StatusCode.OK.code) return head
        val detail = when (opcode) {
            Opcode.GET_CAPS -> BleFrame.parseCaps(result.payload)?.let { caps ->
                String.format(
                    Locale.US,
                    "proto %s fw %s mtu %d status %d B caps 0x%08X",
                    caps.protocolText,
                    caps.firmwareText,
                    caps.attMtu,
                    caps.statusSize,
                    caps.caps,
                )
            } ?: hexOrDash(result.payload)

            Opcode.GET_STATUS -> "status ${result.payload.size} B"

            Opcode.FLASH_INFO -> BleFrame.parseFlashInfo(result.payload)?.let { info ->
                "flash ${info.flashBytes} B / traj ${info.trajBytes} B / " +
                    "sectors ${info.sectorCount} / seq ${info.sectorSeq} / records ${info.recordsWritten}"
            } ?: hexOrDash(result.payload)

            Opcode.POI_MARK -> if (result.payload.isNotEmpty()) {
                "written=${BleFrame.u8(result.payload, 0)}"
            } else {
                "written=?"
            }

            else -> hexOrDash(result.payload)
        }
        return if (detail == "--") head else "$head | $detail"
    }

    private fun hexOrDash(bytes: ByteArray): String =
        if (bytes.isEmpty()) "--" else BleFrame.toHex(bytes)

    private fun parseHex(text: String): ByteArray? {
        val cleaned = text
            .replace(" ", "")
            .replace("\t", "")
            .replace("\n", "")
            .replace(",", "")
            .replace("0x", "", ignoreCase = true)
        if (cleaned.isEmpty()) return ByteArray(0)
        if (cleaned.length % 2 != 0) return null
        val out = ByteArray(cleaned.length / 2)
        for (index in out.indices) {
            val high = Character.digit(cleaned[index * 2], 16)
            val low = Character.digit(cleaned[index * 2 + 1], 16)
            if (high < 0 || low < 0) return null
            out[index] = ((high shl 4) or low).toByte()
        }
        return out
    }

    private fun now(): String {
        val synced = syncedClockMillis()
        if (synced != null) return LOCAL_CLOCK_FORMAT.format(Instant.ofEpochMilli(synced))
        return LocalTime.now().format(clock)
    }

    /**
     * The App's wall clock, taken from the receiver instead of the phone.
     *
     * A GNSS fix carries UTC, and a phone whose own clock is wrong would otherwise
     * stamp every log line with the wrong time. [clockSyncEpochS] is the last UTC
     * the receiver reported and [clockSyncAtMs] the phone's monotonic time at that
     * moment; the difference is extrapolated so the log keeps counting between two
     * 1 Hz frames. Returns null until a fix carried UTC, and the caller falls back
     * to the phone clock.
     */
    private fun syncedClockMillis(): Long? {
        val base = clockSyncEpochS
        if (base <= 0L) return null
        val elapsed = SystemClock.elapsedRealtime() - clockSyncAtMs
        return ((base + LIFEGPS_EPOCH_UNIX_OFFSET_S) * 1000L) + elapsed
    }

    private fun syncClock(epochS: Long, utcValid: Boolean) {
        if (!utcValid || epochS <= 0L) return
        clockSyncEpochS = epochS
        clockSyncAtMs = SystemClock.elapsedRealtime()
    }

    /**
     * GNSS time in the product's default zone.
     *
     * The device only knows UTC (the receiver has no concept of a civil time
     * zone), and this product ships in China, so the App renders it as UTC+8.
     * That is a deliberate constant, not the phone's zone: two phones in two zones
     * must show the same device time. The status row is labelled with the offset.
     */
    private fun formatUtc(epochS: Long): String {
        if (epochS <= 0L) return "--"
        // utc_epoch_s counts from the Life_GPS 2020 epoch, not from Unix.
        val unixSeconds = epochS + LIFEGPS_EPOCH_UNIX_OFFSET_S
        return try {
            LOCAL_FORMAT.format(Instant.ofEpochSecond(unixSeconds))
        } catch (error: RuntimeException) {
            "--"
        }
    }

    /**
     * One command response.
     *
     * [payload] is the body **after** the status byte - [handleResponseFrame] strips
     * it, and every decoder here (GnssCodec, CalibCodec, BleFrame.parse*) expects
     * the stripped form. Dropping another byte breaks every parse silently.
     */
    private data class CommandResult(val status: Int, val payload: ByteArray)

    private fun initialDevice(): DeviceSummary = DeviceSummary(
        name = "未连接",
        connected = false,
        firmware = "—",
        protocol = "—",
        batteryPct = 0,
        batteryMv = 0,
        charging = false,
        mode = "—",
        uptimeSeconds = 0,
    )

    private fun initialGnss(): GnssStatus = GnssStatus(
        state = "OFF",
        fix = false,
        sats = 0,
        pdop = 0.0,
        latitude = 0.0,
        longitude = 0.0,
        altitudeM = 0.0,
        speedKmh = 0.0,
        headingDeg = 0.0,
        utcTime = "--",
        ttff = "",
        satellites = emptyList(),
    )

    private fun initialImu(): ImuStatus = ImuStatus(
        ax = 0f,
        ay = 0f,
        az = 0f,
        gx = 0f,
        gy = 0f,
        gz = 0f,
        qw = 0f,
        qx = 0f,
        qy = 0f,
        qz = 0f,
        steps = 0,
        zupt = false,
        axSensor = 0f,
        aySensor = 0f,
        azSensor = 0f,
        gxSensor = 0f,
        gySensor = 0f,
        gzSensor = 0f,
        gravityXg = 0f,
        gravityYg = 0f,
        gravityZg = 0f,
        linearXg = 0f,
        linearYg = 0f,
        linearZg = 0f,
        imuTempC = 0f,
        magTempC = 0f,
        gravityValid = false,
        imuTempValid = false,
        magTempValid = false,
    )

    private fun initialMag(): MagStatus = MagStatus(
        mx = 0f,
        my = 0f,
        mz = 0f,
        norm = 0f,
        anomaly = false,
    )

    private fun initialBaro(): BaroStatus = BaroStatus(
        pressureHpa = 0f,
        temperatureC = 0f,
        altitudeM = 0f,
        floor = 0,
    )

    private fun initialFlash(): FlashStatus = FlashStatus(
        jedec = "",
        usedMb = 0f,
        totalMb = 0f,
        recordCount = 0,
        journal = "",
        sectors = emptyList(),
    )

    private fun pendingChecks(): List<CheckItem> = listOf(
        CheckItem("MCU", "等待 GET_CAPS（0x01）", CheckState.PENDING),
        CheckItem("外部 Flash", "等待 FLASH_INFO（0x60）", CheckState.PENDING),
        CheckItem("IMU", "等待 Status 帧", CheckState.PENDING),
        CheckItem("磁力计", "等待 Status 帧", CheckState.PENDING),
        CheckItem("传感器校准", "等待 GET_CAPS（0x01）", CheckState.PENDING),
        CheckItem("气压计", "等待 Status 帧", CheckState.PENDING),
        CheckItem("GNSS 模组", "等待 Status 帧", CheckState.PENDING),
        CheckItem("GNSS 天线", "固件 v1 未提供（无单星 C/N0）", CheckState.PENDING),
        CheckItem("电池 ADC", "等待 Status 帧", CheckState.PENDING),
        CheckItem("充电状态", "等待 Status 帧", CheckState.PENDING),
        CheckItem("按键", "等待 Status 帧", CheckState.PENDING),
        CheckItem("LED", "固件 v1 未提供 LED 状态", CheckState.PENDING),
        CheckItem("BLE", "等待 GET_CAPS（0x01）", CheckState.PENDING),
    )

    private companion object {
        const val COMMAND_TIMEOUT_MS = 2_000L

        /**
         * A GNSS_CFG (0x35) write is ACK + the CFG-SIGNAL restart margin (0.5 s)
         * + a readback that a 1 Hz receiver only sends at its next epoch, so the
         * flat 2 s command budget is too tight. The firmware itself waits up to
         * 2.4 s (`GNSS_CFG_SET_WAIT_MS` in `src/gnss/gnss.c`).
         */
        const val GNSS_CFG_TIMEOUT_MS = 4_000L

        /** QUERY cadence while a capture runs; 30 Hz sampling needs ~2.5 Hz of polling. */
        const val CALIB_POLL_PERIOD_MS = 400L

        /** FINISH_SAVE computes the coefficients and writes the flash record. */
        const val CALIB_SAVE_TIMEOUT_MS = 6_000L

        const val CALIB_CLEAR_TIMEOUT_MS = 4_000L

        /** acc_lsb unit is 0.122 mg/LSB at ±4 g; convert to g. */
        const val ACC_G_PER_LSB = 0.122f / 1000f

        /** gyro_lsb unit is 35 mdps/LSB at ±1000 dps; convert to °/s. */
        const val GYRO_DPS_PER_LSB = 35f / 1000f

        /** LIS2MDL: 1.5 mG/LSB = 0.15 µT/LSB. */
        const val MAG_UT_PER_LSB = 0.15f

        /** Attitude is pushed into Compose at ~15 Hz even though the IMU sends 30 Hz. */
        const val ATTITUDE_UI_PERIOD_MS = 66L

        const val MAX_GEO_POINTS = 2_000
        const val MAX_TRAIL_POINTS = 600
        const val MAX_LOG_ENTRIES = 300
        const val MAX_CONSOLE_ENTRIES = 40

        /** NMEA lines kept in memory; a 1 Hz stream with ~15 sentences/s fills this in ~30 s. */
        const val MAX_NMEA_LINES = 500

        const val PENDING_RESPONSE = "-- 等待响应…"

        /** Seconds between the Life_GPS 2020 epoch and the Unix epoch. */
        const val LIFEGPS_EPOCH_UNIX_OFFSET_S = 1_577_836_800L

        /**
         * Default zone of the GNSS time on screen: UTC+8 (China Standard Time, no
         * daylight saving). The receiver reports UTC only; this is the one place
         * the offset from UTC to the product's civil time is decided.
         */
        const val DEFAULT_UTC_OFFSET_HOURS = 8

        val LOCAL_FORMAT: DateTimeFormatter =
            DateTimeFormatter.ofPattern("HH:mm:ss")
                .withZone(ZoneOffset.ofHours(DEFAULT_UTC_OFFSET_HOURS))

        val LOCAL_CLOCK_FORMAT: DateTimeFormatter =
            DateTimeFormatter.ofPattern("HH:mm:ss.SSS")
                .withZone(ZoneOffset.ofHours(DEFAULT_UTC_OFFSET_HOURS))
    }
}
