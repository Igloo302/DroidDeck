package com.droiddeck.launcher.xr

import android.content.Context
import android.os.Build
import com.droiddeck.launcher.session.QuestExperimental
import com.droiddeck.xr.XrPlatform as Platform

object XrPlatform {
    fun isAuraPuck(
        manufacturer: String = Build.MANUFACTURER,
        model: String = Build.MODEL,
        device: String = Build.DEVICE,
    ): Boolean = manufacturer.equals("XREAL", ignoreCase = true) &&
        model.equals("Android XR Puck", ignoreCase = true) && device.equals("axrpuck", ignoreCase = true)

    fun current(context: Context): Platform = Platform.detect(
        hasAndroidOpenXrFeature = context.packageManager.hasSystemFeature(Platform.ANDROID_OPENXR_FEATURE),
        isQuestDevice = QuestExperimental.isSupported(),
    )
}
