// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import com.lifegps.core.protocol.GeneratedProtocol
import java.io.ByteArrayOutputStream

/**
 * Full 11-key device configuration model matching `Firmware/include/lifegps/app_config.h`
 * and `protocol/lifegps_protocol.yaml` (`config_tlv`).
 */
data class DeviceConfig(
    val recordMode: Int = 1,          // 0=DEEP_SLEEP, 1=ACTIVE, 2=HIGH_PRECISION
    val gnssRateHz: Int = 1,          // 1..10
    val gnssColdTimeoutS: Int = 60,   // 30..300
    val archiveHourUtc: Int = 18,     // 0..23
    val ledEnable: Boolean = true,    // 0/1
    val autoOffMin: Int = 0,          // 0..1440 (0=disabled)
    val pdrStepKMm: Int = 450,        // 300..550
    val magAnomalyUt: Int = 48,       // 20..120
    val tzOffsetMin: Int = 480,       // -720..+840
    val bleAdvIntervalMs: Int = 200,  // 100..4000
    val logLevelDefault: Int = 2,     // 0..4
)

object ConfigTlvCodec {

    fun encodeAll(cfg: DeviceConfig): ByteArray {
        val out = ByteArrayOutputStream(64)
        writeU8(out, GeneratedProtocol.ConfigTlv.RECORD_MODE, cfg.recordMode)
        writeU8(out, GeneratedProtocol.ConfigTlv.GNSS_RATE_HZ, cfg.gnssRateHz)
        writeU16(out, GeneratedProtocol.ConfigTlv.GNSS_COLD_TIMEOUT_S, cfg.gnssColdTimeoutS)
        writeU8(out, GeneratedProtocol.ConfigTlv.ARCHIVE_HOUR_UTC, cfg.archiveHourUtc)
        writeU8(out, GeneratedProtocol.ConfigTlv.LED_ENABLE, if (cfg.ledEnable) 1 else 0)
        writeU16(out, GeneratedProtocol.ConfigTlv.AUTO_OFF_MIN, cfg.autoOffMin)
        writeU16(out, GeneratedProtocol.ConfigTlv.PDR_STEP_K_MM, cfg.pdrStepKMm)
        writeU16(out, GeneratedProtocol.ConfigTlv.MAG_ANOMALY_UT, cfg.magAnomalyUt)
        writeI16(out, GeneratedProtocol.ConfigTlv.TZ_OFFSET_MIN, cfg.tzOffsetMin)
        writeU16(out, GeneratedProtocol.ConfigTlv.BLE_ADV_INTERVAL_MS, cfg.bleAdvIntervalMs)
        writeU8(out, GeneratedProtocol.ConfigTlv.LOG_LEVEL_DEFAULT, cfg.logLevelDefault)
        return out.toByteArray()
    }

    fun encodeDiff(base: DeviceConfig, target: DeviceConfig): ByteArray {
        val out = ByteArrayOutputStream(64)
        if (target.recordMode != base.recordMode) {
            writeU8(out, GeneratedProtocol.ConfigTlv.RECORD_MODE, target.recordMode)
        }
        if (target.gnssRateHz != base.gnssRateHz) {
            writeU8(out, GeneratedProtocol.ConfigTlv.GNSS_RATE_HZ, target.gnssRateHz)
        }
        if (target.gnssColdTimeoutS != base.gnssColdTimeoutS) {
            writeU16(out, GeneratedProtocol.ConfigTlv.GNSS_COLD_TIMEOUT_S, target.gnssColdTimeoutS)
        }
        if (target.archiveHourUtc != base.archiveHourUtc) {
            writeU8(out, GeneratedProtocol.ConfigTlv.ARCHIVE_HOUR_UTC, target.archiveHourUtc)
        }
        if (target.ledEnable != base.ledEnable) {
            writeU8(out, GeneratedProtocol.ConfigTlv.LED_ENABLE, if (target.ledEnable) 1 else 0)
        }
        if (target.autoOffMin != base.autoOffMin) {
            writeU16(out, GeneratedProtocol.ConfigTlv.AUTO_OFF_MIN, target.autoOffMin)
        }
        if (target.pdrStepKMm != base.pdrStepKMm) {
            writeU16(out, GeneratedProtocol.ConfigTlv.PDR_STEP_K_MM, target.pdrStepKMm)
        }
        if (target.magAnomalyUt != base.magAnomalyUt) {
            writeU16(out, GeneratedProtocol.ConfigTlv.MAG_ANOMALY_UT, target.magAnomalyUt)
        }
        if (target.tzOffsetMin != base.tzOffsetMin) {
            writeI16(out, GeneratedProtocol.ConfigTlv.TZ_OFFSET_MIN, target.tzOffsetMin)
        }
        if (target.bleAdvIntervalMs != base.bleAdvIntervalMs) {
            writeU16(out, GeneratedProtocol.ConfigTlv.BLE_ADV_INTERVAL_MS, target.bleAdvIntervalMs)
        }
        if (target.logLevelDefault != base.logLevelDefault) {
            writeU8(out, GeneratedProtocol.ConfigTlv.LOG_LEVEL_DEFAULT, target.logLevelDefault)
        }
        return out.toByteArray()
    }

