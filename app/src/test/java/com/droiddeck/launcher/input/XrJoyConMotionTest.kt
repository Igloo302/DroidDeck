package com.droiddeck.launcher.input

import android.hardware.Sensor
import android.hardware.SensorManager
import android.view.InputDevice
import com.droiddeck.launcher.glasses.Quaternion
import com.droiddeck.launcher.glasses.Vec3
import kotlin.math.cos
import kotlin.math.sin
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.InputDeviceBuilder
import org.robolectric.shadows.SensorEventBuilder
import org.robolectric.shadows.ShadowSensor

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], manifest = Config.NONE)
class XrJoyConMotionTest {
    private val state = XrJoyConMotionState()
    private val faceUp = floatArrayOf(0f, 0f, G)
    private val still = FloatArray(3)
    private var now = 1_000_000_000L

    @Test fun gravityInitialisesOrientationAndRawArrivalControlsFreshness() {
        assertFalse(state.snapshot().fresh(now))
        state.add(faceUp, still, now)
        state.flush()
        val sample = state.snapshot()
        assertRotation(Quaternion.IDENTITY, sample.rotation)
        assertEquals(now, sample.timeNs)
        assertTrue(sample.fresh(now + 100_000_000L))
        assertFalse(sample.fresh(now + 100_000_001L))
        assertFalse(sample.fresh(now - 1))
    }

    @Test fun orientationWaitsForPlausibleGravity() {
        state.add(floatArrayOf(0f, 0f, 3f), still, now)
        state.flush()
        assertFalse(state.snapshot().fresh(now))
        now += 10_000_000L
        state.add(faceUp, still, now)
        state.flush()
        assertTrue(state.snapshot().fresh(now))
    }

    @Test fun reportIntegrationRetainsExistingJoyConAxesAndRotation() {
        state.add(faceUp, still, now)
        state.flush()
        repeat(100) {
            now += 10_000_000L
            repeat(3) { state.add(faceUp, floatArrayOf(0f, 0f, 1f), now) }
            state.flush()
        }
        val sample = state.snapshot()
        val forward = sample.rotation.rotate(Vec3(0f, 0f, -1f))
        assertEquals(-sin(1f), forward.x, 2e-3f)
        assertEquals(0f, forward.y, 2e-3f)
        assertEquals(-cos(1f), forward.z, 2e-3f)
        assertEquals(1f, sample.angularSpeed, 1e-5f)
        assertTrue(sample.fresh(now))
    }

    @Test fun sensorGapDiscardsPendingReportsWithoutChangingHeadingOrEpoch() {
        state.add(faceUp, still, now)
        state.flush()
        repeat(100) {
            now += 10_000_000L
            state.add(faceUp, floatArrayOf(0f, 0f, 1f), now)
            state.flush()
        }
        val before = state.snapshot()
        assertTrue(before.rotation.rotate(Vec3(0f, 0f, -1f)).x < -.8f)
        now += 10_000_000L
        state.add(faceUp, floatArrayOf(0f, 0f, 9f), now)
        now += 150_000_000L
        state.add(faceUp, still, now)
        assertFalse(state.snapshot().fresh(now))
        assertEquals(before.epoch, state.snapshot().epoch)
        assertRotation(before.rotation, state.snapshot().rotation)
        state.flush()
        assertEquals(before.epoch, state.snapshot().epoch)
        assertRotation(before.rotation, state.snapshot().rotation)
        assertEquals(now, state.snapshot().timeNs)
        assertTrue(state.snapshot().fresh(now))
    }

    @Test fun sensorGapPreservesLearnedGyroBias() {
        val biased = floatArrayOf(0f, 0f, .02f)
        state.add(faceUp, biased, now)
        state.flush()
        repeat(2000) {
            now += 10_000_000L
            state.add(faceUp, biased, now)
            state.flush()
        }
        val before = state.snapshot()
        now += 150_000_000L
        state.add(faceUp, biased, now)
        state.flush()
        repeat(100) {
            now += 10_000_000L
            state.add(faceUp, biased, now)
            state.flush()
        }
        assertEquals(before.epoch, state.snapshot().epoch)
        assertRotation(before.rotation, state.snapshot().rotation)
        assertEquals(0f, state.snapshot().angularSpeed, 2e-6f)
        assertTrue(state.snapshot().fresh(now))
    }

