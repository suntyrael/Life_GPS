package com.lifegps.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.lifegps.app.ble.BleDebugRepository
import com.lifegps.app.ble.BlePermissionState
import com.lifegps.app.ble.rememberBlePermissions
import com.lifegps.app.debug.CalibrationScreen
import com.lifegps.app.debug.CommandConsoleScreen
import com.lifegps.app.debug.DebugHomeScreen
import com.lifegps.app.debug.DeviceScanScreen
import com.lifegps.app.debug.FlashStatusScreen
import com.lifegps.app.debug.GnssStatusScreen
import com.lifegps.app.debug.HardwareCheckScreen
import com.lifegps.app.debug.MapLiveScreen
import com.lifegps.app.debug.SensorStatusScreen
import com.lifegps.app.debug.SerialLogScreen
import com.lifegps.app.debug.SpatialTrailScreen
import com.lifegps.app.navigation.Routes
import com.lifegps.app.ui.theme.LifeGpsTheme
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@Composable
fun LifeGpsApp() {
    val appContext = LocalContext.current.applicationContext
    val repo = remember { BleDebugRepository(appContext) }
    val permissions = rememberBlePermissions()

    LaunchedEffect(permissions.granted) {
        repo.setPermissionsGranted(permissions.granted)
    }
    DisposableEffect(repo) {
        onDispose { repo.close() }
    }

    LifeGpsTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            when {
                permissions.unsupported -> BleUnsupportedNotice()
                !permissions.granted -> BlePermissionGate(permissions)
                else -> AppNavHost(repo)
            }
        }
    }
}

@Composable
private fun AppNavHost(repo: BleDebugRepository) {
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

@Composable
private fun BlePermissionGate(state: BlePermissionState) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            "需要蓝牙权限",
            color = TextMain,
            fontSize = 20.sp,
            fontWeight = FontWeight.Bold,
        )
        Spacer(Modifier.height(10.dp))
        Text(
            "调试界面直接读取 LGPS 设备的 Status / Control / Log 特征，必须先授予蓝牙权限才能扫描和连接。",
            color = TextDim,
            fontSize = 13.sp,
        )
        Spacer(Modifier.height(20.dp))
        Button(
            onClick = state.launch,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("允许蓝牙权限") }
        Spacer(Modifier.height(10.dp))
        Text(
            "Android 12 及以上：附近设备（BLUETOOTH_SCAN / BLUETOOTH_CONNECT）；Android 11 及以下：位置权限用于扫描。",
            color = TextDim,
            fontSize = 11.sp,
        )
    }
}

@Composable
private fun BleUnsupportedNotice() {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            "此设备不支持 BLE",
            color = TextMain,
            fontSize = 20.sp,
            fontWeight = FontWeight.Bold,
        )
        Spacer(Modifier.height(10.dp))
        Text(
            "系统未声明 FEATURE_BLUETOOTH_LE。Life_GPS 的数据全部来自 BLE GATT，没有 BLE 硬件时调试界面不可用。",
            color = TextDim,
            fontSize = 13.sp,
        )
    }
}
