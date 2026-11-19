package com.lifegps.app.ble

/**
 * Wire codec for the GNSS debug (0x36) and GNSS configuration (0x35) commands.
 *
 * The key table below is the App side of the whitelist that lives in
 * `Firmware/src/gnss/gnss.c`; both sides must agree, and the firmware re-validates
 * every range before a key reaches the receiver. The SSOT for the field layout is
 * `Doc/Software/BLE_Wire_v1.md` §6.6 / §6.7.
 *
 * Nothing here invents a value: a read returns what the receiver reported, and a
 * write is always followed by that readback.
 */
data class GnssCfgKey(
    val key: Long,
    val len: Int,
    val label: String,
    val min: Int,
    val max: Int,
    val unit: String = "",
    /** Named values, when the key is an enum; the first entry is the default. */
    val choices: List<Pair<Int, String>> = emptyList(),
) {
    val isBoolean: Boolean get() = min == 0 && max == 1 && unit.isEmpty()
}

data class GnssCfgValue(
    val key: Long,
    val len: Int,
    val value: Long,
    /** Non-null when the last exchange failed; the UI shows it verbatim. */
    val error: String? = null,
)

data class GnssDebugState(
    val flags: Int = 0,
    val remainingSeconds: Int = 0,
    val error: String? = null,
) {
    val holdPower: Boolean get() = flags and GnssCodec.FLAG_HOLD_POWER != 0
    val navSat: Boolean get() = flags and GnssCodec.FLAG_NAV_SAT != 0
    val nmea: Boolean get() = flags and GnssCodec.FLAG_NMEA != 0
}

object GnssCodec {
    // --- debug session (0x36) -------------------------------------------------

    const val FLAG_HOLD_POWER = 0x01
    const val FLAG_NAV_SAT = 0x02
    const val FLAG_NMEA = 0x04
    const val FLAGS_ALL = FLAG_HOLD_POWER or FLAG_NAV_SAT or FLAG_NMEA

    /** `u8 flags, u16 ttl_s`; an empty body asks for the current session. */
    fun encodeDebug(flags: Int, ttlSeconds: Int): ByteArray =
        byteArrayOf(
            (flags and 0xFF).toByte(),
            (ttlSeconds and 0xFF).toByte(),
            ((ttlSeconds ushr 8) and 0xFF).toByte(),
        )

    fun encodeDebugQuery(): ByteArray = ByteArray(0)

    /** Response body after the status byte: `u8 flags, u16 remaining_s`. */
    fun decodeDebug(body: ByteArray): GnssDebugState? {
        if (body.size < 3) return null
        val flags = BleFrame.u8(body, 0) and 0xFF
        val remaining = BleFrame.u16(body, 1)
        return GnssDebugState(flags = flags, remainingSeconds = remaining)
    }

    // --- configuration (0x35) -------------------------------------------------

    const val ACTION_GET = 0
    const val ACTION_SET = 1
    const val ACTION_SET_PERSIST = 2

    /** `u8 action, u32 key` for a read. */
    fun encodeCfgGet(key: Long): ByteArray {
        val out = ByteArray(5)
        out[0] = ACTION_GET.toByte()
        writeU32(out, 1, key)
        return out
    }

    /** `u8 action, u32 key, value[len]` for a write. */
    fun encodeCfgSet(key: Long, value: Long, len: Int, persist: Boolean): ByteArray {
        require(len in 1..4)
        val out = ByteArray(5 + len)
        out[0] = (if (persist) ACTION_SET_PERSIST else ACTION_SET).toByte()
        writeU32(out, 1, key)
        for (index in 0 until len) {
            out[5 + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
        return out
    }

    /** Response body after the status byte: `u32 key, u8 len, value[len]`. */
    fun decodeCfg(body: ByteArray): GnssCfgValue? {
        if (body.size < 6) return null
        val key = readU32(body, 0)
        val len = BleFrame.u8(body, 4)
        if (len <= 0 || len > 4 || body.size < 5 + len) return null
        var value = 0L
        for (index in 0 until len) {
            value = value or ((BleFrame.u8(body, 5 + index).toLong() and 0xFF) shl (8 * index))
        }
        return GnssCfgValue(key = key, len = len, value = value)
    }

    private fun writeU32(dst: ByteArray, offset: Int, value: Long) {
        for (index in 0 until 4) {
            dst[offset + index] = ((value ushr (8 * index)) and 0xFF).toByte()
        }
    }

    private fun readU32(src: ByteArray, offset: Int): Long {
        var value = 0L
        for (index in 0 until 4) {
            value = value or ((BleFrame.u8(src, offset + index).toLong() and 0xFF) shl (8 * index))
        }
        return value
    }
}

/**
 * The whitelisted receiver settings, in the order the App shows them.
 *
 * Kept in one place so the settings screen, the unit tests and the firmware table
 * can be diffed against each other; the firmware rejects anything outside these
 * ranges and answers `-EINVAL`/`-ERANGE` instead of writing it.
 */
object GnssCfgKeys {
    val ANA_USE = GnssCfgKey(
        key = 0x10230001L, len = 1, label = "预测星历 (AssistNow Autonomous)",
        min = 0, max = 1,
    )
    val ANA_ORBMAXERR = GnssCfgKey(
        key = 0x30230002L, len = 2, label = "预测星历最大误差", min = 1, max = 65535, unit = "m",
    )
    val DYNMODEL = GnssCfgKey(
        key = 0x20110021L, len = 1, label = "动态模型", min = 0, max = 10,
        choices = listOf(
            0 to "便携", 2 to "静止", 3 to "步行", 4 to "车载", 5 to "船载",
            6 to "飞行 <1g", 7 to "飞行 <2g", 8 to "飞行 <4g", 9 to "腕表",
        ),
    )
    val SIGNAL_GPS = GnssCfgKey(0x1031001FL, 1, "GPS 星座", 0, 1)
    val SIGNAL_GAL = GnssCfgKey(0x10310021L, 1, "Galileo 星座", 0, 1)
    val SIGNAL_BDS = GnssCfgKey(0x10310022L, 1, "北斗星座", 0, 1)
    val SIGNAL_GLO = GnssCfgKey(0x10310025L, 1, "GLONASS 星座", 0, 1)
    val MINELEV = GnssCfgKey(
        key = 0x201100A4L, len = 1, label = "最小仰角", min = 0, max = 90, unit = "°",
    )
    val PDOP = GnssCfgKey(
        key = 0x301100B1L, len = 2, label = "pDOP 门限", min = 1, max = 9999, unit = "×0.1",
    )
    val RATE_MEAS = GnssCfgKey(
        key = 0x30210001L, len = 2, label = "导航频率", min = 25, max = 1000, unit = "ms",
    )

    val SIGNALS = listOf(SIGNAL_GPS, SIGNAL_GAL, SIGNAL_BDS, SIGNAL_GLO)

    val ALL = listOf(
        DYNMODEL, MINELEV, PDOP, RATE_MEAS,
        SIGNAL_GPS, SIGNAL_GAL, SIGNAL_BDS, SIGNAL_GLO,
        ANA_USE, ANA_ORBMAXERR,
    )

    fun byKey(key: Long): GnssCfgKey? = ALL.firstOrNull { it.key == key }
}
