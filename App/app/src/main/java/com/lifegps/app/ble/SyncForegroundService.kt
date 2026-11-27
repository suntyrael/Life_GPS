// SPDX-License-Identifier: MIT
package com.lifegps.app.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat

/**
 * Connected-device Foreground Service keeping BLE bulk trajectory synchronization
 * alive when the user switches away from the app during multi-day LGD1 transfers.
 */
class SyncForegroundService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_UPDATE -> {
                ensureChannel()
                val text = intent.getStringExtra(EXTRA_TEXT) ?: "正在同步轨迹…"
                val progress = intent.getIntExtra(EXTRA_PROGRESS, -1)
                val nm = getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager
                nm?.notify(NOTIFICATION_ID, buildNotification(text, progress))
            }
            else -> {
                ensureChannel()
                val text = intent?.getStringExtra(EXTRA_TEXT) ?: "正在同步 Life_GPS 轨迹…"
                val notification = buildNotification(text, -1)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    startForeground(
                        NOTIFICATION_ID,
                        notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
                    )
                } else {
                    startForeground(NOTIFICATION_ID, notification)
                }
            }
        }
        return START_NOT_STICKY
    }

    private fun ensureChannel() {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "轨迹同步",
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = "保持 Life_GPS 蓝牙轨迹同步在后台稳定运行"
                setShowBadge(false)
            }
            nm.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(text: String, progressPct: Int): Notification {
        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Life_GPS 轨迹同步")
            .setContentText(text)
            .setOngoing(true)
            .setSilent(true)
        if (progressPct in 0..100) {
            builder.setProgress(100, progressPct, false)
        } else {
            builder.setProgress(0, 0, true)
        }
        return builder.build()
    }

    companion object {
        private const val CHANNEL_ID = "lifegps_sync"
        private const val NOTIFICATION_ID = 4101
        private const val ACTION_START = "com.lifegps.app.sync.START"
        private const val ACTION_UPDATE = "com.lifegps.app.sync.UPDATE"
        private const val ACTION_STOP = "com.lifegps.app.sync.STOP"
        private const val EXTRA_TEXT = "text"
        private const val EXTRA_PROGRESS = "progress"

        fun start(context: Context, text: String = "正在同步 Life_GPS 轨迹…") {
            runCatching {
                val intent = Intent(context, SyncForegroundService::class.java).apply {
                    action = ACTION_START
                    putExtra(EXTRA_TEXT, text)
                }
                ContextCompat.startForegroundService(context, intent)
            }
        }

        fun update(context: Context, text: String, progressPct: Int = -1) {
            runCatching {
                val intent = Intent(context, SyncForegroundService::class.java).apply {
                    action = ACTION_UPDATE
                    putExtra(EXTRA_TEXT, text)
                    putExtra(EXTRA_PROGRESS, progressPct)
                }
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            runCatching {
                val intent = Intent(context, SyncForegroundService::class.java).apply {
                    action = ACTION_STOP
                }
                context.startService(intent)
            }
        }
    }
}
