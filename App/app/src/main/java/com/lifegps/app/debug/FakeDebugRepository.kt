package com.lifegps.app.debug

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.math.cos
import kotlin.math.sin

/**
 * 假设备数据源。
 *
 * 目的：在固件/BLE 就绪前，让硬件调试界面可以完整跑起来。
 * 后续把同名的 StateFlow 换成 Transport/BLE 仓库即可，UI 不需要改。
 */
class FakeDebugRepository {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val clock = DateTimeFormatter.ofPattern("HH:mm:ss.SSS")

    private val _device = MutableStateFlow(
        DeviceSummary(
            name = "LGPS-3F2A",
            connected = true,
            firmware = "fw 0.1.0-dev",
            protocol = "proto 0.1",
            batteryPct = 86,
            batteryMv = 4012,
            charging = false,
            mode = "ADAPTIVE",
            uptimeSeconds = 12_345,
        )
    )
    val device: StateFlow<DeviceSummary> = _device.asStateFlow()

    private val _gnss = MutableStateFlow(
        GnssStatus(
            state = "TRACKING",
            fix = true,
            sats = 18,
            hdop = 0.8,
            latitude = 31.230416,
            longitude = 121.473701,
            altitudeM = 12.4,
            speedKmh = 4.6,
            headingDeg = 87.3,
            utcTime = "2026-10-26 09:58:12",
            ttff = "3.2 s",
            satellites = listOf(
                SatelliteInfo(3, "GPS", 42, 61, 214, true),
                SatelliteInfo(8, "GPS", 40, 48, 132, true),
                SatelliteInfo(14, "GPS", 38, 35, 301, true),
                SatelliteInfo(22, "GPS", 36, 28, 77, true),
                SatelliteInfo(27, "GPS", 31, 19, 156, true),
                SatelliteInfo(11, "GAL", 39, 55, 188, true),
                SatelliteInfo(19, "GAL", 34, 32, 245, true),
                SatelliteInfo(24, "GAL", 28, 14, 118, false),
                SatelliteInfo(16, "BDS", 37, 44, 201, true),
                SatelliteInfo(27, "BDS", 33, 26, 96, true),
                SatelliteInfo(9, "GLO", 30, 22, 311, false),
                SatelliteInfo(18, "GLO", 26, 12, 64, false),
            ),
        )
    )
    val gnss: StateFlow<GnssStatus> = _gnss.asStateFlow()

    private val _imu = MutableStateFlow(
        ImuStatus(
            ax = 0.02f, ay = -0.01f, az = 0.99f,
            gx = 0.4f, gy = -0.3f, gz = 0.2f,
            qw = 0.998f, qx = 0.01f, qy = -0.02f, qz = 0.03f,
            steps = 1284,
            zupt = false,
        )
    )
    val imu: StateFlow<ImuStatus> = _imu.asStateFlow()

    private val _mag = MutableStateFlow(
        MagStatus(mx = 21.4f, my = -8.7f, mz = 42.1f, norm = 48.2f, anomaly = false)
    )
    val mag: StateFlow<MagStatus> = _mag.asStateFlow()

    private val _baro = MutableStateFlow(
        BaroStatus(pressureHpa = 1013.2f, temperatureC = 26.4f, altitudeM = 11.8f, floor = 0)
    )
    val baro: StateFlow<BaroStatus> = _baro.asStateFlow()

    private val _flash = MutableStateFlow(
        FlashStatus(
            jedec = "EF 40 20",
            usedMb = 128.4f,
            totalMb = 63f,
            recordCount = 4_218,
            beaconCount = 327,
            journal = "COMMITTED",
            sectors = List(16) { index ->
                when {
                    index < 11 -> "ARCHIVED"
                    index == 11 -> "WRITING"
                    else -> "ERASED"
                }
            },
        )
    )
    val flash: StateFlow<FlashStatus> = _flash.asStateFlow()

    private val _checks = MutableStateFlow(defaultChecks())
    val checks: StateFlow<List<CheckItem>> = _checks.asStateFlow()

    private val _checkRunning = MutableStateFlow(false)
    val checkRunning: StateFlow<Boolean> = _checkRunning.asStateFlow()

