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
import android.view.InputDevice
import com.droiddeck.launcher.glasses.Quaternion

internal data class XrJoyConOrientation(
    val rotation: Quaternion = Quaternion.IDENTITY,
    val timeNs: Long = 0L,
    val epoch: Int = 0,
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

internal class XrJoyConMotionState {
    private var filter = JoyConOrientation()
    private var reports = JoyConReports()
    private var calibration = JoyConAccelCalibration()
    private var arrivalNs = 0L
    private var current = XrJoyConOrientation()

    @Synchronized fun reset(savedCalibration: FloatArray? = calibration.saved()) {
        filter = JoyConOrientation()
        reports = JoyConReports()
        calibration = JoyConAccelCalibration().apply { restore(savedCalibration) }
        arrivalNs = 0L
        current = XrJoyConOrientation(epoch = current.epoch + 1)
    }

    @Synchronized fun recenter() { current = current.copy(epoch = current.epoch + 1) }
    @Synchronized fun snapshot(): XrJoyConOrientation = current

    @Synchronized fun add(accel: FloatArray, gyro: FloatArray, nowNs: Long): Int {
        if (accel.size < 3 || gyro.size < 3 || (0..2).any { !accel[it].isFinite() || !gyro[it].isFinite() }) {
            reset()
            return 0
        }
        if (arrivalNs != 0L && nowNs < arrivalNs) reset()
        else if (arrivalNs != 0L && nowNs - arrivalNs > XrJoyConOrientation.MAX_AGE_NS) {
            reports = JoyConReports()
            current = current.copy(timeNs = 0L)
        }
        arrivalNs = nowNs
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
        if (!q.w.isFinite() || !q.x.isFinite() || !q.y.isFinite() || !q.z.isFinite()) {
            reset()
            return
        }
        if (filter.ready) current = XrJoyConOrientation(q, arrivalNs, current.epoch)
    }
}

internal class XrJoyConMotion(
    private val context: Context,
    private val nowNs: () -> Long = System::nanoTime,
) : XrJoyConMotionSource {
    private val states = Array(2) { XrJoyConMotionState() }
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
            val device = devices[side]
            if (wanted[side]?.id == device?.id && wanted[side]?.descriptor == device?.descriptor) continue
            hands[side]?.stop()
            hands[side] = null
            wanted[side] = device
            states[side].reset(device?.let { JoyConCalibrations.load(context, it.descriptor) })
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
        if (Build.VERSION.SDK_INT < 31) return
        var waiting = false
        for (side in states.indices) {
            val device = wanted[side] ?: continue
            if (hands[side] != null) continue
            val sensors = runCatching { device.sensorManager }.getOrNull()
            val accel = runCatching { sensors?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) }.getOrNull()
            val gyro = runCatching { sensors?.getDefaultSensor(Sensor.TYPE_GYROSCOPE) }.getOrNull()
            if (sensors == null || accel == null || gyro == null) { waiting = true; continue }
            val handler = handler()
            val hand = Hand(sensors, states[side], handler)
            val listening = runCatching {
                sensors.registerListener(hand, accel, SensorManager.SENSOR_DELAY_FASTEST, handler) &&
                    sensors.registerListener(hand, gyro, SensorManager.SENSOR_DELAY_FASTEST, handler)
            }.getOrDefault(false)
            if (listening) hands[side] = hand
            else { hand.stop(); waiting = true }
        }
        if (waiting && retries++ < 6) handler().postDelayed(retry, 500L)
    }

    private fun handler(): Handler = worker ?: HandlerThread("xr-joycon-motion", Process.THREAD_PRIORITY_DISPLAY)
        .also { thread = it; it.start() }.let { Handler(it.looper).also { out -> worker = out } }

    private inner class Hand(
        private val sensors: SensorManager,
        private val state: XrJoyConMotionState,
        private val handler: Handler,
    ) : SensorEventListener {
        private val accel = FloatArray(3)
        private var accelStamp = Long.MIN_VALUE
        private var accelAtNs = 0L
        private var live = true
        private val flush = Runnable { synchronized(this) { if (live) state.flush() } }

        @Synchronized override fun onSensorChanged(event: SensorEvent) {
            if (!live) return
            val now = nowNs()
            when (event.sensor.type) {
                Sensor.TYPE_ACCELEROMETER -> {
                    if (event.timestamp == accelStamp) return
                    if (event.values.size < 3 || (0..2).any { !event.values[it].isFinite() }) {
                        accelAtNs = 0L
                        state.reset()
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
            runCatching { sensors.unregisterListener(this) }
            handler.removeCallbacks(flush)
        }
    }
}
