// SPDX-License-Identifier: MIT
package com.lifegps.app.ble

import com.lifegps.app.debug.ScannedDevice
import com.lifegps.core.model.BondState
import com.lifegps.core.protocol.GeneratedProtocol
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Deterministic test double implementing [LifeGpsTransport] so higher-level BLE
 * flows can be verified on pure JVM without Android Bluetooth hardware.
 */
class FakeTransport : LifeGpsTransport {
    private val _state = MutableStateFlow<LinkState>(LinkState.DISCONNECTED)
    override val state: StateFlow<LinkState> = _state.asStateFlow()

    private val _bondState = MutableStateFlow(BondState.NONE)
    override val bondState: StateFlow<BondState> = _bondState.asStateFlow()

    private val _scanning = MutableStateFlow(false)
    override val scanning: StateFlow<Boolean> = _scanning.asStateFlow()

    private val _scanError = MutableStateFlow<String?>(null)
    override val scanError: StateFlow<String?> = _scanError.asStateFlow()

    override var connectionMtu: Int = GeneratedProtocol.Meta.MTU

    override var onDeviceFound: ((ScannedDevice) -> Unit)? = null
    override var onStatus: ((ByteArray) -> Unit)? = null
    override var onLog: ((ByteArray) -> Unit)? = null
    override var onFrame: ((ByteArray) -> Unit)? = null
    override var onTelemetry: ((ByteArray) -> Unit)? = null
    override var onData: ((ByteArray) -> Unit)? = null

    val sentFrames = mutableListOf<ByteArray>()
    var debugStreamsEnabled: Boolean = true
        private set
    var closed: Boolean = false
        private set

    override fun scan(onDevice: (ScannedDevice) -> Unit) {
        onDeviceFound = onDevice
        _scanning.value = true
    }

    override fun stopScan() {
        _scanning.value = false
    }

    override fun connect(address: String) {
        _state.value = LinkState.READY
    }

    override fun disconnect() {
        _state.value = LinkState.DISCONNECTED
    }

    override fun send(bytes: ByteArray) {
        sentFrames.add(bytes.copyOf())
    }

    override fun setDebugStreamsEnabled(enabled: Boolean) {
        debugStreamsEnabled = enabled
    }

    override fun close() {
        closed = true
        _state.value = LinkState.DISCONNECTED
    }
}

class FakeTransportTest {

    @Test
    fun fakeTransportTracksDebugStreamToggleAndFrames() {
        val fake = FakeTransport()
        assertTrue(fake.debugStreamsEnabled)
        fake.setDebugStreamsEnabled(false)
        assertFalse(fake.debugStreamsEnabled)

        val setTimePayload = byteArrayOf(0x10, 0x20, 0x30, 0x40)
        val encoded = BleFrame.encodeCommand(Opcode.SET_TIME.code, seq = 7, payload = setTimePayload)
        fake.send(encoded)
        assertEquals(1, fake.sentFrames.size)

        val decoded = BleFrame.parseFrame(fake.sentFrames.first())
        assertEquals(Opcode.SET_TIME.code, decoded?.opcode)
        assertEquals(7, decoded?.seq)
        assertEquals(4, decoded?.payload?.size)
    }
}
