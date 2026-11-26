// SPDX-License-Identifier: MIT
package com.lifegps.app.ble

import com.lifegps.app.debug.ScannedDevice
import com.lifegps.core.model.BondState
import kotlinx.coroutines.flow.StateFlow

/**
 * Abstraction over the BLE GATT link so repositories and sync engines can be
 * unit-tested with a deterministic `FakeTransport` without Android Bluetooth APIs.
 */
interface LifeGpsTransport {
    val state: StateFlow<LinkState>
    val bondState: StateFlow<BondState>
    val scanning: StateFlow<Boolean>
    val scanError: StateFlow<String?>
    val connectionMtu: Int

    var onDeviceFound: ((ScannedDevice) -> Unit)?
    var onStatus: ((ByteArray) -> Unit)?
    var onLog: ((ByteArray) -> Unit)?
    var onFrame: ((ByteArray) -> Unit)?
    var onTelemetry: ((ByteArray) -> Unit)?
    var onData: ((ByteArray) -> Unit)?

    fun scan(onDevice: (ScannedDevice) -> Unit)
    fun stopScan()
    fun connect(address: String)
    fun disconnect()
    fun send(bytes: ByteArray)
    fun setDebugStreamsEnabled(enabled: Boolean)
    fun close()
}
