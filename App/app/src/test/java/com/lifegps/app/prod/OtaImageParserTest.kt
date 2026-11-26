// SPDX-License-Identifier: MIT
package com.lifegps.app.prod

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class OtaImageParserTest {

    @Test
    fun parsesValidMcubootSignedHeader() {
        val raw = ByteArray(16 * 1024)
        // Little-endian MCUboot magic 0x96F3B83D
        raw[0] = 0x3D
        raw[1] = 0xB8.toByte()
        raw[2] = 0xF3.toByte()
        raw[3] = 0x96.toByte()
        // Version 0.5.1+42 at offsets 20..27
        raw[20] = 0
        raw[21] = 5
        raw[22] = 1
        raw[23] = 0
        raw[24] = 42

        val parsed = parseMcubootSignedImage("zephyr.signed.bin", raw).getOrThrow()
        assertEquals("zephyr.signed.bin", parsed.fileName)
        assertEquals("0.5.1+42", parsed.versionLabel)
        assertEquals(16, parsed.sha256Short.length)
    }

    @Test
    fun rejectsBadMagicOrInvalidSize() {
        val badMagic = ByteArray(8192)
        assertTrue(parseMcubootSignedImage("bad.bin", badMagic).isFailure)

        val tooSmall = ByteArray(512)
        tooSmall[0] = 0x3D
        tooSmall[1] = 0xB8.toByte()
        tooSmall[2] = 0xF3.toByte()
        tooSmall[3] = 0x96.toByte()
        assertTrue(parseMcubootSignedImage("tiny.bin", tooSmall).isFailure)
    }
}