    fun decode(bytes: ByteArray, base: DeviceConfig = DeviceConfig()): DeviceConfig {
        var cfg = base
        var off = 0
        while (off + 2 <= bytes.size) {
            val tag = bytes[off].toInt() and 0xFF
            val len = bytes[off + 1].toInt() and 0xFF
            off += 2
            if (off + len > bytes.size) break

            cfg = when (tag) {
                GeneratedProtocol.ConfigTlv.RECORD_MODE ->
                    if (len == 1) cfg.copy(recordMode = bytes[off].toInt() and 0xFF) else cfg
                GeneratedProtocol.ConfigTlv.GNSS_RATE_HZ ->
                    if (len == 1) cfg.copy(gnssRateHz = bytes[off].toInt() and 0xFF) else cfg
                GeneratedProtocol.ConfigTlv.GNSS_COLD_TIMEOUT_S ->
                    if (len == 2) cfg.copy(gnssColdTimeoutS = readU16(bytes, off)) else cfg
                GeneratedProtocol.ConfigTlv.ARCHIVE_HOUR_UTC ->
                    if (len == 1) cfg.copy(archiveHourUtc = bytes[off].toInt() and 0xFF) else cfg
                GeneratedProtocol.ConfigTlv.LED_ENABLE ->
                    if (len == 1) cfg.copy(ledEnable = (bytes[off].toInt() and 0xFF) != 0) else cfg
                GeneratedProtocol.ConfigTlv.AUTO_OFF_MIN ->
                    if (len == 2) cfg.copy(autoOffMin = readU16(bytes, off)) else cfg
                GeneratedProtocol.ConfigTlv.PDR_STEP_K_MM ->
                    if (len == 2) cfg.copy(pdrStepKMm = readU16(bytes, off)) else cfg
                GeneratedProtocol.ConfigTlv.MAG_ANOMALY_UT ->
                    if (len == 2) cfg.copy(magAnomalyUt = readU16(bytes, off)) else cfg
                GeneratedProtocol.ConfigTlv.TZ_OFFSET_MIN ->
                    if (len == 2) cfg.copy(tzOffsetMin = readU16(bytes, off).toShort().toInt()) else cfg
                GeneratedProtocol.ConfigTlv.BLE_ADV_INTERVAL_MS ->
                    if (len == 2) cfg.copy(bleAdvIntervalMs = readU16(bytes, off)) else cfg
                GeneratedProtocol.ConfigTlv.LOG_LEVEL_DEFAULT ->
                    if (len == 1) cfg.copy(logLevelDefault = bytes[off].toInt() and 0xFF) else cfg
                else -> cfg
            }
            off += len
        }
        return cfg
    }

    private fun writeU8(out: ByteArrayOutputStream, tag: Int, value: Int) {
        out.write(tag and 0xFF)
        out.write(1)
        out.write(value and 0xFF)
    }

    private fun writeU16(out: ByteArrayOutputStream, tag: Int, value: Int) {
        out.write(tag and 0xFF)
        out.write(2)
        out.write(value and 0xFF)
        out.write((value ushr 8) and 0xFF)
    }

    private fun writeI16(out: ByteArrayOutputStream, tag: Int, value: Int) = writeU16(out, tag, value)

    private fun readU16(bytes: ByteArray, off: Int): Int =
        (bytes[off].toInt() and 0xFF) or ((bytes[off + 1].toInt() and 0xFF) shl 8)
}
