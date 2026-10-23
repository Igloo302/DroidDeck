package com.droiddeck.launcher.gpu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class TurnipStartupFailureTest {
    @get:Rule val temporary = TemporaryFolder()
    private val auraError = "TU: error: ../mesa-b/src/freedreno/vulkan/tu_device.cc:1776: device (chip_id = 44050100, gpu_id = 6851) is unsupported (VK_ERROR_INCOMPATIBLE_DRIVER)"

    @Test fun identifiesAuraChipFromTheActualDriverRejection() {
        val log = temporary.newFile().apply { writeText(auraError) }
        assertEquals("0x44050100", TurnipStartupFailure.unsupportedChipId(log))
        log.writeText(auraError.replace("44050100", "0xFFFF44050100"))
        assertEquals("0xffff44050100", TurnipStartupFailure.unsupportedChipId(log))
    }

    @Test fun unrelatedDriverAndSurfaceFailuresDoNotClaimAnUnsupportedChip() {
        val log = temporary.newFile().apply {
            writeText("failed to query kernel driver version (VK_ERROR_INCOMPATIBLE_DRIVER)\n" +
                "SDL_Vulkan_CreateSurface failed: VK_KHR_wayland_surface extension is not enabled")
        }
        assertNull(TurnipStartupFailure.unsupportedChipId(log))
        assertNull(TurnipStartupFailure.unsupportedChipId(null))
        assertNull(TurnipStartupFailure.unsupportedChipId(temporary.root.resolve("missing")))
    }

    @Test fun readsOnlyTheBoundedTailOfLargeLogs() {
        val log = temporary.newFile().apply { writeText("x".repeat(300 * 1024) + "\n" + auraError) }
        assertEquals("0x44050100", TurnipStartupFailure.unsupportedChipId(log))
        log.writeText(auraError + "\n" + "x".repeat(300 * 1024))
        assertNull(TurnipStartupFailure.unsupportedChipId(log))
    }
}
