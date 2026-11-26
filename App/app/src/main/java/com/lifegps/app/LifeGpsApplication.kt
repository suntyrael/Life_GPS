// SPDX-License-Identifier: MIT
package com.lifegps.app

import android.app.Application
import com.lifegps.app.ble.BleDebugRepository
import com.lifegps.app.ble.BleTransport
import com.lifegps.core.model.CoordSystem
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Application-scoped dependency container.
 *
 * Owns the single [BleTransport] and [BleDebugRepository] instance so BLE links,
 * active calibration sessions, and sync state survive Activity configuration
 * changes (screen rotation, dark-mode toggle, multi-window resize).
 */
class AppContainer(application: Application) {
    val transport = BleTransport(
        context = application,
        initialDebugStreamsEnabled = true,
    )
    val repo = BleDebugRepository(application, transport)

    private val _displayCoordSystem = MutableStateFlow(CoordSystem.WGS84)
    val displayCoordSystem: StateFlow<CoordSystem> = _displayCoordSystem.asStateFlow()

    fun setDisplayCoordSystem(system: CoordSystem) {
        _displayCoordSystem.value = system
    }

    fun close() {
        repo.close()
    }
}

class LifeGpsApplication : Application() {
    lateinit var container: AppContainer
        private set

    override fun onCreate() {
        super.onCreate()
        container = AppContainer(this)
    }

    override fun onTerminate() {
        super.onTerminate()
        container.close()
    }
}
