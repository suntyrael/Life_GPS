package com.lifegps.app.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.ParcelUuid
import androidx.core.content.ContextCompat
import com.lifegps.app.debug.ScannedDevice
import java.util.Locale
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Connection state of the GATT link, driven only by platform callbacks. */
sealed class LinkState {
    object DISCONNECTED : LinkState()
    object CONNECTING : LinkState()
    object CONNECTED : LinkState()
    object READY : LinkState()
    data class ERROR(val message: String) : LinkState()
}


/**
 * Thin wrapper around the Android BLE APIs.
 *
 * Runtime permissions are checked before every scan/connect (`scanPermissionGranted`
 * / `connectPermissionGranted`) and the checks are mirrored by the UI gate; the
 * class-level lint suppression is only there because lint cannot see through
 * those helpers.
 *
 * Responsibilities kept here (and deliberately not in the repository):
 *  - scanning with a Service-UUID filter, falling back to an unfiltered scan;
 *  - the connection setup order: connected -> requestMtu -> discoverServices ->
 *    CCCD subscriptions -> READY;
 *  - serialising GATT operations so `requestMtu` and `discoverServices` never
 *    race (the stack only allows one outstanding request).
 *
 * Decoding is not done here: characteristic values are forwarded as raw bytes.
 */
@SuppressLint("MissingPermission")
class BleTransport(context: Context) {

    private val appContext: Context = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val writeMutex = Mutex()

    private val _state = MutableStateFlow<LinkState>(LinkState.DISCONNECTED)
    val state: StateFlow<LinkState> = _state.asStateFlow()

    private val _scanning = MutableStateFlow(false)
    val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    private val _scanError = MutableStateFlow<String?>(null)
    val scanError: StateFlow<String?> = _scanError.asStateFlow()

    /** Last negotiated ATT MTU; 0 until the stack reports one. */
    var connectionMtu: Int = 0
        private set

    var onDeviceFound: ((ScannedDevice) -> Unit)? = null
    var onStatus: ((ByteArray) -> Unit)? = null
    var onLog: ((ByteArray) -> Unit)? = null

    /** Raw response frames from the Control characteristic. */
    var onFrame: ((ByteArray) -> Unit)? = null

    @Volatile
    private var closed = false

    @Volatile
    private var gatt: BluetoothGatt? = null

    @Volatile
    private var statusChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var controlChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var logChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var cccdFailed = false

    @Volatile
    private var foundCount = 0

    @Volatile
    private var filteredScan = true

    private var scanTimeoutJob: Job? = null

    // --- serialised GATT operation queue -------------------------------------

    private val queueLock = Any()
    private val gattQueue = ArrayDeque<() -> Boolean>()
    private var queueBusy = false

    private fun enqueueOp(op: () -> Boolean) {
        synchronized(queueLock) { gattQueue.addLast(op) }
        drainQueue()
    }

    private fun drainQueue() {
        while (true) {
            val op: () -> Boolean
            synchronized(queueLock) {
                if (queueBusy) return
                if (gattQueue.isEmpty()) return
                op = gattQueue.removeFirst()
                queueBusy = true
            }
            val accepted = try {
                op()
            } catch (error: SecurityException) {
                _state.value = LinkState.ERROR("缺少蓝牙权限：${error.message ?: "SecurityException"}")
                false
            }
            if (accepted) return
            synchronized(queueLock) { queueBusy = false }
        }
    }

    private fun completeOp() {
        synchronized(queueLock) { queueBusy = false }
        drainQueue()
    }

    private fun resetQueue() {
        synchronized(queueLock) {
            gattQueue.clear()
            queueBusy = false
        }
    }

    // --- scanning -------------------------------------------------------------

    /**
     * Starts a 10 s scan. Results are filtered by the Life_GPS Service UUID; if
     * nothing is seen after a few seconds the scan restarts unfiltered so that
     * devices whose advertising packet is malformed still show up.
     */
    fun scan(onDevice: (ScannedDevice) -> Unit) {
        if (closed || _scanning.value) return
        onDeviceFound = onDevice
        _scanError.value = null

        val adapter = adapter()
        if (adapter == null) {
            _scanError.value = "此设备没有蓝牙适配器"
            return
        }
        if (!scanPermissionGranted()) {
            _scanError.value = "缺少蓝牙扫描权限"
            return
        }
        val enabled = try {
            adapter.isEnabled
        } catch (error: SecurityException) {
            _scanError.value = "读取蓝牙状态失败：缺少权限"
            return
        }
        if (!enabled) {
            _scanError.value = "蓝牙未开启"
            return
        }
        val scanner = try {
            adapter.bluetoothLeScanner
        } catch (error: SecurityException) {
            null
        }
        if (scanner == null) {
            _scanError.value = "BLE 扫描器不可用"
            return
        }

        foundCount = 0
        filteredScan = true
        _scanning.value = true
        if (!startScan(scanner, filtered = true)) {
            filteredScan = false
            if (!startScan(scanner, filtered = false)) {
                _scanning.value = false
                _scanError.value = "startScan 调用失败"
                return
            }
        }
        scanTimeoutJob?.cancel()
        scanTimeoutJob = scope.launch {
            delay(SCAN_FILTER_FALLBACK_MS)
            if (!_scanning.value) return@launch
            if (filteredScan && foundCount == 0) {
                val current = try {
                    adapter.bluetoothLeScanner
                } catch (error: SecurityException) {
                    null
                }
                if (current != null) {
                    try {
                        current.stopScan(scanCallback)
                    } catch (ignored: SecurityException) {
                        // ignore: the scan will be stopped by the timeout anyway
                    }
                    filteredScan = false
                    startScan(current, filtered = false)
                }
            }
            delay(SCAN_TIMEOUT_MS - SCAN_FILTER_FALLBACK_MS)
            if (_scanning.value) stopScan()
        }
    }

