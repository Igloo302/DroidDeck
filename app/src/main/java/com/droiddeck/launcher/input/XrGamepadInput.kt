package com.droiddeck.launcher.input

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.roundToInt

internal class XrGamepadMenu {
    data class Result(val consume: Boolean = false, val request: Boolean = false)
    private data class Keys(var down: Int = 0, var latched: Boolean = false)
    private val devices = HashMap<Int, Keys>()

    fun key(device: Int, code: Int, action: Int, repeat: Int): Result {
        val bit = when (code) {
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BACK -> 1
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_MENU -> 2
            KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_HOME -> 4
            else -> return Result()
        }
        val keys = devices.getOrPut(device) { Keys() }
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) return Result(keys.latched || bit == 4)
        val wasDown = keys.down and bit != 0
        if (action == KeyEvent.ACTION_DOWN) keys.down = keys.down or bit
        else keys.down = keys.down and bit.inv()
        val request = action == KeyEvent.ACTION_DOWN && !wasDown && repeat == 0 && !keys.latched &&
            (bit == 4 || keys.down and 3 == 3)
        val consume = keys.latched || request || bit == 4
        if (request) keys.latched = true
        if (keys.down == 0) devices.remove(device)
        return Result(consume, request)
    }

    fun remove(device: Int) { devices.remove(device) }
    fun clear() { devices.clear() }
}

