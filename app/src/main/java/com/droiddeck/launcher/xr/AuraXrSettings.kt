package com.droiddeck.launcher.xr

import android.content.Context
import android.content.SharedPreferences

data class AuraXrPreferences(
    val joyCon6Dof: Boolean = false,
    val cameraGuidedCalibration: Boolean = false,
    val viewZoomPercent: Int = 100,
)

object AuraXrSettings {
    val VIEW_ZOOM_RANGE = 100..300

    internal fun prefs(context: Context): SharedPreferences = context.getSharedPreferences("aura-xr", Context.MODE_PRIVATE)

    internal fun stored(context: Context): AuraXrPreferences = prefs(context).let {
        AuraXrPreferences(it.getBoolean("joyCon6Dof", false), it.getBoolean("cameraGuidedCalibration", false),
            it.getInt("viewZoomPercent", 100).coerceIn(VIEW_ZOOM_RANGE))
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

    fun setViewZoomPercent(context: Context, percent: Int) {
        prefs(context).edit().putInt("viewZoomPercent", percent.coerceIn(VIEW_ZOOM_RANGE)).apply()
    }

    fun reset(context: Context): Boolean = prefs(context).edit().clear().commit()
}
