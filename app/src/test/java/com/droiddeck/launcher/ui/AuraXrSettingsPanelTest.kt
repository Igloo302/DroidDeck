package com.droiddeck.launcher.ui

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.View
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.test.*
import androidx.compose.ui.unit.dp
import com.droiddeck.launcher.R
import com.droiddeck.launcher.glasses.Glasses
import com.droiddeck.launcher.glasses.GlassesState
import com.droiddeck.launcher.gpu.FrameGen
import com.droiddeck.launcher.gpu.Lossless
import com.droiddeck.launcher.input.SecondScreenMode
import com.droiddeck.launcher.xr.AuraXrPreferences
import com.droiddeck.launcher.xr.AuraXrSettings
import java.io.File
import kotlinx.coroutines.flow.MutableStateFlow
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import org.robolectric.shadows.ShadowBuild

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w360dp-h800dp-port", application = Application::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class AuraXrSettingsPanelTest {
    @get:Rule val compose = createRobolectricComposeRule()
    private val context get() = RuntimeEnvironment.getApplication()
    private lateinit var view: View
    private lateinit var previousGlasses: GlassesState

    @Before fun prepare() {
        ShadowBuild.setManufacturer("XREAL")
        ShadowBuild.setModel("Android XR Puck")
        ShadowBuild.setDevice("axrpuck")
        AuraXrSettings.reset(context)
        previousGlasses = Glasses.state.value
        glassesState().value = GlassesState()
    }

    @After fun restore() { glassesState().value = previousGlasses }

    @Test fun portraitSetupShowsBothOptionsOffWithoutUsbDisplayControls() {
        showSetup()
        compose.onNodeWithTag("setup-glasses-tab-aura").assertExists()
        zoom().assertIsEnabled().assert(zoomAt(100))
        toggle("aura-joycon-6dof").assertIsEnabled().assert(off())
        toggle("aura-camera-calibration").assertIsNotEnabled().assert(off())
        compose.onNodeWithText(text(R.string.aura_camera_calibration_requires_6dof)).assertExists()
        compose.onNodeWithText(text(R.string.setup_glasses_connect)).assertDoesNotExist()
        toggle("glasses-game").assertDoesNotExist()
        toggle("glasses-3dof").assertDoesNotExist()
        savePreview("setup-portrait")
        compose.onNodeWithText(text(R.string.aura_joycon_recenter)).performScrollTo().assertIsDisplayed()
        savePreview("setup-portrait-recenter")
    }

    @Test
    @Config(qualifiers = "w960dp-h540dp-land")
    fun landscapeSettingsReactToChangesAndPreserveAnInactiveCalibrationChoice() {
        showSetup()
        toggle("aura-joycon-6dof").performScrollTo().performClick()
        toggle("aura-camera-calibration").assertIsEnabled().performScrollTo().performClick()
        assertEquals(AuraXrPreferences(true, true), AuraXrSettings.read(context))
        savePreview("setup-landscape")
        compose.runOnIdle { AuraXrSettings.setJoyCon6Dof(context, false) }
        toggle("aura-joycon-6dof").assert(off())
        toggle("aura-camera-calibration").assertIsNotEnabled().assert(off())
        assertEquals(AuraXrPreferences(), AuraXrSettings.read(context))
        compose.runOnIdle { AuraXrSettings.setJoyCon6Dof(context, true) }
        toggle("aura-camera-calibration").assertIsEnabled().assert(on())
        compose.runOnIdle { AuraXrSettings.reset(context) }
        toggle("aura-joycon-6dof").assert(off())
        toggle("aura-camera-calibration").assertIsNotEnabled().assert(off())
    }

    @Test
    @Config(qualifiers = "w960dp-h540dp-land")
    fun sessionDrawerOffersTheSameAuraSettingsWithoutUsbGlasses() {
        compose.setContent {
            DroidDeckTheme {
                view = LocalView.current
                SessionDrawer(true, SessionDrawerPage.GLASSES, false, {}, actions())
            }
        }
        compose.onNodeWithTag("glasses-tab-aura").assertExists()
        zoom().performScrollTo().performSemanticsAction(SemanticsActions.SetProgress) { it(150f) }
        assertEquals(150, AuraXrSettings.read(context).viewZoomPercent)
        toggle("aura-joycon-6dof").assertExists().performScrollTo().performClick()
        assertTrue(AuraXrSettings.read(context).joyCon6Dof)
        toggle("aura-camera-calibration").assertIsEnabled()
        toggle("glasses-game").assertDoesNotExist()
        savePreview("session-landscape")
    }

    @Test fun otherHeadsetsDoNotShowAuraControls() {
        ShadowBuild.setModel("Another headset")
        showSetup()
        toggle("aura-joycon-6dof").assertDoesNotExist()
        toggle("aura-camera-calibration").assertDoesNotExist()
        zoom().assertDoesNotExist()
        compose.onNodeWithText(text(R.string.setup_glasses_connect)).assertExists()
    }

    @Test fun settingsSearchOnlyOffersAuraControlsOnTheMatchingDevice() {
        assertEquals(setOf(R.string.aura_joycon_6dof, R.string.aura_camera_calibration, R.string.aura_view_zoom),
            SettingsIndex.search(context, "aura").map { it.title }.toSet())
        assertEquals(listOf(R.string.aura_view_zoom), SettingsIndex.search(context, "aura fov").map { it.title })
        ShadowBuild.setModel("Another headset")
        assertTrue(SettingsIndex.search(context, "aura").isEmpty())
    }

    @Test fun viewZoomChangesLiveInFivePercentStepsWithoutEnablingCameraFeatures() {
        showSetup()
        zoom().assert(zoomAt(100)).performSemanticsAction(SemanticsActions.SetProgress) { it(177f) }
        zoom().assert(zoomAt(175))
        assertEquals(AuraXrPreferences(viewZoomPercent = 175), AuraXrSettings.read(context))
        savePreview("zoom-portrait")
        compose.runOnIdle { AuraXrSettings.setViewZoomPercent(context, 300) }
        zoom().assert(zoomAt(300))
        compose.runOnIdle { AuraXrSettings.reset(context) }
        zoom().assert(zoomAt(100))
    }

    private fun showSetup() {
        compose.setContent {
            DroidDeckTheme {
                view = LocalView.current
                Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
                    GlassesSetupPanel()
                }
            }
        }
    }

    private fun actions() = DrawerActions(steam = true, desktop = false, isHomeApp = false,
        androidApps = emptyList(), hudOn = false, frameGen = FrameGen.Mode.OFF, lossless = Lossless.State.NONE,
        oscMode = "never", suspendPolicy = "auto", backActionsInverted = false, touchMode = "auto",
        touchAuto = "touchpad", fexPreset = "default", glassesPresent = false,
        secondScreenMode = SecondScreenMode.NONE, secondScreenDisplays = emptyList(), selectedSecondScreenDisplay = 1,
        onHud = {}, onFrameGenPick = {}, onImportLossless = {}, onKeyboard = {}, onHardwareKeyboard = {},
        onSteamMenu = null, onQam = null, onOsc = {}, onSuspendPolicy = {}, onBackActionsInverted = {},
        onTouch = {}, onFexPreset = {}, onSecondScreenMode = {}, onSecondScreenDisplay = {},
        onLaunchAndroidApp = { _, _ -> }, onBackground = {}, onShareLogs = {}, onStop = {}, onClose = {})

    private fun toggle(key: String) = compose.onNodeWithTag("setting-$key")
    private fun zoom() = compose.onNodeWithTag("aura-view-zoom")
    private fun zoomAt(percent: Int) = SemanticsMatcher.expectValue(SemanticsProperties.ProgressBarRangeInfo,
        ProgressBarRangeInfo(percent.toFloat(), 100f..300f))
    private fun text(id: Int) = context.getString(id)
    private fun off() = SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, text(R.string.widgets_off))
    private fun on() = SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, text(R.string.widgets_on))

    private fun savePreview(name: String) {
        val file = File("build/reports/aura-settings/$name.png")
        file.parentFile?.mkdirs()
        compose.runOnIdle {
            val root = view.rootView
            val bitmap = Bitmap.createBitmap(root.width, root.height, Bitmap.Config.ARGB_8888)
            root.draw(Canvas(bitmap))
            file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bitmap.recycle()
        }
    }

    @Suppress("UNCHECKED_CAST")
    private fun glassesState() = Glasses::class.java.getDeclaredField("_state").apply { isAccessible = true }
        .get(null) as MutableStateFlow<GlassesState>
}
