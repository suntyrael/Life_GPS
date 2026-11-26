// SPDX-License-Identifier: MIT
package com.lifegps.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ConfigTlvCodecTest {

    @Test
    fun roundTripAll11Tags() {
        val cfg = DeviceConfig(
            recordMode = 2,
            gnssRateHz = 5,
            gnssColdTimeoutS = 90,
            archiveHourUtc = 19,
            ledEnable = false,
            autoOffMin = 120,
            pdrStepKMm = 425,
            magAnomalyUt = 55,
            tzOffsetMin = -300,
            bleAdvIntervalMs = 500,
            logLevelDefault = 3,
        )
        val encoded = ConfigTlvCodec.encodeAll(cfg)
        assertTrue(encoded.size in 30..45)
        val decoded = ConfigTlvCodec.decode(encoded)
        assertEquals(cfg, decoded)
    }

    @Test
    fun encodeDiffOnlyEmitsChangedTags() {
        val base = DeviceConfig()
        val updated = base.copy(ledEnable = false, tzOffsetMin = 540)
        val diff = ConfigTlvCodec.encodeDiff(base, updated)
        // LED_ENABLE (3 B) + TZ_OFFSET_MIN (4 B) = 7 B
        assertEquals(7, diff.size)
        assertEquals(updated, ConfigTlvCodec.decode(diff, base))
    }
}
