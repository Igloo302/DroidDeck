package com.droiddeck.launcher.xr

import android.content.Context
import android.content.SharedPreferences

data class AuraXrPreferences(
    val joyCon6Dof: Boolean = false,
    val cameraGuidedCalibration: Boolean = false,
)

object AuraXrSettings {
    internal fun prefs(context: Context): SharedPreferences = context.getSharedPreferences("aura-xr", Context.MODE_PRIVATE)

    internal fun stored(context: Context): AuraXrPreferences = prefs(context).let {
        AuraXrPreferences(it.getBoolean("joyCon6Dof", false), it.getBoolean("cameraGuidedCalibration", false))
    }

    fun read(context: Context): AuraXrPreferences {
        if (!XrPlatform.isAuraPuck()) return AuraXrPreferences()
        val saved = stored(context)
        return saved.copy(cameraGuidedCalibration = saved.joyCon6Dof && saved.cameraGuidedCalibration)
    }

    fun setJoyCon6Dof(context: Context, enabled: Boolean) {
        prefs(context).edit().putBoolean("joyCon6Dof", enabled).apply()
    }

    fun setCameraGuidedCalibration(context: Context, enabled: Boolean) {
        prefs(context).edit().putBoolean("cameraGuidedCalibration", enabled).apply()
    }

    fun reset(context: Context): Boolean = prefs(context).edit().clear().commit()
}