internal class XrGamepadInput(
    context: Context,
    private val nowNs: () -> Long = System::nanoTime,
    private val orientation: XrJoyConMotionSource = XrJoyConMotion(context, nowNs),
    private val bridge: () -> PadBridge?,
) : InputManager.InputDeviceListener {
    private val manager = context.getSystemService(InputManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private val menu = XrGamepadMenu()
    private val sequence = AtomicInteger()
    private class JoyConState(val deviceId: Int, val side: Int) {
        val buttons = JoyConButtons(side)
        var stickX = 0
        var stickY = 0
        fun release() {
            buttons.clear(); stickX = 0; stickY = 0
        }
        fun stick(x: Float, y: Float) {
            val normalized = JoyConControls.stick(x, y)
            stickX = stickValue(normalized.x)
            stickY = stickValue(normalized.y)
        }
    }
    private val joyCons = arrayOfNulls<JoyConState>(2)
    private var joyConBridge: PadBridge? = null
    private var resumed = false
    private var focused = false
    val menuSequence: Int get() = sequence.get()

    @Synchronized fun resume() {
        if (resumed) return
        resumed = true
        manager.registerInputDeviceListener(this, handler)
        syncControllers()
        logDevices()
    }

    @Synchronized fun pause() {
        if (!resumed) return
        resumed = false
        manager.unregisterInputDeviceListener(this)
        release()
        orientation.devices(arrayOfNulls(2))
        joyConBridge?.releaseXrJoyCons(this)
        joyConBridge = null
    }

    @Synchronized fun close() { pause(); orientation.close() }

    @Synchronized fun focus(active: Boolean) {
        if (focused == active) return
        focused = active
        if (!active) release()
    }

    private fun release() {
        menu.clear()
        joyCons.forEach { it?.release() }
        bridge()?.releaseAll()
    }

    @Synchronized fun joyConState(): IntArray {
        val state = IntArray(18)
        if (!resumed || !focused) return state
        val now = nowNs()
        for (side in joyCons.indices) {
            val hand = joyCons[side] ?: continue
            val base = side * 9
            state[base] = JOYCON_CONNECTED or hand.buttons.buttons(now) { orientation.recenter(side) }
            state[base + 1] = hand.stickX
            state[base + 2] = hand.stickY
            val sample = orientation.snapshot(side)
            state[base + 8] = sample.epoch
            if (sample.fresh(now)) {
                state[base + 3] = sample.rotation.x.toRawBits()
                state[base + 4] = sample.rotation.y.toRawBits()
                state[base + 5] = sample.rotation.z.toRawBits()
                state[base + 6] = sample.rotation.w.toRawBits()
                state[base + 7] = 1
            }
        }
        return state
    }

    @Synchronized fun key(event: KeyEvent): Boolean {
        val side = JoyCon.side(event.device)
        if (side >= 0) {
            if (!resumed || !focused) return true
            val hand = joyCon(event.deviceId, side) ?: return true
            if (JoyCon.vrButton(side, event.scanCode) == 0) return true
            if (event.isCanceled) hand.buttons.cancel(event.scanCode)
            else if (event.action == KeyEvent.ACTION_UP ||
                (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0)) {
                hand.buttons.key(event.scanCode, event.action == KeyEvent.ACTION_DOWN, nowNs()) { orientation.recenter(side) }
            }
            return true
        }
        val normalized = JoyCon.normalize(event)
        if (!isController(normalized.device) || !PadBridge.isControllerKeyEvent(normalized)) return false
        if (!resumed || !focused) return true
        val pad = bridge() ?: return true
        val result = menu.key(normalized.deviceId, normalized.keyCode, normalized.action, normalized.repeatCount)
        if (result.request) {
            // The first chord key may already be in the guest; release it before the VR menu pulse.
            for (code in intArrayOf(KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BACK,
                    KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_MENU)) {
                pad.onKeyEvent(KeyEvent(normalized.downTime, normalized.eventTime, KeyEvent.ACTION_UP,
                    code, 0, normalized.metaState, normalized.deviceId, 0, normalized.flags, normalized.source))
            }
            val value = sequence.incrementAndGet()
            Log.i("DroidDeckXR", "gamepad dashboard request=$value device=${normalized.deviceId}")
        }
        return if (result.consume) true else pad.onKeyEvent(normalized)
    }

    @Synchronized fun motion(event: MotionEvent): Boolean {
        val device = event.device
        val side = JoyCon.side(device)
        if (side >= 0) {
            if (!resumed || !focused || !event.isFromSource(InputDevice.SOURCE_JOYSTICK) ||
                event.actionMasked != MotionEvent.ACTION_MOVE) return true
            val hand = joyCon(event.deviceId, side) ?: return true
            val axes = JoyCon.stickAxes(side, device!!, event.source)
            hand.stick(event.getAxisValue(axes[0]), -event.getAxisValue(axes[1]))
            return true
        }
        if (!PadBridge.isControllerMotionEvent(event) || !isController(event.device)) return false
        if (!resumed || !focused) return true
        return bridge()?.onMotionEvent(event) ?: true
    }

    @Synchronized override fun onInputDeviceAdded(deviceId: Int) {
        if (!resumed) return
        syncControllers()
        logDevices()
    }
    @Synchronized override fun onInputDeviceRemoved(deviceId: Int) {
        if (!resumed) return
        menu.remove(deviceId)
        removeJoyCon(deviceId)
        bridge()?.onDeviceRemoved(deviceId)
    }
    @Synchronized override fun onInputDeviceChanged(deviceId: Int) {
        if (!resumed) return
        menu.remove(deviceId)
        removeJoyCon(deviceId)
        bridge()?.onDeviceRemoved(deviceId)
        syncControllers()
    }

    private fun syncControllers() {
        val devices = manager.inputDeviceIds.map(manager::getInputDevice).filterNotNull()
        for (side in joyCons.indices) {
            val current = joyCons[side]
            val found = devices.firstOrNull { it.id == current?.deviceId && JoyCon.side(it) == side }
                ?: devices.firstOrNull { JoyCon.side(it) == side }
            if (current?.deviceId != found?.id) joyCons[side] = found?.let { JoyConState(it.id, side) }
        }
        orientation.devices(Array(2) { side -> joyCons[side]?.deviceId?.let(manager::getInputDevice) })
        val pad = bridge()
        if (joyConBridge !== pad) {
            joyConBridge?.releaseXrJoyCons(this)
            joyConBridge = pad
            pad?.claimXrJoyCons(this)
        }
        if (pad == null) return
        for (device in devices) {
            if (!isController(device) || JoyCon.side(device) >= 0) pad.onDeviceRemoved(device.id)
        }
        pad.syncConnectedControllers { isController(it) && JoyCon.side(it) < 0 }
    }

    private fun joyCon(deviceId: Int, side: Int): JoyConState? {
        if (joyCons[side]?.deviceId != deviceId) syncControllers()
        return joyCons[side]?.takeIf { it.deviceId == deviceId }
    }

    private fun removeJoyCon(deviceId: Int) {
        for (side in joyCons.indices) if (joyCons[side]?.deviceId == deviceId) joyCons[side] = null
        orientation.devices(Array(2) { side -> joyCons[side]?.deviceId?.let(manager::getInputDevice) })
    }

    private fun logDevices() {
        for (id in manager.inputDeviceIds) {
            val device = manager.getInputDevice(id) ?: continue
            if (isController(device))
                Log.i("DroidDeckXR", "gamepad available id=$id name=${device.name} sources=${device.sources}")
        }
    }

    fun nativeMotion(device: Int, source: Int, action: Int, downTime: Long, eventTime: Long, axes: FloatArray): Boolean {
        if (axes.size != AXES.size) return false
        val coords = MotionEvent.PointerCoords().apply {
            for (i in AXES.indices) setAxisValue(AXES[i], axes[i])
        }
        val event = MotionEvent.obtain(downTime, eventTime, action, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }), arrayOf(coords),
            0, 0, 1f, 1f, device, 0, source, 0)
        return try { motion(event) } finally { event.recycle() }
    }

    companion object {
        internal const val JOYCON_CONNECTED = 1 shl 6
        private fun stickValue(value: Float): Int =
            if (value.isFinite()) (value.coerceIn(-1f, 1f) * 32767).roundToInt() else 0

        internal fun isController(device: InputDevice?): Boolean {
            if (!PadBridge.isFromController(device) || device == null) return false
            if (device.supportsSource(InputDevice.SOURCE_GAMEPAD) || device.motionRanges.isEmpty()) return true
            return AXES.any { axis ->
                device.getMotionRange(axis, InputDevice.SOURCE_JOYSTICK)?.let { it.max > it.min } == true
            }
        }

        private val AXES = intArrayOf(MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
            MotionEvent.AXIS_RX, MotionEvent.AXIS_RY, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y,
            MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_BRAKE, MotionEvent.AXIS_GAS)
    }
}