    @Test fun invalidSamplesCannotPoisonOrientationAndRecoveryChangesEpoch() {
        state.add(faceUp, still, now)
        state.flush()
        val epoch = state.snapshot().epoch
        now += 10_000_000L
        assertEquals(0, state.add(faceUp, floatArrayOf(Float.NaN, 0f, 0f), now))
        state.flush()
        assertFalse(state.snapshot().fresh(now))
        assertEquals(epoch + 1, state.snapshot().epoch)
        state.add(faceUp, still, now)
        state.flush()
        assertTrue(state.snapshot().fresh(now))
        state.add(floatArrayOf(0f, Float.POSITIVE_INFINITY, G), still, now)
        assertFalse(state.snapshot().fresh(now))
        assertEquals(epoch + 2, state.snapshot().epoch)
    }

    @Test fun recenterChangesEpochWithoutDiscardingTheAtomicSample() {
        state.add(floatArrayOf(G, 0f, 0f), still, now)
        state.flush()
        val before = state.snapshot()
        state.recenter()
        val after = state.snapshot()
        assertEquals(before.rotation, after.rotation)
        assertEquals(before.timeNs, after.timeNs)
        assertEquals(before.epoch + 1, after.epoch)
        assertEquals(before.recenterSequence + 1, after.recenterSequence)
        state.reset()
        assertEquals(after.epoch + 1, state.snapshot().epoch)
        assertEquals(after.recenterSequence, state.snapshot().recenterSequence)
        assertFalse(state.snapshot().fresh(now))
    }

    @Test fun savedAccelCalibrationIsUsedBeforeGravityAlignmentAndSurvivesCallbackGap() {
        val scale = 1.04f
        state.reset(floatArrayOf(.4f, -.2f, .3f, scale))
        val raw = floatArrayOf(-.3f, -.4f, -.2f + G / scale)
        state.add(raw, still, now)
        state.flush()
        assertRotation(Quaternion.IDENTITY, state.snapshot().rotation)
        now += 200_000_000L
        state.add(raw, still, now)
        state.flush()
        assertRotation(Quaternion.IDENTITY, state.snapshot().rotation)
        assertTrue(state.snapshot().fresh(now))
    }

    @Test fun diagnosticsIdentifyResetsAndGapsWithoutLoggingEachSample() {
        val events = mutableListOf<String>()
        val motion = XrJoyConMotionState(events::add)
        motion.reset(reason = "device_changed")
        repeat(10) {
            motion.add(faceUp, still, now)
            motion.flush()
            now += 10_000_000L
        }
        assertEquals(1, events.size)
        assertTrue(events.single().contains("reset reason=device_changed epoch=1 manual=0 resets=1 gaps=0 samples=0"))
        motion.recenter()
        now += 150_000_000L
        motion.add(faceUp, still, now)
        motion.flush()
        assertEquals(3, events.size)
        assertTrue(events.last().contains("sample_gap durationNs=160000000 epoch=2 manual=1 resets=1 gaps=1 samples=10"))
        motion.add(faceUp, floatArrayOf(Float.NaN, 0f, 0f), now)
        assertTrue(events.last().contains("reset reason=invalid_sample epoch=3 manual=1 resets=2 gaps=1 samples=11"))
    }

