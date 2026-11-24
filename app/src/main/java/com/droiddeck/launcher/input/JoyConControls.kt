package com.droiddeck.launcher.input

import kotlin.math.sqrt

internal object JoyConControls {
    const val STICK_DEADZONE = .12f
    const val SYSTEM_HOLD_MS = 800L
    const val SYSTEM_TAP_MS = 120L

    data class Stick(val x: Float, val y: Float)

    fun stick(x: Float, y: Float): Stick {
        val safeX = if (x.isFinite()) x.coerceIn(-1f, 1f) else 0f
        val safeY = if (y.isFinite()) y.coerceIn(-1f, 1f) else 0f
        val length = sqrt(safeX * safeX + safeY * safeY)
        val scale = if (length <= STICK_DEADZONE) 0f
            else ((length - STICK_DEADZONE) / (1f - STICK_DEADZONE)).coerceAtMost(1f) / length
        return Stick(safeX * scale, safeY * scale)
    }
}

internal class JoyConButtons(private val side: Int) {
    private val pressed = HashSet<Int>()
    private var systemAtNs = 0L
    private var systemHeld = false
    private var tapUntilNs = 0L

    fun clear() {
        pressed.clear()
        systemAtNs = 0L; systemHeld = false; tapUntilNs = 0L
    }

    fun key(scan: Int, down: Boolean, now: Long, recenter: () -> Unit) {
        hold(now, recenter)
        val bit = JoyCon.vrButton(side, scan)
        if (bit == 0) return
        val wasSystemDown = systemDown()
        if (down) pressed.add(scan) else pressed.remove(scan)
        if (bit != JoyCon.VR_SYSTEM || wasSystemDown == systemDown()) return
        if (down) { systemAtNs = now; systemHeld = false }
        else {
            if (!systemHeld) tapUntilNs = now + JoyConControls.SYSTEM_TAP_MS * 1_000_000L
            systemAtNs = 0L
        }
    }

    fun cancel(scan: Int) {
        pressed.remove(scan)
        if (JoyCon.vrButton(side, scan) == JoyCon.VR_SYSTEM && !systemDown()) {
            systemAtNs = 0L; systemHeld = false; tapUntilNs = 0L
        }
    }

    fun buttons(now: Long, recenter: () -> Unit): Int {
        hold(now, recenter)
        val tap = if (tapUntilNs != 0L && now < tapUntilNs) JoyCon.VR_SYSTEM else 0
        return pressed.fold(tap) { bits, scan -> bits or (JoyCon.vrButton(side, scan) and JoyCon.VR_SYSTEM.inv()) }
    }

    fun nextUpdateMs(now: Long): Long? {
        var due: Long? = if (systemDown() && !systemHeld)
            systemAtNs + JoyConControls.SYSTEM_HOLD_MS * 1_000_000L else null
        if (tapUntilNs != 0L && now < tapUntilNs) due = due?.coerceAtMost(tapUntilNs) ?: tapUntilNs
        return due?.let { ((it - now).coerceAtLeast(0L) + 999_999L) / 1_000_000L }
    }

    private fun systemDown() = pressed.any { JoyCon.vrButton(side, it) == JoyCon.VR_SYSTEM }

    private fun hold(now: Long, recenter: () -> Unit) {
        if (systemDown() && !systemHeld && now - systemAtNs >= JoyConControls.SYSTEM_HOLD_MS * 1_000_000L) {
            systemHeld = true
            tapUntilNs = 0L
            recenter()
        }
    }
}
