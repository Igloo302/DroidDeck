package com.droiddeck.launcher

import android.os.Bundle

class AndroidXrActivity : GuestXrActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        intent.putExtra("xr_input", "hands")
        if (com.droiddeck.launcher.gpu.GpuInfo.detect().model == 831)
            intent.putExtra("xr_render_scale", 0.75f)
        super.onCreate(savedInstanceState)
    }
}
