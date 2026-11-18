package com.lifegps.app.ble

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat

/** Result of the runtime-permission gate used by the BLE debug UI. */
class BlePermissionState(
    val granted: Boolean,
    val unsupported: Boolean,
    val launch: () -> Unit,
)

/**
 * Requests the permissions the BLE stack actually needs and reports whether a
 * scan/connect may be attempted.
 *
 * Android 12+ (API 31) uses the "nearby devices" permissions; older releases
 * still require location access for BLE scanning. The feature check is separate
 * because a missing BLE radio cannot be fixed by granting anything.
 */
@Composable
fun rememberBlePermissions(): BlePermissionState {
    val context = LocalContext.current
    val leSupported = remember(context) {
        context.packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)
    }
    val required = remember(context) { requiredBlePermissions() }
    var granted by remember(required) { mutableStateOf(hasAllPermissions(context, required)) }

    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions(),
    ) { result ->
        granted = required.all { permission ->
            result[permission] == true ||
                ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
        }
    }

    return BlePermissionState(
        granted = leSupported && granted,
        unsupported = !leSupported,
        launch = { launcher.launch(required) },
    )
}

private fun requiredBlePermissions(): Array<String> =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
        )
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

private fun hasAllPermissions(context: Context, permissions: Array<String>): Boolean =
    permissions.all { permission ->
        ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
    }