    private val _logs = MutableStateFlow<List<LogEntry>>(emptyList())
    val logs: StateFlow<List<LogEntry>> = _logs.asStateFlow()

    private val _console = MutableStateFlow<List<ConsoleEntry>>(emptyList())
    val console: StateFlow<List<ConsoleEntry>> = _console.asStateFlow()

    private val _devices = MutableStateFlow<List<FakeDevice>>(emptyList())
    val devices: StateFlow<List<FakeDevice>> = _devices.asStateFlow()

    private val _scanning = MutableStateFlow(false)
    val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    /** 地图实时轨迹：GNSS 经纬度点。 */
    private val _geoTrail = MutableStateFlow<List<GeoPoint>>(emptyList())
    val geoTrail: StateFlow<List<GeoPoint>> = _geoTrail.asStateFlow()

    private val _liveTracking = MutableStateFlow(true)
    val liveTracking: StateFlow<Boolean> = _liveTracking.asStateFlow()

    /** 传感器空间轨迹：IMU/PDR 局部坐标（米）。 */
    private val _trail = MutableStateFlow<List<TrailPoint>>(emptyList())
    val trail: StateFlow<List<TrailPoint>> = _trail.asStateFlow()

    private val _trailRunning = MutableStateFlow(true)
    val trailRunning: StateFlow<Boolean> = _trailRunning.asStateFlow()

    private val _pois = MutableStateFlow<List<PoiMarker>>(emptyList())
    val pois: StateFlow<List<PoiMarker>> = _pois.asStateFlow()

    private var nextPoiId = 1
    private var tick = 0

    init {
        seedLogs()
        seedPois()
        scope.launch {
            while (true) {
                delay(1_000)
                tick++
                updateTelemetry()
                if (tick % 5 == 0) {
                    addLog(
                        LogLevel.DEBUG,
                        LogSource.BLE,
                        "PWR",
                        "batt=${_device.value.batteryMv}mV pct=${_device.value.batteryPct}%",
                    )
                }
            }
        }
    }

    private fun seedLogs() {
        _logs.value = listOf(
            LogEntry(now(), LogLevel.INFO, LogSource.USB, "BOOT", "LIFE GPS mock device boot"),
            LogEntry(now(), LogLevel.INFO, LogSource.USB, "FLASH", "JEDEC EF 40 20, 64MB"),
            LogEntry(now(), LogLevel.INFO, LogSource.BLE, "BLE", "advertising as LGPS-3F2A"),
            LogEntry(now(), LogLevel.INFO, LogSource.BLE, "GATT", "Status/Control/Data/Log ready"),
            LogEntry(now(), LogLevel.DEBUG, LogSource.USB, "IMU", "LSM6DSVTR WHO_AM_I=0x70"),
            LogEntry(now(), LogLevel.WARN, LogSource.BLE, "GNSS", "cold start: waiting for ephemeris"),
            LogEntry(now(), LogLevel.INFO, LogSource.BLE, "GNSS", "fix acquired, sats=18 hdop=0.8"),
        )
    }

    private fun seedPois() {
        _pois.value = listOf(
            PoiMarker(
                id = nextPoiId++,
                latitude = 31.23050,
                longitude = 121.47390,
                label = "P1",
                note = "示例营地",
                time = now(),
            )
        )
    }

