// SPDX-License-Identifier: MIT
package com.lifegps.core.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class CoordTransformTest {

    @Test
    fun outsideChinaIsIdentity() {
        val tokyo = CoordTransform.wgs84ToGcj02(35.6895, 139.6917)
        assertEquals(35.6895, tokyo.latitude, 1e-9)
        assertEquals(139.6917, tokyo.longitude, 1e-9)
    }

    @Test
    fun shanghaiRoundTripWithinSubMeter() {
        val lat = 31.230416
        val lon = 121.473701
        val gcj = CoordTransform.wgs84ToGcj02(lat, lon)
        assertTrue(abs(gcj.latitude - lat) > 1e-4)
        assertTrue(abs(gcj.longitude - lon) > 1e-4)
        val back = CoordTransform.gcj02ToWgs84(gcj.latitude, gcj.longitude)
        assertEquals(lat, back.latitude, 1e-7)
        assertEquals(lon, back.longitude, 1e-7)
    }
}
