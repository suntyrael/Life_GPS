package com.lifegps.app

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.lifegps.app.debug.CommandConsoleScreen
import com.lifegps.app.debug.DebugHomeScreen
import com.lifegps.app.debug.DeviceScanScreen
import com.lifegps.app.debug.FakeDebugRepository
import com.lifegps.app.debug.FlashStatusScreen
import com.lifegps.app.debug.GnssStatusScreen
import com.lifegps.app.debug.HardwareCheckScreen
import com.lifegps.app.debug.MapLiveScreen
import com.lifegps.app.debug.SensorStatusScreen
import com.lifegps.app.debug.SpatialTrailScreen
import com.lifegps.app.debug.SerialLogScreen
import com.lifegps.app.navigation.Routes
import com.lifegps.app.ui.theme.LifeGpsTheme

@Composable
fun LifeGpsApp() {
    val repo = remember { FakeDebugRepository() }
    val navController = rememberNavController()

    LifeGpsTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
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
                    )
                }
                composable(Routes.SPATIAL) {
                    SpatialTrailScreen(repo) { navController.popBackStack() }
                }
                composable(Routes.MAP) {
                    MapLiveScreen(repo) { navController.popBackStack() }
                }
                composable(Routes.GNSS) {
                    GnssStatusScreen(repo) { navController.popBackStack() }
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
    }
}