    fun stopScan() {
        scanTimeoutJob?.cancel()
        scanTimeoutJob = null
        val scanner = try {
            adapter()?.bluetoothLeScanner
        } catch (error: SecurityException) {
            null
        }
        if (scanner != null) {
            try {
                scanner.stopScan(scanCallback)
            } catch (ignored: SecurityException) {
                // ignore
            } catch (ignored: IllegalStateException) {
                // Bluetooth was turned off while scanning
            }
        }
        _scanning.value = false
    }

    private fun startScan(scanner: BluetoothLeScanner, filtered: Boolean): Boolean {
        val filters: List<ScanFilter> = if (filtered) {
            listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(BleUuids.SERVICE)).build())
        } else {
            emptyList()
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        return try {
            scanner.startScan(filters, settings, scanCallback)
            true
        } catch (ignored: SecurityException) {
            false
        } catch (ignored: IllegalStateException) {
            false
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            foundCount++
            val device = result.device
            val advertised = result.scanRecord?.deviceName?.takeIf { it.isNotBlank() }
            val platformName = advertised ?: try {
                device.name
            } catch (error: SecurityException) {
                null
            }
            onDeviceFound?.invoke(
                ScannedDevice(
                    name = platformName.orEmpty(),
                    address = device.address,
                    rssi = result.rssi,
                )
            )
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { onScanResult(ScanSettings.CALLBACK_TYPE_ALL_MATCHES, it) }
        }

        override fun onScanFailed(errorCode: Int) {
            _scanError.value = "扫描失败（ScanCallback error=$errorCode）"
            stopScan()
        }
    }

    // --- connection -----------------------------------------------------------

    fun connect(address: String) {
        if (closed) return
        stopScan()
        val adapter = adapter()
        if (adapter == null) {
            _state.value = LinkState.ERROR("此设备没有蓝牙适配器")
            return
        }
        if (!connectPermissionGranted()) {
            _state.value = LinkState.ERROR("缺少 BLUETOOTH_CONNECT 权限")
            return
        }
        val enabled = try {
            adapter.isEnabled
        } catch (error: SecurityException) {
            _state.value = LinkState.ERROR("读取蓝牙状态失败：缺少权限")
            return
        }
        if (!enabled) {
            _state.value = LinkState.ERROR("蓝牙未开启")
            return
        }
        val device = try {
            adapter.getRemoteDevice(address)
        } catch (error: IllegalArgumentException) {
            _state.value = LinkState.ERROR("MAC 地址无效：$address")
            return
        }

        forgetGatt(gatt, disconnectFirst = true)
        cccdFailed = false
        connectionMtu = 0
        _state.value = LinkState.CONNECTING
        try {
            gatt = device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } catch (error: SecurityException) {
            _state.value = LinkState.ERROR("缺少 BLUETOOTH_CONNECT 权限")
        }
    }

    fun disconnect() {
        val current = gatt
        if (current == null) {
            _state.value = LinkState.DISCONNECTED
            return
        }
        try {
            current.disconnect()
        } catch (error: SecurityException) {
            _state.value = LinkState.ERROR("缺少 BLUETOOTH_CONNECT 权限")
        }
    }

    private fun forgetGatt(target: BluetoothGatt?, disconnectFirst: Boolean) {
        if (target != null && gatt === target) {
            gatt = null
            statusChar = null
            controlChar = null
            logChar = null
        }
        if (target != null) {
            if (disconnectFirst) {
                try {
                    target.disconnect()
                } catch (ignored: Throwable) {
                    // already gone
                }
            }
            try {
                target.close()
            } catch (ignored: Throwable) {
                // already closed
            }
        }
        resetQueue()
    }

    private fun enqueueHandshake() {
        enqueueOp {
            val current = gatt ?: return@enqueueOp false
            current.requestMtu(REQUESTED_MTU)
        }
        enqueueOp {
            val current = gatt ?: return@enqueueOp false
            current.discoverServices()
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g !== gatt) {
                // A callback from a previous connection after reconnect/disconnect.
                try {
                    g.close()
                } catch (ignored: Throwable) {
                    // ignore
                }
                return
            }
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                _state.value = LinkState.CONNECTED
                resetQueue()
                enqueueHandshake()
                return
            }
            if (newState == BluetoothProfile.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                forgetGatt(g, disconnectFirst = false)
                _state.value = if (status == BluetoothGatt.GATT_SUCCESS) {
                    LinkState.DISCONNECTED
                } else {
                    LinkState.ERROR(String.format(Locale.US, "GATT 错误 0x%02X", status))
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (g !== gatt) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                connectionMtu = mtu
            }
            completeOp()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (g !== gatt) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                _state.value = LinkState.ERROR(String.format(Locale.US, "服务发现失败 0x%02X", status))
                completeOp()
                return
            }
            val service = g.getService(BleUuids.SERVICE)
            if (service == null) {
                _state.value = LinkState.ERROR("未找到 Life_GPS 服务（UUID ${BleUuids.SERVICE}）")
                completeOp()
                return
            }
            statusChar = service.getCharacteristic(BleUuids.STATUS)
            controlChar = service.getCharacteristic(BleUuids.CONTROL)
            logChar = service.getCharacteristic(BleUuids.LOG)
            if (controlChar == null) {
                _state.value = LinkState.ERROR("缺少 Control 特征（无法收发命令）")
                completeOp()
                return
            }
            completeOp()
            if (statusChar != null) {
                enqueueOp { writeCccd(BleUuids.STATUS) }
            }
            if (logChar != null) {
                enqueueOp { writeCccd(BleUuids.LOG) }
            }
            if (controlChar != null) {
                enqueueOp { writeCccd(BleUuids.CONTROL) }
            }
            enqueueOp {
                _state.value = if (cccdFailed) {
                    LinkState.ERROR("CCCD 订阅失败，收不到通知")
                } else {
                    LinkState.READY
                }
                false
            }
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            if (g !== gatt) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                cccdFailed = true
            }
            completeOp()
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (g !== gatt) return
            val value = characteristic.value ?: return
            when (characteristic.uuid) {
                BleUuids.STATUS -> onStatus?.invoke(value.copyOf())
                BleUuids.LOG -> onLog?.invoke(value.copyOf())
                BleUuids.CONTROL -> onFrame?.invoke(value.copyOf())
                else -> Unit
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun writeCccd(uuid: UUID): Boolean {
        val current = gatt ?: return false
        val service = current.getService(BleUuids.SERVICE) ?: return false
        val characteristic = service.getCharacteristic(uuid) ?: return false
        if (!current.setCharacteristicNotification(characteristic, true)) return false
        val descriptor = characteristic.getDescriptor(BleUuids.CCCD) ?: return false
        descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        return current.writeDescriptor(descriptor)
    }

    // --- command writes -------------------------------------------------------

    /**
     * Writes one command frame to Control. Write-without-response is preferred;
     * when the stack refuses to queue it, the write is retried as an acknowledged
     * write with the same bytes.
     */
    fun send(bytes: ByteArray) {
        if (closed) return
        scope.launch {
            writeMutex.withLock { writeNow(bytes) }
        }
    }

    @Suppress("DEPRECATION")
    private fun writeNow(bytes: ByteArray) {
        val current = gatt ?: return
        val characteristic = controlChar ?: return
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        characteristic.value = bytes
        val queued = try {
            current.writeCharacteristic(characteristic)
        } catch (error: SecurityException) {
            false
        }
        if (queued) return
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        characteristic.value = bytes
        try {
            current.writeCharacteristic(characteristic)
        } catch (ignored: SecurityException) {
            // Dropped: the caller times out and reports "-- TIMEOUT" to the user.
        }
    }

    /** Releases the GATT client and stops scanning. Safe to call repeatedly. */
    fun close() {
        if (closed) return
        closed = true
        stopScan()
        scope.cancel()
        forgetGatt(gatt, disconnectFirst = true)
        _state.value = LinkState.DISCONNECTED
    }

    // --- platform helpers -----------------------------------------------------

    private fun adapter(): BluetoothAdapter? =
        (appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private fun hasPermission(permission: String): Boolean =
        ContextCompat.checkSelfPermission(appContext, permission) == PackageManager.PERMISSION_GRANTED

    private fun scanPermissionGranted(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            hasPermission(Manifest.permission.BLUETOOTH_SCAN)
        } else {
            hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private fun connectPermissionGranted(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            hasPermission(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            true
        }

    private companion object {
        const val REQUESTED_MTU = 247
        const val SCAN_TIMEOUT_MS = 10_000L
        const val SCAN_FILTER_FALLBACK_MS = 4_000L
    }
}