    @Test fun equivalentDeviceAndSensorRefreshPreservesHeadingBiasAndManualSequence() {
        val fixture = Collector()
        try {
            fixture.report(.02f)
            repeat(2000) { now += 10_000_000L; fixture.report(.02f) }
            repeat(100) { now += 10_000_000L; fixture.report(1.02f) }
            fixture.motion.recenter(JoyCon.LEFT)
            val before = fixture.motion.snapshot(JoyCon.LEFT)
            assertTrue(before.rotation.rotate(Vec3(0f, 0f, -1f)).x < -.8f)
            val oldListener = fixture.shadow.listeners.single()
            fixture.device = device(101, "left", JoyCon.LEFT)
            fixture.sensors = fixture.sensors()
            fixture.connect()
            assertEquals(before, fixture.motion.snapshot(JoyCon.LEFT))
            assertFalse(fixture.shadow.hasListener(oldListener))
            assertEquals(1, fixture.shadow.listeners.size)
            now += 10_000_000L
            fixture.report(.02f)
            repeat(100) { now += 10_000_000L; fixture.report(.02f) }
            val after = fixture.motion.snapshot(JoyCon.LEFT)
            assertRotation(before.rotation, after.rotation)
            assertEquals(before.epoch, after.epoch)
            assertEquals(before.recenterSequence, after.recenterSequence)
            assertEquals(0f, after.angularSpeed, 2e-6f)
            assertTrue(after.fresh(now))
        } finally { fixture.motion.close() }
    }

    @Test fun descriptorIdAndSideChangesResetOnlyTheAffectedMotion() {
        val fixture = Collector()
        try {
            fixture.report()
            fixture.motion.recenter(JoyCon.LEFT)
            var epoch = fixture.motion.snapshot(JoyCon.LEFT).epoch
            val rightBefore = fixture.motion.snapshot(JoyCon.RIGHT)
            fixture.device = device(101, "replacement-left", JoyCon.LEFT)
            fixture.connect()
            assertEquals(++epoch, fixture.motion.snapshot(JoyCon.LEFT).epoch)
            assertEquals(1, fixture.motion.snapshot(JoyCon.LEFT).recenterSequence)
            assertFalse(fixture.motion.snapshot(JoyCon.LEFT).fresh(now))
            assertEquals(rightBefore, fixture.motion.snapshot(JoyCon.RIGHT))
            fixture.report()
            fixture.device = device(102, "replacement-left", JoyCon.LEFT)
            fixture.connect()
            assertEquals(++epoch, fixture.motion.snapshot(JoyCon.LEFT).epoch)
            assertFalse(fixture.motion.snapshot(JoyCon.LEFT).fresh(now))
            fixture.device = device(102, "replacement-left", JoyCon.RIGHT)
            fixture.motion.devices(arrayOf(null, fixture.device))
            assertEquals(++epoch, fixture.motion.snapshot(JoyCon.LEFT).epoch)
            assertFalse(fixture.motion.snapshot(JoyCon.LEFT).fresh(now))
            assertEquals(rightBefore.epoch + 1, fixture.motion.snapshot(JoyCon.RIGHT).epoch)
            assertEquals(0, fixture.motion.snapshot(JoyCon.RIGHT).recenterSequence)
        } finally { fixture.motion.close() }
    }

    @Test fun changedGyroOrAccelIdentityResetsFusionButNotManualSequence() {
        val fixture = Collector()
        try {
            fixture.report()
            fixture.motion.recenter(JoyCon.LEFT)
            var epoch = fixture.motion.snapshot(JoyCon.LEFT).epoch
            fixture.sensors = fixture.sensors(gyroName = "replacement gyro")
            fixture.connect()
            assertEquals(++epoch, fixture.motion.snapshot(JoyCon.LEFT).epoch)
            assertFalse(fixture.motion.snapshot(JoyCon.LEFT).fresh(now))
            assertEquals(1, fixture.motion.snapshot(JoyCon.LEFT).recenterSequence)
            fixture.report()
            fixture.sensors = fixture.sensors(accelName = "replacement accel", gyroName = "replacement gyro")
            fixture.connect()
            assertEquals(++epoch, fixture.motion.snapshot(JoyCon.LEFT).epoch)
            assertFalse(fixture.motion.snapshot(JoyCon.LEFT).fresh(now))
            assertEquals(1, fixture.motion.snapshot(JoyCon.LEFT).recenterSequence)
        } finally { fixture.motion.close() }
    }

