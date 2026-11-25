package com.droiddeck.launcher.ui

import android.content.SharedPreferences
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import com.droiddeck.launcher.R
import com.droiddeck.launcher.xr.AuraXrSettings
import com.droiddeck.launcher.xr.XrPlatform

@Composable
internal fun AuraXrSettingsPanel(host: MenuHost, track: (String) -> Modifier = { Modifier }) {
    if (!XrPlatform.isAuraPuck()) return
    val context = LocalContext.current
    var settings by remember(context) { mutableStateOf(AuraXrSettings.stored(context)) }
    DisposableEffect(context) {
        val prefs = AuraXrSettings.prefs(context)
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> settings = AuraXrSettings.stored(context) }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        settings = AuraXrSettings.stored(context)
        onDispose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }
    SettingsGroup(stringResource(R.string.aura_joycon_group)) {
        ToggleRow(host, "aura-joycon-6dof", stringResource(R.string.aura_joycon_6dof),
            stringResource(R.string.aura_joycon_6dof_hint), settings.joyCon6Dof,
            chipModifier = track("aura-joycon-6dof")) { AuraXrSettings.setJoyCon6Dof(context, it) }
        ToggleRow(host, "aura-camera-calibration", stringResource(R.string.aura_camera_calibration),
            stringResource(if (settings.joyCon6Dof) R.string.aura_camera_calibration_hint else R.string.aura_camera_calibration_requires_6dof),
            settings.joyCon6Dof && settings.cameraGuidedCalibration, enabled = settings.joyCon6Dof,
            chipModifier = track("aura-camera-calibration")) { AuraXrSettings.setCameraGuidedCalibration(context, it) }
        SettingsRow(stringResource(R.string.aura_joycon_recenter), stringResource(R.string.aura_joycon_recenter_hint), hintLines = 5) {}
    }
}
