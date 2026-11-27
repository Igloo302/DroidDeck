package com.droiddeck.launcher.input

import kotlin.math.sqrt
import org.junit.Assert.*
import org.junit.Test

class JoyConControlsTest {
    @Test fun radialStickDeadzonePreservesDirectionAndLimitsDiagonalTravel() {
        assertStick(0f, 0f, JoyConControls.stick(.08f, .08f))
        assertStick(.5f, 0f, JoyConControls.stick(.56f, 0f))
        val diagonal = 1f / sqrt(2f)
        assertStick(diagonal, -diagonal, JoyConControls.stick(1f, -1f))
        assertStick(0f, -1f, JoyConControls.stick(0f, -1f))
    }

    @Test fun nonFiniteAndOutOfRangeStickValuesCannotPoisonControls() {
        assertStick(1f, 0f, JoyConControls.stick(2f, Float.NaN))
        assertStick(0f, -1f, JoyConControls.stick(Float.POSITIVE_INFINITY, -3f))
        assertStick(0f, 0f, JoyConControls.stick(Float.NaN, Float.NEGATIVE_INFINITY))
    }

    @Test fun physicalAliasesRemainPressedUntilTheirLastRawScanIsReleased() {
        val buttons = JoyConButtons(JoyCon.LEFT)
        val failRecenter = { fail("ordinary controls must not recenter") }
        buttons.key(0x136, true, 1L, failRecenter)
        buttons.key(0x137, true, 1L, failRecenter)
        buttons.key(0x136, false, 2L, failRecenter)
        assertEquals(JoyCon.VR_GRIP, buttons.buttons(2L, failRecenter))
        buttons.key(0x137, false, 3L, failRecenter)
        assertEquals(0, buttons.buttons(3L, failRecenter))
        buttons.key(0x220, true, 4L, failRecenter)
        buttons.key(0x13a, true, 4L, failRecenter)
        buttons.cancel(0x220)
        assertEquals(JoyCon.VR_B, buttons.buttons(5L, failRecenter))
    }

    @Test fun bothSystemButtonsPulseFor120msAfterShortRelease() {
        for ((side, scan) in listOf(JoyCon.LEFT to 0x135, JoyCon.RIGHT to 0x13c)) {
            val buttons = JoyConButtons(side)
            val failRecenter = { fail("short tap must not recenter") }
            buttons.key(scan, true, 1_000_000_000L, failRecenter)
            assertEquals(0, buttons.buttons(1_000_000_000L, failRecenter))
            assertEquals(800L, buttons.nextUpdateMs(1_000_000_000L))
            buttons.key(scan, false, 1_100_000_000L, failRecenter)
            assertEquals(JoyCon.VR_SYSTEM, buttons.buttons(1_100_000_000L, failRecenter))
            assertEquals(120L, buttons.nextUpdateMs(1_100_000_000L))
            assertEquals(JoyCon.VR_SYSTEM, buttons.buttons(1_219_999_999L, failRecenter))
            assertEquals(1L, buttons.nextUpdateMs(1_219_999_999L))
            assertEquals(0, buttons.buttons(1_220_000_000L, failRecenter))
            assertNull(buttons.nextUpdateMs(1_220_000_000L))
        }
    }

    @Test fun holdRecentersAt800msOnceAndDoesNotEmitDashboardOnRelease() {
        val buttons = JoyConButtons(JoyCon.RIGHT)
        var centers = 0
        val recenter = { centers++; Unit }
        buttons.key(0x13c, true, 1_000_000_000L, recenter)
        buttons.key(0x13c, true, 1_200_000_000L, recenter)
        assertEquals(1L, buttons.nextUpdateMs(1_799_999_999L))
        assertEquals(0, buttons.buttons(1_799_999_999L, recenter))
        assertEquals(0, centers)
        assertEquals(0, buttons.buttons(1_800_000_000L, recenter))
        assertEquals(1, centers)
        assertNull(buttons.nextUpdateMs(1_800_000_000L))
        buttons.buttons(3_000_000_000L, recenter)
        buttons.key(0x13c, false, 3_000_000_000L, recenter)
        assertEquals(0, buttons.buttons(3_000_000_000L, recenter))
        assertEquals(1, centers)
    }

    @Test fun clearingHeldOrPulsedInputCancelsAllDelayedActions() {
        val buttons = JoyConButtons(JoyCon.LEFT)
        val failRecenter = { fail("released controller must not recenter") }
        buttons.key(0x135, true, 1_000_000_000L, failRecenter)
        buttons.clear()
        assertEquals(0, buttons.buttons(2_000_000_000L, failRecenter))
        assertNull(buttons.nextUpdateMs(2_000_000_000L))
        buttons.key(0x135, false, 2_000_000_000L, failRecenter)
        assertEquals(0, buttons.buttons(2_000_000_000L, failRecenter))
        buttons.key(0x135, true, 3_000_000_000L, failRecenter)
        buttons.key(0x135, false, 3_100_000_000L, failRecenter)
        buttons.clear()
        assertEquals(0, buttons.buttons(3_110_000_000L, failRecenter))
        assertNull(buttons.nextUpdateMs(3_110_000_000L))
    }

    private fun assertStick(x: Float, y: Float, actual: JoyConControls.Stick) {
        assertEquals(x, actual.x, 1e-6f)
        assertEquals(y, actual.y, 1e-6f)
    }
}
