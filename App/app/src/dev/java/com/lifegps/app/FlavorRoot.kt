// SPDX-License-Identifier: MIT
package com.lifegps.app

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.SmallFloatingActionButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.lifegps.app.ble.BleDebugRepository
import com.lifegps.app.debug.CalibrationScreen
import com.lifegps.app.debug.CommandConsoleScreen
import com.lifegps.app.debug.DebugHomeScreen
import com.lifegps.app.debug.DeviceScanScreen
import com.lifegps.app.debug.FlashStatusScreen
import com.lifegps.app.debug.GnssConfigScreen
import com.lifegps.app.debug.GnssNmeaScreen
import com.lifegps.app.debug.GnssStatusScreen
import com.lifegps.app.debug.HardwareCheckScreen
import com.lifegps.app.debug.MapLiveScreen
import com.lifegps.app.debug.SensorStatusScreen
import com.lifegps.app.debug.SerialLogScreen
import com.lifegps.app.debug.SpatialTrailScreen
import com.lifegps.app.navigation.Routes
import com.lifegps.app.prod.ProdAppHost
import com.lifegps.app.ui.theme.Cyan

/**
 * Dev flavor root: hosts BOTH the 13 hardware debug screens and the Product UI,
 * sharing a single Application-scoped BLE connection.
 */
@Composable
fun FlavorRoot(container: AppContainer) {
    var debugMode by rememberSaveable { mutableStateOf(true) }

    LaunchedEffect(container, debugMode) {
        container.repo.setDebugStreamsEnabled(debugMode)
    }

    if (debugMode) {
        Box(modifier = Modifier.fillMaxSize()) {
            DevDebugNavHost(repo = container.repo)
            SmallFloatingActionButton(
                onClick = { debugMode = false },
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(16.dp),
            ) {
                Text("产品", fontSize = 11.sp, modifier = Modifier.padding(horizontal = 8.dp))
            }
        }
    } else {
        ProdAppHost(
            container = container,
            topBarExtraAction = {
                TextButton(onClick = { debugMode = true }) {
                    Text("硬件调试", color = Cyan, fontSize = 12.sp)
                }
            },
        )
    }
}

@Composable
private fun DevDebugNavHost(repo: BleDebugRepository) {
    val navController = rememberNavController()
    NavHost(
        navController = navController,
        startDestination = Routes.HOME,
    ) {
        composable(Routes.HOME) {
            DebugHomeScreen(repo) { route -> navController.navigate(route) }
        }
        composable(Routes.DEVICE) {
            DeviceScanScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.SERIAL) {
            SerialLogScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.SENSORS) {
            SensorStatusScreen(
                repo = repo,
                onBack = { navController.popBackStack() },
                onOpenTrail = { navController.navigate(Routes.SPATIAL) },
                onOpenCalibration = { navController.navigate(Routes.CALIB) },
            )
        }
        composable(Routes.CALIB) {
            CalibrationScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.SPATIAL) {
            SpatialTrailScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.MAP) {
            MapLiveScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.GNSS) {
            GnssStatusScreen(
                repo = repo,
                onBack = { navController.popBackStack() },
                onOpenNmea = { navController.navigate(Routes.GNSS_NMEA) },
                onOpenConfig = { navController.navigate(Routes.GNSS_CFG) },
            )
        }
        composable(Routes.GNSS_NMEA) {
            GnssNmeaScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.GNSS_CFG) {
            GnssConfigScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.CHECK) {
            HardwareCheckScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.FLASH) {
            FlashStatusScreen(repo) { navController.popBackStack() }
        }
        composable(Routes.CONSOLE) {
            CommandConsoleScreen(repo) { navController.popBackStack() }
        }
    }
}