    private fun updateTelemetry() {
        val phase = tick / 6.0
        val lat = 31.230416 + sin(phase) * 0.00012
        val lon = 121.473701 + sin(phase / 2.0) * 0.00018
        _gnss.update {
            it.copy(
                latitude = lat,
                longitude = lon,
                speedKmh = 4.2 + sin(phase) * 0.8,
                headingDeg = (87.0 + sin(phase) * 6.0) % 360.0,
                sats = 17 + (tick % 3),
                hdop = 0.7 + (tick % 4) * 0.1,
                satellites = it.satellites.mapIndexed { index, satellite ->
                    satellite.copy(
                        cno = (satellite.cno + ((tick + index) % 3) - 1).coerceIn(12, 48),
                    )
                },
            )
        }

        if (_liveTracking.value) {
            _geoTrail.update { points ->
                (points + GeoPoint(lat, lon, tick.toLong())).takeLast(2_000)
            }
        }

        if (_trailRunning.value) {
            val x = (sin(phase * 0.6) * 2.0 + phase * 0.04).toFloat()
            val y = (cos(phase * 0.45) * 1.5).toFloat()
            val z = (sin(phase * 0.9) * 0.7).toFloat()
            _trail.update { points ->
                (points + TrailPoint(x, y, z, tick.toLong())).takeLast(600)
            }
        }

        _imu.update {
            it.copy(
                ax = (sin(phase) * 0.04).toFloat(),
                ay = (sin(phase / 2.0) * 0.03).toFloat(),
                az = (0.99 + sin(phase / 3.0) * 0.01).toFloat(),
                gx = (sin(phase) * 0.8).toFloat(),
                gy = (sin(phase / 2.0) * 0.6).toFloat(),
                gz = (sin(phase / 3.0) * 0.4).toFloat(),
                qw = (0.998 + sin(phase) * 0.001).toFloat(),
                qx = (sin(phase) * 0.01).toFloat(),
                qy = (sin(phase / 2.0) * 0.02).toFloat(),
                qz = (sin(phase / 3.0) * 0.03).toFloat(),
            )
        }
        _mag.update {
            it.copy(
                mx = (21.4 + sin(phase) * 0.8).toFloat(),
                my = (-8.7 + sin(phase / 2.0) * 0.5).toFloat(),
                mz = (42.1 + sin(phase / 3.0) * 0.6).toFloat(),
            )
        }
        _baro.update {
            it.copy(
                pressureHpa = (1013.2 + sin(phase / 4.0) * 0.3).toFloat(),
                temperatureC = (26.4 + sin(phase / 5.0) * 0.2).toFloat(),
                altitudeM = (11.8 + sin(phase / 4.0) * 0.2).toFloat(),
            )
        }
        _device.update {
            it.copy(
                uptimeSeconds = it.uptimeSeconds + 1,
                batteryMv = 4012 - (tick / 120),
                batteryPct = (86 - tick / 600).coerceAtLeast(0),
            )
        }
    }

    fun runSelfTest() {
        if (_checkRunning.value) return
        _checkRunning.value = true
        _checks.update { items -> items.map { it.copy(state = CheckState.RUNNING) } }
        addLog(LogLevel.INFO, LogSource.BLE, "SELFTEST", "self-test started")
        scope.launch {
            delay(900)
            _checks.value = defaultChecks().map { item ->
                if (item.name == "GNSS 天线") {
                    item.copy(state = CheckState.PENDING, detail = "需在开阔天空下确认 C/N0")
                } else {
                    item.copy(state = CheckState.PASS)
                }
            }
            _checkRunning.value = false
            addLog(LogLevel.INFO, LogSource.BLE, "SELFTEST", "done: 11 PASS / 1 PENDING")
        }
    }

    fun connect(device: FakeDevice) {
        _device.update { it.copy(name = device.name, connected = true) }
        addLog(LogLevel.INFO, LogSource.BLE, "BLE", "connected to ${device.name} rssi=${device.rssi}")
    }

    fun disconnect() {
        _device.update { it.copy(connected = false) }
        addLog(LogLevel.WARN, LogSource.BLE, "BLE", "disconnected")
    }

    fun scan() {
        if (_scanning.value) return
        _scanning.value = true
        _devices.value = emptyList()
        addLog(LogLevel.DEBUG, LogSource.BLE, "SCAN", "scanning for LGPS service...")
        scope.launch {
            delay(800)
            _devices.value = listOf(
                FakeDevice("LGPS-3F2A", -48, "fw 0.1.0-dev"),
                FakeDevice("LGPS-91C4", -67, "fw 0.1.0-dev"),
                FakeDevice("LGPS-A07B", -82, "fw 0.0.9"),
            )
            _scanning.value = false
            addLog(LogLevel.INFO, LogSource.BLE, "SCAN", "found 3 devices")
        }
    }

    fun markPoi() {
        val current = _gnss.value
        addPoi(current.latitude, current.longitude, "快捷打点")
    }

