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
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.ParcelUuid
import androidx.core.content.ContextCompat
import com.lifegps.app.debug.ScannedDevice
import com.lifegps.core.model.BondState
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
 * Responsibilities:
 *  - scanning with a Service-UUID filter, falling back to an unfiltered scan;
 *  - connection setup: connected -> requestMtu(247) -> discoverServices ->
 *    CCCD subscriptions -> READY (plus best-effort 2M PHY request);
 *  - serialising GATT operations so only one request is outstanding at a time;
 *  - observing platform BLE bonding state (`ACTION_BOND_STATE_CHANGED`);
 *  - gating debug-only Telemetry / Log CCCDs via [setDebugStreamsEnabled].
 */
@SuppressLint("MissingPermission")
class BleTransport(
    context: Context,
    initialDebugStreamsEnabled: Boolean = true,
) : LifeGpsTransport {

    private val appContext: Context = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val writeMutex = Mutex()

    private val _state = MutableStateFlow<LinkState>(LinkState.DISCONNECTED)
    override val state: StateFlow<LinkState> = _state.asStateFlow()

    private val _bondState = MutableStateFlow(BondState.NONE)
    override val bondState: StateFlow<BondState> = _bondState.asStateFlow()

    private val _scanning = MutableStateFlow(false)
    override val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    private val _scanError = MutableStateFlow<String?>(null)
    override val scanError: StateFlow<String?> = _scanError.asStateFlow()

    /** Last negotiated ATT MTU; 0 until the stack reports one. */
    override var connectionMtu: Int = 0
        private set

    override var onDeviceFound: ((ScannedDevice) -> Unit)? = null
    override var onStatus: ((ByteArray) -> Unit)? = null
    override var onLog: ((ByteArray) -> Unit)? = null
    override var onFrame: ((ByteArray) -> Unit)? = null
    override var onTelemetry: ((ByteArray) -> Unit)? = null
    override var onData: ((ByteArray) -> Unit)? = null

    @Volatile
    private var debugStreamsEnabled: Boolean = initialDebugStreamsEnabled

    @Volatile
    private var closed = false

    @Volatile
    private var activeAddress: String? = null

    @Volatile
    private var gatt: BluetoothGatt? = null

    @Volatile
    private var statusChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var controlChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var dataChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var configChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var logChar: BluetoothGattCharacteristic? = null

    @Volatile
    private var telemChar: BluetoothGattCharacteristic? = null

    private data class PendingCccdWrite(val uuid: UUID, val enable: Boolean)

    @Volatile
    private var cccdFailed = false

    @Volatile
    private var activeCccdWrite: PendingCccdWrite? = null

    @Volatile
    private var bondingRetryCount = 0

    private val bondLock = Any()
    private var pendingBondCccd: PendingCccdWrite? = null
    private var bondWaitReceiver: BroadcastReceiver? = null
    private var bondWaitTimeoutJob: Job? = null
    private var bondRetryJob: Job? = null

    @Volatile
    private var foundCount = 0

    @Volatile
    private var filteredScan = true

    private var scanTimeoutJob: Job? = null
    private var bondReceiverRegistered = false

    private val bondReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
            val device = extractBluetoothDevice(intent)
            if (device != null && activeAddress != null && device.address == activeAddress) {
                val raw = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
                _bondState.value = mapBondState(raw)
            }
        }
    }

    init {
        try {
            ContextCompat.registerReceiver(
                appContext,
                bondReceiver,
                IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED),
                ContextCompat.RECEIVER_EXPORTED,
            )
            bondReceiverRegistered = true
        } catch (_: Throwable) {
            bondReceiverRegistered = false
        }
    }

    override fun setDebugStreamsEnabled(enabled: Boolean) {
        if (debugStreamsEnabled == enabled) return
        debugStreamsEnabled = enabled
        if (_state.value == LinkState.READY) {
            if (logChar != null) {
                enqueueOp { writeCccd(BleUuids.LOG, enable = enabled) }
            }
            if (telemChar != null) {
                enqueueOp { writeCccd(BleUuids.TELEMETRY, enable = enabled) }
            }
        }
    }

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

    override fun scan(onDevice: (ScannedDevice) -> Unit) {
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
                    } catch (_: SecurityException) {
                        // timeout will stop scan
                    }
                    filteredScan = false
                    startScan(current, filtered = false)
                }
            }
            delay(SCAN_TIMEOUT_MS - SCAN_FILTER_FALLBACK_MS)
            if (_scanning.value) stopScan()
        }
    }

    override fun stopScan() {
        scanTimeoutJob?.cancel()
        scanTimeoutJob = null
        val scanner = try {
            adapter()?.bluetoothLeScanner
        } catch (_: SecurityException) {
            null
        }
        if (scanner != null) {
            try {
                scanner.stopScan(scanCallback)
            } catch (_: SecurityException) {
                // ignore
            } catch (_: IllegalStateException) {
                // Bluetooth turned off while scanning
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
        } catch (_: SecurityException) {
            false
        } catch (_: IllegalStateException) {
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
            } catch (_: SecurityException) {
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

    override fun connect(address: String) {
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
        } catch (_: SecurityException) {
            _state.value = LinkState.ERROR("读取蓝牙状态失败：缺少权限")
            return
        }
        if (!enabled) {
            _state.value = LinkState.ERROR("蓝牙未开启")
            return
        }
        val device = try {
            adapter.getRemoteDevice(address)
        } catch (_: IllegalArgumentException) {
            _state.value = LinkState.ERROR("MAC 地址无效：$address")
            return
        }

        forgetGatt(gatt, disconnectFirst = true)
        activeAddress = address
        _bondState.value = try {
            mapBondState(device.bondState)
        } catch (_: SecurityException) {
            BondState.NONE
        }
        cccdFailed = false
        activeCccdWrite = null
        bondingRetryCount = 0
        connectionMtu = 0
        _state.value = LinkState.CONNECTING
        try {
            gatt = device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } catch (_: SecurityException) {
            _state.value = LinkState.ERROR("缺少 BLUETOOTH_CONNECT 权限")
        }
    }

    override fun disconnect() {
        clearBondWait()
        val current = gatt
        if (current == null) {
            _state.value = LinkState.DISCONNECTED
            return
        }
        try {
            current.disconnect()
        } catch (_: SecurityException) {
            _state.value = LinkState.ERROR("缺少 BLUETOOTH_CONNECT 权限")
        }
    }

    private fun forgetGatt(target: BluetoothGatt?, disconnectFirst: Boolean) {
        clearBondWait()
        activeCccdWrite = null
        bondingRetryCount = 0
        if (target != null && gatt === target) {
            gatt = null
            statusChar = null
            controlChar = null
            dataChar = null
            configChar = null
            logChar = null
            telemChar = null
        }
        if (target != null) {
            if (disconnectFirst) {
                try {
                    target.disconnect()
                } catch (_: Throwable) {
                    // already gone
                }
            }
            try {
                target.close()
            } catch (_: Throwable) {
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

    private fun dispatchCharacteristic(uuid: UUID, value: ByteArray) {
        when (uuid) {
            BleUuids.STATUS -> onStatus?.invoke(value)
            BleUuids.LOG -> onLog?.invoke(value)
            BleUuids.CONTROL -> onFrame?.invoke(value)
            BleUuids.TELEMETRY -> onTelemetry?.invoke(value)
            BleUuids.DATA -> onData?.invoke(value)
            else -> Unit
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g !== gatt) {
                try {
                    g.close()
                } catch (_: Throwable) {
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
            try {
                g.setPreferredPhy(
                    BluetoothDevice.PHY_LE_2M_MASK,
                    BluetoothDevice.PHY_LE_2M_MASK,
                    BluetoothDevice.PHY_OPTION_NO_PREFERRED,
                )
            } catch (_: Throwable) {
                // Best-effort 2M PHY request; keep going regardless.
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
            dataChar = service.getCharacteristic(BleUuids.DATA)
            configChar = service.getCharacteristic(BleUuids.CONFIG)
            logChar = service.getCharacteristic(BleUuids.LOG)
            telemChar = service.getCharacteristic(BleUuids.TELEMETRY)
            if (controlChar == null) {
                _state.value = LinkState.ERROR("缺少 Control 特征（无法收发命令）")
                completeOp()
                return
            }
            completeOp()
            if (statusChar != null) {
                enqueueOp { writeCccd(BleUuids.STATUS, enable = true) }
            }
            if (controlChar != null) {
                enqueueOp { writeCccd(BleUuids.CONTROL, enable = true) }
            }
            if (dataChar != null) {
                enqueueOp { writeCccd(BleUuids.DATA, enable = true) }
            }
            if (debugStreamsEnabled && logChar != null) {
                enqueueOp { writeCccd(BleUuids.LOG, enable = true) }
            }
            if (debugStreamsEnabled && telemChar != null) {
                enqueueOp { writeCccd(BleUuids.TELEMETRY, enable = true) }
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
            val written = activeCccdWrite ?: descriptor.characteristic?.uuid?.let {
                PendingCccdWrite(it, enable = true)
            }
            activeCccdWrite = null
            if (status == BluetoothGatt.GATT_SUCCESS) {
                completeOp()
                return
            }
            if (isAuthOrEncryptionError(status) && written != null) {
                if (bondingRetryCount < MAX_BOND_RETRIES) {
                    bondingRetryCount++
                    handleCccdAuthError(g, written)
                    return
                }
                clearBondWait()
                cccdFailed = true
                resetQueue()
                _state.value = LinkState.ERROR(BOND_ERROR_MESSAGE)
                return
            }
            cccdFailed = true
            completeOp()
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (g !== gatt) return
            dispatchCharacteristic(characteristic.uuid, value.copyOf())
        }

        @Suppress("DEPRECATION")
        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
            if (g !== gatt) return
            val value = characteristic.value ?: return
            dispatchCharacteristic(characteristic.uuid, value.copyOf())
        }
    }

    private fun handleCccdAuthError(g: BluetoothGatt, pending: PendingCccdWrite) {
        val device = g.device
        val currentBondState = try {
            device.bondState
        } catch (_: SecurityException) {
            BluetoothDevice.BOND_NONE
        }
        _bondState.value = mapBondState(currentBondState)

        if (currentBondState == BluetoothDevice.BOND_BONDED) {
            scheduleCccdRetry(g, pending)
            return
        }

        clearBondWait()
        val targetAddress = device.address
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (intent.action != BluetoothDevice.ACTION_BOND_STATE_CHANGED) return
                val dev = extractBluetoothDevice(intent) ?: return
                if (dev.address != targetAddress) return
                val newState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
                _bondState.value = mapBondState(newState)
                when (newState) {
                    BluetoothDevice.BOND_BONDED -> {
                        val toRetry = takePendingBondCccdAndClear() ?: return
                        scheduleCccdRetry(g, toRetry)
                    }
                    BluetoothDevice.BOND_NONE -> {
                        onBondWaitFailed(g)
                    }
                }
            }
        }

        val registered = try {
            ContextCompat.registerReceiver(
                appContext,
                receiver,
                IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED),
                ContextCompat.RECEIVER_EXPORTED,
            )
            true
        } catch (_: Throwable) {
            false
        }

        if (!registered) {
            onBondWaitFailed(g, requirePending = false)
            return
        }

        synchronized(bondLock) {
            pendingBondCccd = pending
            bondWaitReceiver = receiver
            bondWaitTimeoutJob = scope.launch {
                delay(BOND_TIMEOUT_MS)
                onBondWaitFailed(g)
            }
        }

        if (currentBondState == BluetoothDevice.BOND_NONE) {
            val started = try {
                device.createBond()
            } catch (_: SecurityException) {
                false
            }
            val postState = try {
                device.bondState
            } catch (_: SecurityException) {
                BluetoothDevice.BOND_NONE
            }
            _bondState.value = mapBondState(postState)
            if (postState == BluetoothDevice.BOND_BONDED) {
                val toRetry = takePendingBondCccdAndClear()
                if (toRetry != null) {
                    scheduleCccdRetry(g, toRetry)
                }
                return
            }
            if (!started && postState == BluetoothDevice.BOND_NONE) {
                onBondWaitFailed(g)
                return
            }
        } else {
            val postState = try {
                device.bondState
            } catch (_: SecurityException) {
                BluetoothDevice.BOND_NONE
            }
            if (postState == BluetoothDevice.BOND_BONDED) {
                val toRetry = takePendingBondCccdAndClear()
                if (toRetry != null) {
                    scheduleCccdRetry(g, toRetry)
                }
            }
        }
    }

    private fun scheduleCccdRetry(expectedGatt: BluetoothGatt, pending: PendingCccdWrite) {
        synchronized(bondLock) {
            bondRetryJob?.cancel()
            bondRetryJob = scope.launch {
                delay(BOND_RETRY_DELAY_MS)
                if (closed || gatt !== expectedGatt) return@launch
                retryCccd(pending)
            }
        }
    }

    private fun retryCccd(pending: PendingCccdWrite) {
        synchronized(queueLock) {
            gattQueue.addFirst { writeCccd(pending.uuid, pending.enable) }
            queueBusy = false
        }
        drainQueue()
    }

    private fun takePendingBondCccdAndClear(): PendingCccdWrite? {
        val pending: PendingCccdWrite?
        val receiver: BroadcastReceiver?
        val timeoutJob: Job?
        synchronized(bondLock) {
            pending = pendingBondCccd
            pendingBondCccd = null
            receiver = bondWaitReceiver
            bondWaitReceiver = null
            timeoutJob = bondWaitTimeoutJob
            bondWaitTimeoutJob = null
        }
        timeoutJob?.cancel()
        if (receiver != null) {
            try {
                appContext.unregisterReceiver(receiver)
            } catch (_: Throwable) {
                // ignore
            }
        }
        return pending
    }

    private fun clearBondWait() {
        val receiver: BroadcastReceiver?
        val timeoutJob: Job?
        val retryJob: Job?
        synchronized(bondLock) {
            pendingBondCccd = null
            receiver = bondWaitReceiver
            bondWaitReceiver = null
            timeoutJob = bondWaitTimeoutJob
            bondWaitTimeoutJob = null
            retryJob = bondRetryJob
            bondRetryJob = null
        }
        timeoutJob?.cancel()
        retryJob?.cancel()
        if (receiver != null) {
            try {
                appContext.unregisterReceiver(receiver)
            } catch (_: Throwable) {
                // ignore
            }
        }
    }

    private fun onBondWaitFailed(expectedGatt: BluetoothGatt, requirePending: Boolean = true) {
        val pending = takePendingBondCccdAndClear()
        if (requirePending && pending == null) return
        if (closed || gatt !== expectedGatt) return
        cccdFailed = true
        resetQueue()
        _state.value = LinkState.ERROR(BOND_ERROR_MESSAGE)
    }

    private fun isAuthOrEncryptionError(status: Int): Boolean =
        status == BluetoothGatt.GATT_INSUFFICIENT_AUTHENTICATION ||
            status == BluetoothGatt.GATT_INSUFFICIENT_ENCRYPTION ||
            status == GATT_AUTH_FAIL

    private fun writeCccd(uuid: UUID, enable: Boolean): Boolean {
        val current = gatt ?: return false
        val service = current.getService(BleUuids.SERVICE) ?: return false
        val characteristic = service.getCharacteristic(uuid) ?: return false
        if (!current.setCharacteristicNotification(characteristic, enable)) return false
        val descriptor = characteristic.getDescriptor(BleUuids.CCCD) ?: return false
        val payload = if (enable) {
            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        } else {
            BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE
        }
        activeCccdWrite = PendingCccdWrite(uuid, enable)
        val accepted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            current.writeDescriptor(descriptor, payload) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            descriptor.value = payload
            @Suppress("DEPRECATION")
            current.writeDescriptor(descriptor)
        }
        if (!accepted) {
            activeCccdWrite = null
        }
        return accepted
    }

    // --- command writes -------------------------------------------------------

    override fun send(bytes: ByteArray) {
        if (closed) return
        scope.launch {
            writeMutex.withLock { writeNow(bytes) }
        }
    }

    private fun writeNow(bytes: ByteArray) {
        val current = gatt ?: return
        val characteristic = controlChar ?: return
        val queued = writeCharCompat(
            current,
            characteristic,
            bytes,
            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE,
        )
        if (queued) return
        writeCharCompat(
            current,
            characteristic,
            bytes,
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
        )
    }

    private fun writeCharCompat(
        g: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        bytes: ByteArray,
        writeType: Int,
    ): Boolean = try {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(characteristic, bytes, writeType) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.writeType = writeType
            @Suppress("DEPRECATION")
            characteristic.value = bytes
            @Suppress("DEPRECATION")
            g.writeCharacteristic(characteristic)
        }
    } catch (_: SecurityException) {
        false
    }

    override fun close() {
        if (closed) return
        closed = true
        stopScan()
        clearBondWait()
        scope.cancel()
        if (bondReceiverRegistered) {
            try {
                appContext.unregisterReceiver(bondReceiver)
            } catch (_: Throwable) {
                // ignore
            }
            bondReceiverRegistered = false
        }
        forgetGatt(gatt, disconnectFirst = true)
        _state.value = LinkState.DISCONNECTED
        _bondState.value = BondState.NONE
    }

    // --- platform helpers -----------------------------------------------------

    private fun extractBluetoothDevice(intent: Intent): BluetoothDevice? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        }

    private fun mapBondState(raw: Int): BondState = when (raw) {
        BluetoothDevice.BOND_BONDED -> BondState.BONDED
        BluetoothDevice.BOND_BONDING -> BondState.BONDING
        else -> BondState.NONE
    }

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
        const val GATT_AUTH_FAIL = 137
        const val MAX_BOND_RETRIES = 2
        const val BOND_TIMEOUT_MS = 25_000L
        const val BOND_RETRY_DELAY_MS = 200L
        const val BOND_ERROR_MESSAGE = "BLE 配对失败或已取消，请长按设备 KEY1 3秒进入配对模式后重试"
    }
}
