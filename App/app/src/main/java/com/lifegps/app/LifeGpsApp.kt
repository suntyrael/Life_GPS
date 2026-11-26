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
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.lifegps.app.ble.BlePermissionState
import com.lifegps.app.ble.rememberBlePermissions
import com.lifegps.app.ui.theme.LifeGpsTheme
import com.lifegps.app.ui.theme.TextDim
import com.lifegps.app.ui.theme.TextMain

@Composable
fun LifeGpsApp() {
    val application = LocalContext.current.applicationContext as LifeGpsApplication
    val container = application.container
    val permissions = rememberBlePermissions()

    LaunchedEffect(permissions.granted) {
        container.repo.setPermissionsGranted(permissions.granted)
    }

    LifeGpsTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            when {
                permissions.unsupported -> BleUnsupportedNotice()
                !permissions.granted -> BlePermissionGate(permissions)
                else -> FlavorRoot(container)
            }
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
            "Life_GPS 通过 BLE 与定位设备通信，必须先授予蓝牙权限才能扫描、配对和同步数据。",
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
            "系统未声明 FEATURE_BLUETOOTH_LE。Life_GPS 全部数据来自 BLE GATT，没有 BLE 硬件时无法连接设备。",
            color = TextDim,
            fontSize = 13.sp,
        )
    }
}
