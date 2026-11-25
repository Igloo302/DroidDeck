package com.droiddeck.launcher.input

import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import android.view.MotionEvent
import com.droiddeck.launcher.glasses.Quaternion
import java.nio.file.Files
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.InputDeviceBuilder

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], manifest = Config.NONE, instrumentedPackages = ["com.droiddeck.launcher.input.FakeInputWriter"],
    shadows = [PadBridgeRoutingTest.RegisteredKeyEvent::class, PadBridgeRoutingTest.RegisteredMotionEvent::class,
        JoyConPadTest.RecordingWriter::class])
class XrGamepadInputTest {
    private val dir = Files.createTempDirectory("xr-pad").toFile()
    private lateinit var bridge: PadBridge
    private lateinit var input: XrGamepadInput
    private lateinit var device: InputDevice
    private var now = 1_000_000_000L
    private val orientation = FakeMotion()

    @Before fun setup() {
        JoyConPadTest.RecordingWriter.pads.clear()
        val app = RuntimeEnvironment.getApplication()
        val manager = app.getSystemService(InputManager::class.java)
        device = InputDeviceBuilder.newBuilder().setId(91).setName("Xbox").setDescriptor("xr-xbox")
            .setExternal(true).setVendorId(0x045e).setProductId(0x0b13)
            .setSources(InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK)
            .setKeyboardType(InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC)
            .setKeyCharacterMap(KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD))
            .addMotionRange(MotionEvent.AXIS_X, InputDevice.SOURCE_JOYSTICK, -1f, 1f, 0f, 0f, 0f)
            .addMotionRange(MotionEvent.AXIS_Y, InputDevice.SOURCE_JOYSTICK, -1f, 1f, 0f, 0f, 0f).build()
        shadowOf(manager).addInputDevice(device)
        bridge = PadBridge(dir)
        input = XrGamepadInput(app, { now }, orientation) { bridge }
        input.resume()
        input.focus(true)
    }

    @After fun cleanup() { input.close(); bridge.stop(); dir.deleteRecursively() }

    private fun key(code: Int, action: Int = KeyEvent.ACTION_DOWN, repeat: Int = 0) =
        input.key(KeyEvent(1L, 2L, action, code, repeat, 0, device.id, 0, 0, InputDevice.SOURCE_GAMEPAD))
    private fun pad() = JoyConPadTest.RecordingWriter.pads[0] ?: PadState()

    @Test fun buttonsAndNativeAxesReachExistingPadBridge() {
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A)); assertTrue(pad().isDown(PadState.A))
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A, KeyEvent.ACTION_UP)); assertFalse(pad().isDown(PadState.A))
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_B)); assertTrue(pad().isDown(PadState.B))
        assertTrue(input.nativeMotion(device.id, InputDevice.SOURCE_JOYSTICK, MotionEvent.ACTION_MOVE,
            1L, 2L, floatArrayOf(.75f, -.5f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f)))
        assertEquals(.75f, pad().leftX); assertEquals(-.5f, pad().leftY)
    }

    @Test fun menuChordReleasesGuestKeysAndFiresOnceUntilBothReleased() {
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_SELECT)); assertTrue(pad().isDown(PadState.SELECT))
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_START)); assertEquals(1, input.menuSequence)
        assertFalse(pad().isDown(PadState.SELECT)); assertFalse(pad().isDown(PadState.START))
        key(KeyEvent.KEYCODE_BUTTON_START, repeat = 1); assertEquals(1, input.menuSequence)
        key(KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.ACTION_UP)
        key(KeyEvent.KEYCODE_BUTTON_SELECT); assertEquals(1, input.menuSequence)
        key(KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.ACTION_UP)
        key(KeyEvent.KEYCODE_BUTTON_START, KeyEvent.ACTION_UP)
        key(KeyEvent.KEYCODE_BUTTON_START); assertTrue(pad().isDown(PadState.START))
        key(KeyEvent.KEYCODE_BUTTON_SELECT); assertEquals(2, input.menuSequence)
    }

    @Test fun guideDoesNotAlsoReachSteamClient() {
        key(KeyEvent.KEYCODE_BUTTON_MODE); assertEquals(1, input.menuSequence)
        assertFalse(pad().isDown(PadState.GUIDE))
        key(KeyEvent.KEYCODE_BUTTON_MODE, repeat = 1); assertEquals(1, input.menuSequence)
        key(KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.ACTION_UP)
        key(KeyEvent.KEYCODE_BUTTON_MODE); assertEquals(2, input.menuSequence)
    }

    @Test fun focusLossPauseAndDisconnectReleaseHeldInput() {
        key(KeyEvent.KEYCODE_BUTTON_A); input.focus(false); assertFalse(pad().isDown(PadState.A))
        assertTrue(key(KeyEvent.KEYCODE_BACK)); assertFalse(pad().isDown(PadState.SELECT))
        key(KeyEvent.KEYCODE_BUTTON_MODE); assertEquals(0, input.menuSequence)
        input.focus(true); key(KeyEvent.KEYCODE_BUTTON_B); input.pause(); assertFalse(pad().isDown(PadState.B))
        key(KeyEvent.KEYCODE_BUTTON_A); assertFalse(pad().isDown(PadState.A))
        input.resume(); key(KeyEvent.KEYCODE_BUTTON_A); input.onInputDeviceRemoved(device.id)
        assertFalse(pad().isDown(PadState.A))
    }

    @Test fun virtualKeysAreRejected() {
        assertFalse(input.key(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A)))
        assertEquals(0, input.menuSequence)
    }

    @Test fun genericHeadsetAxesAreFilteredOnlyInAndroidXr() {
        val headset = InputDeviceBuilder.newBuilder().setId(92).setName("Headset input")
            .setSources(InputDevice.SOURCE_JOYSTICK or InputDevice.SOURCE_KEYBOARD)
            .setKeyboardType(InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC)
            .addMotionRange(MotionEvent.AXIS_GENERIC_1, InputDevice.SOURCE_JOYSTICK, -1f, 1f, 0f, 0f, 0f).build()
        assertTrue(PadBridge.isFromController(headset))
        assertFalse(XrGamepadInput.isController(headset))
        assertTrue(XrGamepadInput.isController(device))
    }

    @Test fun differentControllersCannotCombineMenuKeys() {
        val chord = XrGamepadMenu()
        assertFalse(chord.key(1, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.ACTION_DOWN, 0).request)
        assertFalse(chord.key(2, KeyEvent.KEYCODE_BUTTON_START, KeyEvent.ACTION_DOWN, 0).request)
        chord.remove(1)
        assertFalse(chord.key(1, KeyEvent.KEYCODE_BUTTON_START, KeyEvent.ACTION_DOWN, 0).request)
        chord.clear()
        assertFalse(chord.key(1, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.ACTION_DOWN, 0).request)
    }

    @Test fun sensorlessJoyConsOwnControlsWithoutDuplicateGamepadOrMenuEvents() {
        input.pause()
        val left = joyCon(101, JoyCon.LEFT, MotionEvent.AXIS_X, MotionEvent.AXIS_Y)
        val right = joyCon(102, JoyCon.RIGHT, MotionEvent.AXIS_RX, MotionEvent.AXIS_RY)
        bridge.syncConnectedControllers()
        assertEquals(left.id, PadBridge.joyConForSlot(1, JoyCon.LEFT))
        assertEquals(right.id, PadBridge.joyConForSlot(1, JoyCon.RIGHT))
        input.resume()
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, CONNECTED, 0, 0), controls())
        bridge.syncConnectedControllers()
        for (slot in 0 until PadBridge.SLOTS) {
            assertEquals(PadBridge.NO_CONTROLLER, PadBridge.joyConForSlot(slot, JoyCon.LEFT))
            assertEquals(PadBridge.NO_CONTROLLER, PadBridge.joyConForSlot(slot, JoyCon.RIGHT))
        }
        val writes = JoyConPadTest.RecordingWriter.padWrites
        assertTrue(joyKey(left, 0x138))
        assertTrue(joyKey(right, 0x130))
        assertTrue(joyKey(right, 0x13c, code = KeyEvent.KEYCODE_BUTTON_MODE))
        assertTrue(joyKey(right, 0x13c, action = KeyEvent.ACTION_UP, code = KeyEvent.KEYCODE_BUTTON_MODE))
        assertArrayEquals(intArrayOf(CONNECTED or JoyCon.VR_TRIGGER, 0, 0,
            CONNECTED or JoyCon.VR_A or JoyCon.VR_SYSTEM, 0, 0), controls())
        assertEquals(writes, JoyConPadTest.RecordingWriter.padWrites)
        assertEquals(0, input.menuSequence)
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A))
        assertTrue(pad().isDown(PadState.A))
        input.pause()
        bridge.syncConnectedControllers()
        assertEquals(left.id, PadBridge.joyConForSlot(1, JoyCon.LEFT))
        assertEquals(right.id, PadBridge.joyConForSlot(1, JoyCon.RIGHT))
    }

    @Test fun retiringXrOwnerCannotReleaseReplacementJoyConExclusion() {
        val left = joyCon(101, JoyCon.LEFT)
        val right = joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(left.id)
        val replacement = XrGamepadInput(RuntimeEnvironment.getApplication(), { now }, FakeMotion()) { bridge }
        try {
            replacement.resume()
            replacement.focus(true)
            input.pause()
            bridge.syncConnectedControllers()
            for (slot in 0 until PadBridge.SLOTS) {
                assertNotEquals(left.id, PadBridge.controllerForSlot(slot))
                assertNotEquals(right.id, PadBridge.controllerForSlot(slot))
            }
            assertArrayEquals(intArrayOf(CONNECTED, 0, 0, CONNECTED, 0, 0), controls(replacement))
            replacement.pause()
            bridge.syncConnectedControllers()
            assertEquals(left.id, PadBridge.joyConForSlot(1, JoyCon.LEFT))
            assertEquals(right.id, PadBridge.joyConForSlot(1, JoyCon.RIGHT))
        } finally { replacement.close() }
    }

    @Test fun xrJoyConOwnershipPreservesTheExistingDeviceExclusion() {
        val left = joyCon(101, JoyCon.LEFT)
        val right = joyCon(102, JoyCon.RIGHT)
        bridge.setExcluded { it == left.id }
        input.onInputDeviceAdded(left.id)
        bridge.syncConnectedControllers()
        assertFalse(bridge.onKeyEvent(KeyEvent(1L, 2L, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A,
            0, 0, right.id, 0x130, 0, InputDevice.SOURCE_GAMEPAD)))
        input.pause()
        bridge.syncConnectedControllers()
        assertTrue(bridge.onKeyEvent(KeyEvent(1L, 2L, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A,
            0, 0, right.id, 0x130, 0, InputDevice.SOURCE_GAMEPAD)))
        assertEquals(right.id, PadBridge.controllerForSlot(1))
        for (slot in 0 until PadBridge.SLOTS) assertNotEquals(left.id, PadBridge.controllerForSlot(slot))
        assertFalse(bridge.onKeyEvent(KeyEvent(1L, 2L, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_L2,
            0, 0, left.id, 0x138, 0, InputDevice.SOURCE_GAMEPAD)))
    }

    @Test fun aliasedJoyConButtonsStayDownUntilEveryPhysicalButtonIsReleased() {
        val left = joyCon(101, JoyCon.LEFT)
        input.onInputDeviceAdded(left.id)
        joyKey(left, 0x136)
        joyKey(left, 0x137)
        joyKey(left, 0x136, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED or JoyCon.VR_GRIP, controls()[0])
        joyKey(left, 0x137, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED, controls()[0])
        joyKey(left, 0x220)
        joyKey(left, 0x13a)
        joyKey(left, 0x220, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED or JoyCon.VR_B, controls()[0])
        joyKey(left, 0x13a, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED, controls()[0])
    }

    @Test fun joyConFocusPauseDisconnectAndReconnectClearControls() {
        val left = joyCon(101, JoyCon.LEFT, MotionEvent.AXIS_X, MotionEvent.AXIS_Y)
        input.onInputDeviceAdded(left.id)
        joyKey(left, 0x138)
        joyMotion(left, MotionEvent.AXIS_X to 1f)
        input.focus(false)
        assertArrayEquals(IntArray(6), controls())
        assertTrue(joyKey(left, 0x138))
        assertTrue(joyMotion(left, MotionEvent.AXIS_X to -1f))
        input.focus(true)
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, 0, 0, 0), controls())
        joyKey(left, 0x138, repeat = 1)
        assertEquals(CONNECTED, controls()[0])
        joyKey(left, 0x138)
        joyMotion(left, MotionEvent.AXIS_Y to -1f)
        input.pause()
        assertArrayEquals(IntArray(6), controls())
        joyKey(left, 0x138)
        input.resume()
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, 0, 0, 0), controls())
        joyKey(left, 0x138)
        joyMotion(left, MotionEvent.AXIS_X to 1f)
        input.onInputDeviceRemoved(left.id)
        assertArrayEquals(IntArray(6), controls())
        input.onInputDeviceAdded(left.id)
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, 0, 0, 0), controls())
        joyKey(left, 0x138)
        input.onInputDeviceChanged(left.id)
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, 0, 0, 0), controls())
    }

    @Test fun joyConSnapshotKeepsConnectionsWithoutInputEventsAndCannotBeMutatedByReader() {
        val left = joyCon(101, JoyCon.LEFT)
        input.onInputDeviceAdded(left.id)
        val first = input.joyConState()
        first[0] = 0
        repeat(100) {
            assertArrayEquals(intArrayOf(CONNECTED, 0, 0, 0, 0, 0), controls())
        }
    }

    @Test fun nativeJoyConAxesUseLeftXyAndRightRxRyWithUpPositive() {
        val left = joyCon(101, JoyCon.LEFT, MotionEvent.AXIS_X, MotionEvent.AXIS_Y)
        val right = joyCon(102, JoyCon.RIGHT, MotionEvent.AXIS_RX, MotionEvent.AXIS_RY)
        input.onInputDeviceAdded(left.id)
        input.onInputDeviceAdded(right.id)
        assertTrue(input.nativeMotion(left.id, InputDevice.SOURCE_JOYSTICK, MotionEvent.ACTION_MOVE,
            1L, 2L, floatArrayOf(1f, -1f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, 0f)))
        assertTrue(input.nativeMotion(right.id, InputDevice.SOURCE_JOYSTICK, MotionEvent.ACTION_MOVE,
            1L, 2L, floatArrayOf(1f, -1f, 1f, -1f, -.5f, .5f, 0f, 0f, 0f, 0f, 0f, 0f)))
        assertArrayEquals(intArrayOf(CONNECTED, 23170, 23170, CONNECTED, -15458, -15458), controls())
        assertEquals(0f, pad().leftX)
        assertEquals(0f, pad().rightX)
    }

    @Test fun rightJoyConLegacyAxesUseZAndRzAndClampInvalidValues() {
        val right = joyCon(102, JoyCon.RIGHT, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ)
        input.onInputDeviceAdded(right.id)
        assertTrue(joyMotion(right, MotionEvent.AXIS_Z to -1f, MotionEvent.AXIS_RZ to -1f,
            MotionEvent.AXIS_RX to 1f, MotionEvent.AXIS_RY to 1f))
        assertArrayEquals(intArrayOf(0, 0, 0, CONNECTED, -23170, 23170), controls())
        joyMotion(right, MotionEvent.AXIS_Z to 2f, MotionEvent.AXIS_RZ to Float.NaN)
        assertArrayEquals(intArrayOf(0, 0, 0, CONNECTED, 32767, 0), controls())
    }

    @Test fun joyConRadialDeadzoneSuppressesDriftAndRescalesTravel() {
        val left = joyCon(101, JoyCon.LEFT, MotionEvent.AXIS_X, MotionEvent.AXIS_Y)
        input.onInputDeviceAdded(left.id)
        joyMotion(left, MotionEvent.AXIS_X to .08f, MotionEvent.AXIS_Y to -.08f)
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, 0, 0, 0), controls())
        joyMotion(left, MotionEvent.AXIS_X to .56f)
        assertArrayEquals(intArrayOf(CONNECTED, 16384, 0, 0, 0, 0), controls())
        joyMotion(left, MotionEvent.AXIS_X to 1f, MotionEvent.AXIS_Y to -1f)
        assertArrayEquals(intArrayOf(CONNECTED, 23170, 23170, 0, 0, 0), controls())
    }

    @Test fun joyConControlsDoNotDependOnAnAvailablePadBridge() {
        input.pause()
        input.close()
        input = XrGamepadInput(RuntimeEnvironment.getApplication(), { now }, orientation) { null }
        val left = joyCon(101, JoyCon.LEFT)
        input.resume()
        input.focus(true)
        assertTrue(joyKey(left, 0x138))
        assertEquals(CONNECTED or JoyCon.VR_TRIGGER, controls()[0])
    }

    @Test fun orientationSnapshotUsesXyzwBitsAndIndependentPerHandEpochs() {
        val left = joyCon(101, JoyCon.LEFT)
        joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(left.id)
        val q = Quaternion(.5f, -.5f, .5f, -.5f)
        orientation.samples[0] = XrJoyConOrientation(q, now, 12)
        orientation.samples[1] = XrJoyConOrientation(Quaternion.IDENTITY, now, 21)
        val state = input.joyConState()
        assertEquals(18, state.size)
        assertArrayEquals(intArrayOf(CONNECTED, 0, 0, q.x.toRawBits(), q.y.toRawBits(), q.z.toRawBits(),
            q.w.toRawBits(), 1, 12, CONNECTED, 0, 0, 0, 0, 0, 1f.toRawBits(), 1, 21), state)
        state[6] = 0
        assertEquals(q.w.toRawBits(), input.joyConState()[6])
    }

    @Test fun sensorSampleArrivingDuringSnapshotRemainsValidForBothHands() {
        val left = joyCon(101, JoyCon.LEFT)
        joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(left.id)
        orientation.beforeSnapshot = { side ->
            now += 1_000_000L
            orientation.samples[side] = XrJoyConOrientation(Quaternion.IDENTITY, now, side + 3)
        }
        val state = input.joyConState()
        assertEquals(1, state[7])
        assertEquals(1, state[16])
        assertEquals(3, state[8])
        assertEquals(4, state[17])
    }

    @Test fun staleAndFutureOrientationCannotDisableConnectedControls() {
        val left = joyCon(101, JoyCon.LEFT)
        input.onInputDeviceAdded(left.id)
        joyKey(left, 0x138)
        orientation.samples[0] = XrJoyConOrientation(Quaternion.IDENTITY, now, 7)
        now += XrJoyConOrientation.MAX_AGE_NS
        assertEquals(1, input.joyConState()[7])
        now++
        val stale = input.joyConState()
        assertEquals(CONNECTED or JoyCon.VR_TRIGGER, stale[0])
        assertArrayEquals(IntArray(5), stale.copyOfRange(3, 8))
        assertEquals(7, stale[8])
        orientation.samples[0] = XrJoyConOrientation(Quaternion.IDENTITY, now + 1, 8)
        assertEquals(0, input.joyConState()[7])
        assertEquals(CONNECTED or JoyCon.VR_TRIGGER, input.joyConState()[0])
    }

    @Test fun systemTapPulsesOnReleaseAndHoldRecentersOnceWithoutDashboardPulse() {
        val right = joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(right.id)
        val firstEpoch = input.joyConState()[17]
        joyKey(right, 0x13c)
        assertEquals(CONNECTED, input.joyConState()[9])
        now += 100_000_000L
        joyKey(right, 0x13c, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED or JoyCon.VR_SYSTEM, input.joyConState()[9])
        now += 120_000_000L
        assertEquals(CONNECTED, input.joyConState()[9])
        assertEquals(firstEpoch, input.joyConState()[17])
        joyKey(right, 0x13c)
        now += 799_000_000L
        joyKey(right, 0x13c, repeat = 1)
        assertEquals(firstEpoch, input.joyConState()[17])
        now += 1_000_000L
        assertEquals(firstEpoch + 1, input.joyConState()[17])
        now += 1_000_000_000L
        assertEquals(firstEpoch + 1, input.joyConState()[17])
        joyKey(right, 0x13c, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED, input.joyConState()[9])
        assertEquals(firstEpoch + 1, input.joyConState()[17])
        assertEquals(0, input.menuSequence)
    }

    @Test fun systemHoldIsRecognisedOnReleaseEvenWithoutSnapshotPolling() {
        val right = joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(right.id)
        val epoch = input.joyConState()[17]
        joyKey(right, 0x13c)
        now += 800_000_000L
        joyKey(right, 0x13c, KeyEvent.ACTION_UP)
        assertEquals(epoch + 1, input.joyConState()[17])
        assertEquals(CONNECTED, input.joyConState()[9])
    }

    @Test fun cancelledAndRepeatedSystemEventsDoNotProduceDashboardOrRecenter() {
        val right = joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(right.id)
        val epoch = input.joyConState()[17]
        joyKey(right, 0x13c, repeat = 1)
        now += 900_000_000L
        joyKey(right, 0x13c, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED, input.joyConState()[9])
        assertEquals(epoch, input.joyConState()[17])
        joyKey(right, 0x130)
        joyKey(right, 0x13c)
        assertTrue(input.key(KeyEvent(1L, 2L, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BUTTON_MODE,
            0, 0, right.id, 0x13c, KeyEvent.FLAG_CANCELED, InputDevice.SOURCE_GAMEPAD)))
        now += 900_000_000L
        assertEquals(CONNECTED or JoyCon.VR_A, input.joyConState()[9])
        assertEquals(epoch, input.joyConState()[17])
        assertEquals(0, input.menuSequence)
    }

    @Test fun focusLossCancelsSystemHoldAndPauseStopsMotionBeforeReconnect() {
        val right = joyCon(102, JoyCon.RIGHT)
        input.onInputDeviceAdded(right.id)
        val epoch = input.joyConState()[17]
        joyKey(right, 0x13c)
        input.focus(false)
        now += 900_000_000L
        assertArrayEquals(IntArray(18), input.joyConState())
        input.focus(true)
        joyKey(right, 0x13c, KeyEvent.ACTION_UP)
        assertEquals(CONNECTED, input.joyConState()[9])
        assertEquals(epoch, input.joyConState()[17])
        input.pause()
        assertNull(orientation.owned[1])
        assertArrayEquals(IntArray(18), input.joyConState())
        input.resume()
        assertEquals(right.id, orientation.owned[1])
        assertTrue(input.joyConState()[17] > epoch)
        val resumedEpoch = input.joyConState()[17]
        input.onInputDeviceChanged(right.id)
        assertTrue(input.joyConState()[17] > resumedEpoch)
        input.onInputDeviceRemoved(right.id)
        assertNull(orientation.owned[1])
        assertEquals(0, input.joyConState()[9])
        input.close()
        assertTrue(orientation.closed)
    }

    private fun controls(source: XrGamepadInput = input): IntArray {
        val state = source.joyConState()
        return intArrayOf(state[0], state[1], state[2], state[9], state[10], state[11])
    }

    private class FakeMotion : XrJoyConMotionSource {
        val samples = Array(2) { XrJoyConOrientation() }
        val owned = arrayOfNulls<Int>(2)
        var closed = false
        var beforeSnapshot: (Int) -> Unit = {}
        override fun devices(devices: Array<InputDevice?>) {
            for (side in owned.indices) if (owned[side] != devices[side]?.id) {
                owned[side] = devices[side]?.id
                samples[side] = XrJoyConOrientation(epoch = samples[side].epoch + 1)
            }
        }
        override fun snapshot(side: Int): XrJoyConOrientation {
            beforeSnapshot(side)
            return samples[side]
        }
        override fun recenter(side: Int) { samples[side] = samples[side].copy(epoch = samples[side].epoch + 1) }
        override fun close() { closed = true }
    }

    private fun joyCon(id: Int, side: Int, vararg axes: Int): InputDevice {
        val builder = InputDeviceBuilder.newBuilder().setId(id).setName("Joy-Con $id").setDescriptor("xr-joycon-$id")
            .setExternal(true).setVendorId(0x057e).setProductId(if (side == JoyCon.LEFT) 0x2006 else 0x2007)
            .setSources(InputDevice.SOURCE_GAMEPAD or InputDevice.SOURCE_JOYSTICK or InputDevice.SOURCE_KEYBOARD)
            .setKeyboardType(InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC)
            .setKeyCharacterMap(KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD))
        for (axis in axes) builder.addMotionRange(axis, InputDevice.SOURCE_JOYSTICK, -1f, 1f, 0f, 0f, 0f)
        return builder.build().also {
            shadowOf(RuntimeEnvironment.getApplication().getSystemService(InputManager::class.java)).addInputDevice(it)
        }
    }

    private fun joyKey(device: InputDevice, scan: Int, action: Int = KeyEvent.ACTION_DOWN,
                       repeat: Int = 0, code: Int = KeyEvent.KEYCODE_UNKNOWN): Boolean =
        input.key(KeyEvent(1L, 2L, action, code, repeat, 0, device.id, scan, 0, InputDevice.SOURCE_GAMEPAD))

    private fun joyMotion(device: InputDevice, vararg values: Pair<Int, Float>): Boolean {
        val coords = MotionEvent.PointerCoords().apply { for ((axis, value) in values) setAxisValue(axis, value) }
        val event = MotionEvent.obtain(1L, 2L, MotionEvent.ACTION_MOVE, 1,
            arrayOf(MotionEvent.PointerProperties().apply { id = 0 }), arrayOf(coords),
            0, 0, 1f, 1f, device.id, 0, InputDevice.SOURCE_JOYSTICK, 0)
        return try { input.motion(event) } finally { event.recycle() }
    }

    private companion object {
        const val CONNECTED = 1 shl 6
    }
}
