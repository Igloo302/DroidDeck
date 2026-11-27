package com.droiddeck.launcher.input

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.util.Log
import android.view.InputDevice
import com.droiddeck.launcher.glasses.Quaternion

internal data class XrJoyConOrientation(
    val rotation: Quaternion = Quaternion.IDENTITY,
    val timeNs: Long = 0L,
    val epoch: Int = 0,
    val angularSpeed: Float = 0f,
    val recenterSequence: Int = 0,
) {
    fun fresh(nowNs: Long): Boolean = timeNs != 0L && nowNs - timeNs in 0..MAX_AGE_NS

    companion object { const val MAX_AGE_NS = 100_000_000L }
}

internal interface XrJoyConMotionSource {
    fun devices(devices: Array<InputDevice?>)
    fun snapshot(side: Int): XrJoyConOrientation
    fun recenter(side: Int)
    fun close()
}

internal class XrJoyConMotionState(private val diagnostic: (String) -> Unit = {}) {
    private var filter = JoyConOrientation()
    private var reports = JoyConReports()
    private var calibration = JoyConAccelCalibration()
    private var arrivalNs = 0L
    private var current = XrJoyConOrientation()
    private var resetCount = 0L
    private var gapCount = 0L
    private var sampleCount = 0L

    @Synchronized fun reset(savedCalibration: FloatArray? = calibration.saved(), reason: String = "reset") {
        filter = JoyConOrientation()
        reports = JoyConReports()
        calibration = JoyConAccelCalibration().apply { restore(savedCalibration) }
        arrivalNs = 0L
        current = XrJoyConOrientation(epoch = current.epoch + 1, recenterSequence = current.recenterSequence)
        resetCount++
        report("reset reason=$reason")
    }

    @Synchronized fun recenter() {
        current = current.copy(epoch = current.epoch + 1, recenterSequence = current.recenterSequence + 1)
        report("manual_recenter")
    }
    @Synchronized fun refreshListener() {
        reports = JoyConReports()
        report("listener_refresh")
    }
    @Synchronized fun snapshot(): XrJoyConOrientation = current

    private fun report(event: String) {
        diagnostic("$event epoch=${current.epoch} manual=${current.recenterSequence} resets=$resetCount gaps=$gapCount samples=$sampleCount")
    }

    @Synchronized fun add(accel: FloatArray, gyro: FloatArray, nowNs: Long): Int {
        if (accel.size < 3 || gyro.size < 3 || (0..2).any { !accel[it].isFinite() || !gyro[it].isFinite() }) {
            reset(reason = "invalid_sample")
            return 0
        }
        if (arrivalNs != 0L && nowNs < arrivalNs) reset(reason = "clock_reversed")
        else if (arrivalNs != 0L && nowNs - arrivalNs > XrJoyConOrientation.MAX_AGE_NS) {
            reports = JoyConReports()
            current = current.copy(timeNs = 0L)
            gapCount++
            report("sample_gap durationNs=${nowNs - arrivalNs}")
        }
        arrivalNs = nowNs
        sampleCount++
        reports.add(accel, gyro, nowNs)
        return reports.size
    }

    @Synchronized fun flush() {
        val samples = reports.flush()
        if (samples.isEmpty()) return
        for (sample in samples) {
            filter.update(calibration.correct(JoyConOrientation.axes(sample.accel)),
                JoyConOrientation.axes(sample.gyro), sample.dt)
        }
        val q = filter.rotation
        val speed = filter.rate.length()
        if (!q.w.isFinite() || !q.x.isFinite() || !q.y.isFinite() || !q.z.isFinite() || !speed.isFinite()) {
            reset(reason = "invalid_filter")
            return
        }
        if (filter.ready) current = current.copy(rotation = q, timeNs = arrivalNs, angularSpeed = speed)
    }
}

internal data class XrJoyConSensorIdentity(
    val id: Int, val type: Int, val name: String, val vendor: String, val version: Int,
    val resolution: Float, val maxRange: Float, val minDelay: Int,
) {
    companion object {
        fun of(sensor: Sensor) = XrJoyConSensorIdentity(sensor.id, sensor.type, sensor.name.orEmpty(), sensor.vendor.orEmpty(),
            sensor.version, sensor.resolution, sensor.maximumRange, sensor.minDelay)
    }
}

internal data class XrJoyConSensors(val manager: SensorManager, val accel: Sensor, val gyro: Sensor) {
    val identity get() = listOf(XrJoyConSensorIdentity.of(accel), XrJoyConSensorIdentity.of(gyro))
}