    fun addPoi(latitude: Double, longitude: Double, note: String = "") {
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

    fun removePoi(id: Int) {
        _pois.update { list -> list.filterNot { it.id == id } }
        addLog(LogLevel.INFO, LogSource.BLE, "POI", "removed P$id")
    }

    fun setLiveTracking(enabled: Boolean) {
        _liveTracking.value = enabled
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "MAP",
            if (enabled) "real-time trajectory resumed" else "real-time trajectory paused",
        )
    }

    fun clearGeoTrail() {
        _geoTrail.value = emptyList()
        addLog(LogLevel.INFO, LogSource.BLE, "MAP", "geo trail cleared")
    }

    fun setTrailRunning(enabled: Boolean) {
        _trailRunning.value = enabled
        addLog(
            LogLevel.INFO,
            LogSource.BLE,
            "PDR",
            if (enabled) "spatial trail resumed" else "spatial trail paused",
        )
    }

    fun clearTrail() {
        _trail.value = emptyList()
        addLog(LogLevel.INFO, LogSource.BLE, "PDR", "spatial trail cleared")
    }

    fun sendCommand(opcode: String, payload: String): ConsoleEntry {
        val response = when (opcode) {
            "GET_STATUS" -> "00 OK | mode=ADAPTIVE batt=86% sats=18 flash=128.4/63MB"
            "GET_CAPS" -> "00 OK | proto=0.1 fw=0.1.0 mtu=247 streams=5"
            "SET_TIME" -> "00 OK | clock_offset=+12ms"
            "POI_MARK" -> "00 OK | poi_id=${nextPoiId - 1}"
            "SELFTEST" -> "00 OK | 11 pass / 1 pending"
            "GET_DAY_INDEX" -> "00 OK | days=127 last_day=2026-10-26"
            else -> "02 UNKNOWN_OP"
        }
        val entry = ConsoleEntry(
            time = now(),
            opcode = opcode,
            request = payload.ifBlank { "-" },
            response = response,
        )
        _console.update { (it + entry).takeLast(40) }
        addLog(LogLevel.INFO, LogSource.BLE, "CMD", "$opcode -> $response")
        return entry
    }

    fun clearLogs() {
        _logs.value = emptyList()
    }

    fun clearConsole() {
        _console.value = emptyList()
    }

    fun exportLogs() {
        addLog(LogLevel.INFO, LogSource.USB, "EXPORT", "日志已导出（模拟）：/sdcard/Download/life-gps-log.txt")
    }

    fun addLog(level: LogLevel, source: LogSource, tag: String, message: String) {
        val entry = LogEntry(now(), level, source, tag, message)
        _logs.update { (it + entry).takeLast(300) }
    }

    private fun now(): String = LocalTime.now().format(clock)

    private fun defaultChecks(): List<CheckItem> = listOf(
        CheckItem("MCU", "nRF52840 / 64MHz / 1MB Flash", CheckState.PASS),
        CheckItem("外部 Flash", "W25Q512NW JEDEC EF 40 20 / 64MB", CheckState.PASS),
        CheckItem("IMU", "LSM6DSVTR WHO_AM_I 0x70", CheckState.PASS),
        CheckItem("磁力计", "LIS2MDL WHO_AM_I 0x40", CheckState.PASS),
        CheckItem("气压计", "BMP388 CHIP_ID 0x50", CheckState.PASS),
        CheckItem("GNSS 模组", "MAX-M10S UBX-MON-VER", CheckState.PASS),
        CheckItem("GNSS 天线", "C/N0 与定位质量", CheckState.PENDING),
        CheckItem("电池 ADC", "4012mV / 分压比 0.2481", CheckState.PASS),
        CheckItem("充电状态", "SGM4056 /CHG = HIGH（未充电）", CheckState.PASS),
        CheckItem("按键", "SW1 P1.08 / SW7 P0.08", CheckState.PASS),
        CheckItem("LED", "R/G/B 三色灯语", CheckState.PASS),
        CheckItem("BLE", "2M PHY / MTU 247 / bonded", CheckState.PASS),
    )
}
