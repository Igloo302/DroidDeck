package com.droiddeck.launcher.xr

import android.content.Context
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowBuild

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], manifest = Config.NONE)
class AuraXrSettingsTest {
    private val context get() = RuntimeEnvironment.getApplication()

    @Before fun setup() {
        ShadowBuild.setManufacturer("XREAL")
        ShadowBuild.setModel("Android XR Puck")
        ShadowBuild.setDevice("axrpuck")
        assertTrue(AuraXrSettings.reset(context))
    }

    @Test fun newAndUpgradedInstallsStartWithBothOptionsOff() {
        context.getSharedPreferences("glasses", Context.MODE_PRIVATE).edit()
            .putBoolean("positionalTracking", true).putBoolean("handControllers", true).putBoolean("joyCons", true).commit()
        assertEquals(AuraXrPreferences(), AuraXrSettings.read(context))
        assertEquals(AuraXrPreferences(), AuraXrSettings.stored(context))
    }

    @Test fun allPreferenceCombinationsRequireSixDofForCameraCalibration() {
        for (sixDof in listOf(false, true)) for (calibration in listOf(false, true)) {
            AuraXrSettings.setJoyCon6Dof(context, sixDof)
            AuraXrSettings.setCameraGuidedCalibration(context, calibration)
            assertEquals(AuraXrPreferences(sixDof, calibration), AuraXrSettings.stored(context))
            assertEquals(AuraXrPreferences(sixDof, sixDof && calibration), AuraXrSettings.read(context))
        }
    }

    @Test fun disablingSixDofKeepsTheSavedCalibrationChoiceInactiveUntilReenabled() {
        AuraXrSettings.setJoyCon6Dof(context, true)
        AuraXrSettings.setCameraGuidedCalibration(context, true)
        AuraXrSettings.setJoyCon6Dof(context, false)
        assertEquals(AuraXrPreferences(), AuraXrSettings.read(context))
        assertEquals(AuraXrPreferences(false, true), AuraXrSettings.stored(context))
        AuraXrSettings.setJoyCon6Dof(context, true)
        assertEquals(AuraXrPreferences(true, true), AuraXrSettings.read(context))
    }

    @Test fun savedOptionsCannotEnableOtherDevices() {
        AuraXrSettings.setJoyCon6Dof(context, true)
        AuraXrSettings.setCameraGuidedCalibration(context, true)
        AuraXrSettings.setViewZoomPercent(context, 225)
        for ((manufacturer, model, device) in listOf(
            Triple("Samsung", "Galaxy XR", "other-headset"),
            Triple("XREAL", "Another headset", "axrpuck"),
            Triple("Other", "Android XR Puck", "axrpuck"),
            Triple("XREAL", "Android XR Puck", "another-device"),
        )) {
            ShadowBuild.setManufacturer(manufacturer)
            ShadowBuild.setModel(model)
            ShadowBuild.setDevice(device)
            assertEquals(AuraXrPreferences(), AuraXrSettings.read(context))
            assertEquals(AuraXrPreferences(true, true, 225), AuraXrSettings.stored(context))
        }
    }

    @Test fun settingsPersistWithoutChangingExistingGlassesPreferences() {
        val glasses = context.getSharedPreferences("glasses", Context.MODE_PRIVATE)
        glasses.edit().putBoolean("positionalTracking", true).commit()
        AuraXrSettings.setJoyCon6Dof(context, true)
        AuraXrSettings.setCameraGuidedCalibration(context, true)
        AuraXrSettings.setViewZoomPercent(context, 175)
        val anotherContext = context.createConfigurationContext(context.resources.configuration)
        assertEquals(AuraXrPreferences(true, true, 175), AuraXrSettings.read(anotherContext))
        assertTrue(glasses.getBoolean("positionalTracking", false))
        assertTrue(AuraXrSettings.reset(anotherContext))
        assertEquals(AuraXrPreferences(), AuraXrSettings.read(context))
        assertTrue(glasses.getBoolean("positionalTracking", false))
    }

    @Test fun viewZoomDefaultsToNormalAndClampsSavedValues() {
        assertEquals(100, AuraXrSettings.read(context).viewZoomPercent)
        for ((requested, expected) in listOf(Int.MIN_VALUE to 100, 99 to 100, 100 to 100,
            175 to 175, 300 to 300, 301 to 300, Int.MAX_VALUE to 300)) {
            AuraXrSettings.setViewZoomPercent(context, requested)
            assertEquals(expected, AuraXrSettings.prefs(context).getInt("viewZoomPercent", -1))
            assertEquals(AuraXrPreferences(viewZoomPercent = expected), AuraXrSettings.read(context))
        }
    }

    @Test fun restoredOutOfRangeZoomCannotReachTheRenderer() {
        for ((stored, expected) in listOf(-500 to 100, 0 to 100, 100 to 100, 150 to 150, 900 to 300)) {
            AuraXrSettings.prefs(context).edit().putInt("viewZoomPercent", stored).commit()
            assertEquals(expected, AuraXrSettings.read(context).viewZoomPercent)
            assertEquals(expected, AuraXrSettings.stored(context).viewZoomPercent)
        }
    }
}