internal class XrJoyConMotion(
    private val context: Context,
    private val nowNs: () -> Long = System::nanoTime,
    private val sensorsFor: (InputDevice) -> XrJoyConSensors? = { device ->
        if (Build.VERSION.SDK_INT < 31) null else {
            val manager = device.sensorManager
            val accel = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
            val gyro = manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE)
            if (accel == null || gyro == null) null else XrJoyConSensors(manager, accel, gyro)
        }
    },
) : XrJoyConMotionSource {
    private data class DeviceIdentity(val id: Int, val descriptor: String, val side: Int)
    private val identities = arrayOfNulls<DeviceIdentity>(2)
    private val labels = Array(2) { "none" }
    private val states = Array(2) { side -> XrJoyConMotionState { event ->
        Log.i("DroidDeckXR", "joycon motion side=$side device=${labels[side]} $event")
    } }
    private val sensorIdentities = arrayOfNulls<List<XrJoyConSensorIdentity>>(2)
    private val wanted = arrayOfNulls<InputDevice>(2)
    private val hands = arrayOfNulls<Hand>(2)
    private var thread: HandlerThread? = null
    private var worker: Handler? = null
    private var closed = false
    private var retries = 0
    private val retry = Runnable { synchronized(this) { if (!closed) connect() } }

    @Synchronized override fun devices(devices: Array<InputDevice?>) {
        if (closed) return
        for (side in states.indices) {
            val device = devices[side]?.takeIf { JoyCon.side(it) == side }
            val identity = device?.let { DeviceIdentity(it.id, it.descriptor, side) }
            if (identities[side] != identity) {
                hands[side]?.stop()
                hands[side] = null
                labels[side] = (identity ?: identities[side]).toString()
                identities[side] = identity
                sensorIdentities[side] = null
                states[side].reset(device?.let { JoyConCalibrations.load(context, it.descriptor) },
                    if (device == null) "device_unavailable" else "device_changed")
            }
            wanted[side] = device
        }
        retries = 0
        connect()
    }

    override fun snapshot(side: Int): XrJoyConOrientation = states[side].snapshot()
    override fun recenter(side: Int) = states[side].recenter()

    @Synchronized override fun close() {
        if (closed) return
        devices(arrayOfNulls(2))
        closed = true
        worker?.removeCallbacks(retry)
        thread?.quitSafely()
    }

    private fun connect() {
        worker?.removeCallbacks(retry)
        var waiting = false
        for (side in states.indices) {
            val device = wanted[side] ?: continue
            val sensors = runCatching { sensorsFor(device) }.getOrNull()
            if (sensors == null) { waiting = true; continue }
            val identity = sensors.identity
            if (sensorIdentities[side] != null && sensorIdentities[side] != identity) {
                hands[side]?.stop()
                hands[side] = null
                states[side].reset(reason = "sensors_changed")
            }
            sensorIdentities[side] = identity
            val previous = hands[side]
            if (previous?.matches(device, sensors) == true) continue
            if (previous != null) {
                previous.stop()
                states[side].refreshListener()
            }
            hands[side] = null
            val handler = handler()
            val hand = Hand(device, sensors, states[side], handler)
            val listening = runCatching {
                sensors.manager.registerListener(hand, sensors.accel, SensorManager.SENSOR_DELAY_FASTEST, handler) &&
                    sensors.manager.registerListener(hand, sensors.gyro, SensorManager.SENSOR_DELAY_FASTEST, handler)
            }.getOrDefault(false)
            if (listening) hands[side] = hand
            else { hand.stop(); waiting = true }
        }
        if (waiting && retries++ < 6) handler().postDelayed(retry, 500L)
    }

    private fun handler(): Handler = worker ?: HandlerThread("xr-joycon-motion", Process.THREAD_PRIORITY_DISPLAY)
        .also { thread = it; it.start() }.let { Handler(it.looper).also { out -> worker = out } }

    private inner class Hand(
        private val device: InputDevice,
        private val sensors: XrJoyConSensors,
        private val state: XrJoyConMotionState,
        private val handler: Handler,
    ) : SensorEventListener {
        private val accel = FloatArray(3)
        private var accelStamp = Long.MIN_VALUE
        private var accelAtNs = 0L
        private var live = true
        private val flush = Runnable { synchronized(this) { if (live) state.flush() } }

        fun matches(device: InputDevice, sensors: XrJoyConSensors): Boolean = this.device === device &&
            this.sensors.manager === sensors.manager && this.sensors.accel === sensors.accel && this.sensors.gyro === sensors.gyro

        @Synchronized override fun onSensorChanged(event: SensorEvent) {
            if (!live) return
            val now = nowNs()
            when (event.sensor.type) {
                Sensor.TYPE_ACCELEROMETER -> {
                    if (event.timestamp == accelStamp) return
                    if (event.values.size < 3 || (0..2).any { !event.values[it].isFinite() }) {
                        accelAtNs = 0L
                        state.reset(reason = "invalid_accel")
                        return
                    }
                    accelStamp = event.timestamp
                    event.values.copyInto(accel, 0, 0, 3)
                    accelAtNs = now
                }
                Sensor.TYPE_GYROSCOPE -> {
                    if (accelAtNs == 0L || now - accelAtNs !in 0..XrJoyConOrientation.MAX_AGE_NS) return
                    val size = state.add(accel, event.values, now)
                    handler.removeCallbacks(flush)
                    if (size >= JoyConReports.SAMPLES_PER_REPORT) state.flush()
                    else if (size > 0) handler.postDelayed(flush, 3L)
                }
            }
        }

        override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit

        @Synchronized fun stop() {
            live = false
            runCatching { sensors.manager.unregisterListener(this) }
            handler.removeCallbacks(flush)
        }
    }
}
