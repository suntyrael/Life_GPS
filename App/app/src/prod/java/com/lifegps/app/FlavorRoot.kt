// SPDX-License-Identifier: MIT
package com.lifegps.app

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import com.lifegps.app.prod.ProdAppHost

/**
 * Production flavor root: physically isolated from all hardware debug screens.
 * Debug Telemetry / Log GATT subscriptions remain disabled in prod builds.
 */
@Composable
fun FlavorRoot(container: AppContainer) {
    LaunchedEffect(container) {
        container.repo.setDebugStreamsEnabled(false)
    }
    ProdAppHost(container = container, topBarExtraAction = null)
}
