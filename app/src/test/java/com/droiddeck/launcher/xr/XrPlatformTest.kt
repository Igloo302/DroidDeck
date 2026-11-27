package com.droiddeck.launcher.xr

import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowBuild

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], manifest = Config.NONE)
class XrPlatformTest {
    @Test fun auraPuckUsesAllThreeBuildPropertiesCaseInsensitively() {
        hardware("XREAL", "Android XR Puck", "axrpuck")
        assertTrue(XrPlatform.isAuraPuck())
        hardware("xReAl", "aNdRoId xR pUcK", "AXRPUCK")
        assertTrue(XrPlatform.isAuraPuck())
    }

    @Test fun partialIdentityMatchesNeverEnableAura() {
        for (matches in 0 until 7) {
            hardware(if (matches and 1 != 0) "XREAL" else "Other",
                if (matches and 2 != 0) "Android XR Puck" else "Other model",
                if (matches and 4 != 0) "axrpuck" else "other-device")
            assertFalse("partial identity mask=$matches", XrPlatform.isAuraPuck())
        }
    }

    @Test fun genericAndroidXrXrealAndQuestIdentitiesRemainOutsideAura() {
        for ((manufacturer, model, device) in listOf(
            Triple("Samsung", "Galaxy XR", "other-headset"),
            Triple("Meta", "Quest 3", "eureka"),
            Triple("XREAL", "Android XR", "axr"),
            Triple("XREAL", "Android XR Puck Pro", "axrpuck"),
            Triple("XREAL", "Android XR Puck", "axrpuck2"),
            Triple("", "", ""),
        )) {
            hardware(manufacturer, model, device)
            assertFalse("$manufacturer/$model/$device", XrPlatform.isAuraPuck())
        }
    }

    private fun hardware(manufacturer: String, model: String, device: String) {
        ShadowBuild.setManufacturer(manufacturer)
        ShadowBuild.setModel(model)
        ShadowBuild.setDevice(device)
    }
}