    @Test fun sensorDiscoveryRetryAndIdenticalUpdateDoNotResetAnActiveListener() {
        val fixture = Collector()
        try {
            fixture.report()
            fixture.motion.recenter(JoyCon.LEFT)
            val before = fixture.motion.snapshot(JoyCon.LEFT)
            val listener = fixture.shadow.listeners.single()
            fixture.connect()
            assertSame(listener, fixture.shadow.listeners.single())
            fixture.available = false
            fixture.connect()
            assertEquals(before, fixture.motion.snapshot(JoyCon.LEFT))
            assertSame(listener, fixture.shadow.listeners.single())
            fixture.available = true
            fixture.connect()
            assertEquals(before, fixture.motion.snapshot(JoyCon.LEFT))
            assertSame(listener, fixture.shadow.listeners.single())
        } finally { fixture.motion.close() }
    }

    @Test fun removedListenerCannotPublishLateCallbacksAfterDeviceReset() {
        val fixture = Collector()
        try {
            fixture.report()
            val listener = fixture.shadow.listeners.single()
            fixture.motion.devices(arrayOfNulls(2))
            val stopped = fixture.motion.snapshot(JoyCon.LEFT)
            assertFalse(stopped.fresh(now))
            assertTrue(fixture.shadow.listeners.isEmpty())
            repeat(3) {
                listener.onSensorChanged(SensorEventBuilder.newBuilder().setSensor(fixture.sensors.accel)
                    .setValues(faceUp).setTimestamp(++fixture.sensorTimestamp).build())
                listener.onSensorChanged(SensorEventBuilder.newBuilder().setSensor(fixture.sensors.gyro)
                    .setValues(floatArrayOf(0f, 0f, 5f)).setTimestamp(++fixture.sensorTimestamp).build())
            }
            assertEquals(stopped, fixture.motion.snapshot(JoyCon.LEFT))
        } finally { fixture.motion.close() }
    }

    private inner class Collector {
        private val context = RuntimeEnvironment.getApplication()
        private val manager = context.getSystemService(SensorManager::class.java)
        val shadow = shadowOf(manager)
        var device = device(101, "left", JoyCon.LEFT)
        var sensors = sensors()
        var available = true
        var sensorTimestamp = 0L
        val motion = XrJoyConMotion(context, { now }, { if (available) sensors else null })

        init { connect() }

        fun sensors(accelName: String = "accel", gyroName: String = "gyro") = XrJoyConSensors(manager,
            ShadowSensor.newInstance(Sensor.TYPE_ACCELEROMETER).also { shadowOf(it).setName(accelName) },
            ShadowSensor.newInstance(Sensor.TYPE_GYROSCOPE).also { shadowOf(it).setName(gyroName) })

        fun connect() = motion.devices(arrayOf(device, null))

        fun report(rate: Float = 0f) {
            repeat(3) {
                shadow.sendSensorEventToListeners(SensorEventBuilder.newBuilder().setSensor(sensors.accel)
                    .setValues(faceUp).setTimestamp(++sensorTimestamp).build(), sensors.accel)
                shadow.sendSensorEventToListeners(SensorEventBuilder.newBuilder().setSensor(sensors.gyro)
                    .setValues(floatArrayOf(0f, 0f, rate)).setTimestamp(++sensorTimestamp).build(), sensors.gyro)
            }
        }
    }

    private fun device(id: Int, descriptor: String, side: Int): InputDevice = InputDeviceBuilder.newBuilder()
        .setId(id).setName("Joy-Con $id").setDescriptor(descriptor).setExternal(true)
        .setVendorId(0x057e).setProductId(if (side == JoyCon.LEFT) 0x2006 else 0x2007)
        .setSources(InputDevice.SOURCE_GAMEPAD).build()

    private fun assertRotation(expected: Quaternion, actual: Quaternion) {
        assertEquals(expected.w, actual.w, 1e-5f)
        assertEquals(expected.x, actual.x, 1e-5f)
        assertEquals(expected.y, actual.y, 1e-5f)
        assertEquals(expected.z, actual.z, 1e-5f)
    }

    private companion object { const val G = 9.80665f }
}
