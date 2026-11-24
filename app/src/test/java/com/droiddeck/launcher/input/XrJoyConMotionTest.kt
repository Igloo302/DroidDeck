package com.droiddeck.launcher.input

import com.droiddeck.launcher.glasses.Quaternion
import com.droiddeck.launcher.glasses.Vec3
import kotlin.math.cos
import kotlin.math.sin
import org.junit.Assert.*
import org.junit.Test

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
        assertTrue(sample.fresh(now))
    }

    @Test fun sensorGapDiscardsOldPendingReportsAndStartsNewFilterEpoch() {
        state.add(faceUp, still, now)
        state.flush()
        val epoch = state.snapshot().epoch
        now += 10_000_000L
        state.add(faceUp, floatArrayOf(0f, 0f, 9f), now)
        now += 100_000_001L
        state.add(faceUp, still, now)
        assertFalse(state.snapshot().fresh(now))
        state.flush()
        assertEquals(epoch + 1, state.snapshot().epoch)
        assertRotation(Quaternion.IDENTITY, state.snapshot().rotation)
        assertEquals(now, state.snapshot().timeNs)
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
        state.reset()
        assertEquals(after.epoch + 1, state.snapshot().epoch)
        assertFalse(state.snapshot().fresh(now))
    }

    @Test fun savedAccelCalibrationIsUsedBeforeGravityAlignmentAndSurvivesGapReset() {
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

    private fun assertRotation(expected: Quaternion, actual: Quaternion) {
        assertEquals(expected.w, actual.w, 1e-5f)
        assertEquals(expected.x, actual.x, 1e-5f)
        assertEquals(expected.y, actual.y, 1e-5f)
        assertEquals(expected.z, actual.z, 1e-5f)
    }

    private companion object { const val G = 9.80665f }
}
