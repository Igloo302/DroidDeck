package com.droiddeck.launcher.xr

import android.app.Application
import android.app.Presentation
import android.graphics.SurfaceTexture
import android.hardware.display.DisplayManager
import android.os.Looper
import android.util.DisplayMetrics
import android.view.Display
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.compose.ui.platform.ComposeView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.findViewTreeLifecycleOwner
import androidx.savedstate.findViewTreeSavedStateRegistryOwner
import com.droiddeck.launcher.ui.KeyWindows
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.android.controller.ActivityController
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode
import org.robolectric.util.ReflectionHelpers

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], application = Application::class)
@LooperMode(LooperMode.Mode.PAUSED)
class AuraXrMenuPresentationTest {
    private lateinit var activity: ActivityController<ComponentActivity>
    private lateinit var texture: SurfaceTexture
    private lateinit var surface: Surface
    private var menu: AuraXrMenuPresentation? = null
    private var closes = 0
    private var virtualDisplayFlags = 0
    private val keys = mutableListOf<Int>()
    private val touches = mutableListOf<Triple<Int, Float, Float>>()

    @Before fun prepare() {
        activity = Robolectric.buildActivity(ComponentActivity::class.java).setup().visible()
        texture = SurfaceTexture(0)
        surface = Surface(texture)
    }

    @After fun clean() {
        menu?.let(::acknowledgeDetach)
        menu?.close()
        surface.release()
        texture.release()
        activity.pause().stop().destroy()
    }

    private fun open(): AuraXrMenuPresentation {
        return AuraXrMenuPresentation.create(activity.get(), surface, 1280, 960,
            content = { context -> ComposeView(context).apply {
                isFocusable = true
                isFocusableInTouchMode = true
                setOnTouchListener { _, event ->
                    touches += Triple(event.actionMasked, event.x, event.y)
                    true
                }
                setOnGenericMotionListener { _, event ->
                    touches += Triple(event.actionMasked, event.x, event.y)
                    true
                }
                setOnKeyListener { _, code, _ -> keys += code; true }
            } },
            onKey = { menu!!.dispatchWindowKey(it) }, onMotion = { true },
            onBack = { closes++ }, onClosed = { closes++ },
        ).also {
            menu = it
            // Robolectric copies creation flags directly instead of mapping VirtualDisplay flags to Display flags.
            virtualDisplayFlags = it.display.flags
            shadowOf(it.display).setFlags(Display.FLAG_PRIVATE)
            it.show()
            shadowOf(Looper.getMainLooper()).idle()
            val decor = it.window!!.decorView
            decor.measure(View.MeasureSpec.makeMeasureSpec(1280, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(960, View.MeasureSpec.EXACTLY))
            decor.layout(0, 0, 1280, 960)
            it.focus()
        }
    }

    private fun acknowledgeDetach(shown: AuraXrMenuPresentation) {
        // ShadowDisplayManagerGlobal does not send virtual-display pause callbacks.
        val callbacks = ReflectionHelpers.getField<MenuDisplayCallbacks>(shown, "displayCallbacks")
        callbacks.beginDetach()
        callbacks.onPaused()
    }

    @Test fun menuKeepsItsOwnResumedLifecycleWhenActivityStops() {
        val shown = open()
        activity.pause().stop()
        val owner = shown.window!!.decorView.findViewTreeLifecycleOwner()!!
        assertEquals(Lifecycle.State.CREATED, activity.get().lifecycle.currentState)
        assertEquals(Lifecycle.State.RESUMED, owner.lifecycle.currentState)
        assertNotSame(activity.get(), owner)
        assertNotNull(shown.window!!.decorView.findViewTreeSavedStateRegistryOwner())
        acknowledgeDetach(shown)
        shown.close()
        assertEquals(Lifecycle.State.DESTROYED, owner.lifecycle.currentState)
        activity.restart().start().resume()
    }

    @Test fun privateMenuDisplayCannotBeSelectedAsAnOrdinaryPresentationDisplay() {
        val shown = open()
        assertNotEquals(0, virtualDisplayFlags and DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY)
        assertEquals(0, virtualDisplayFlags and DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION)
        assertEquals(0, virtualDisplayFlags and DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC)
        assertEquals(0, shown.display.flags and Display.FLAG_PRESENTATION)
        assertNotEquals(0, shown.display.flags and Display.FLAG_PRIVATE)
        assertFalse(shown.display.name.contains("AURA", ignoreCase = true))
        val metrics = DisplayMetrics()
        shown.display.getMetrics(metrics)
        assertEquals(240, metrics.densityDpi)
        assertNotEquals(0, shown.window!!.attributes.flags and WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
    }

    @Test fun pointerHoverPressDragAndReleaseUsePanelCoordinates() {
        val shown = open()
        assertTrue(shown.dispatchPointer(MotionEvent.ACTION_HOVER_MOVE, 0.25f, 0.5f))
        assertTrue(shown.dispatchPointer(MotionEvent.ACTION_DOWN, 0.25f, 0.5f))
        assertTrue(shown.dispatchPointer(MotionEvent.ACTION_MOVE, 0.5f, 0.75f))
        assertTrue(shown.dispatchPointer(MotionEvent.ACTION_UP, 0.5f, 0.75f))
        assertEquals(listOf(MotionEvent.ACTION_HOVER_ENTER, MotionEvent.ACTION_HOVER_MOVE,
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP), touches.map { it.first })
        assertEquals(1279f / 4, touches.first().second, 0.01f)
        assertEquals(959f / 2, touches.first().third, 0.01f)
        assertFalse(shown.dispatchPointer(MotionEvent.ACTION_DOWN, Float.NaN, 0.5f))
    }

    @Test fun nestedWindowGetsKeysAndPointerBeforeTheMenuRoot() {
        val shown = open()
        val nestedKeys = mutableListOf<Int>()
        val nestedTouches = mutableListOf<Int>()
        val popup = Presentation(activity.get(), shown.display)
        val content = View(popup.context).apply {
            isFocusable = true
            isFocusableInTouchMode = true
            setOnKeyListener { _, code, _ -> nestedKeys += code; true }
            setOnTouchListener { _, event -> nestedTouches += event.actionMasked; true }
        }
        popup.setContentView(content)
        popup.show()
        shadowOf(Looper.getMainLooper()).idle()
        popup.window!!.decorView.layout(0, 0, 1280, 960)
        content.layout(0, 0, 1280, 960)
        content.requestFocus()
        KeyWindows.add(content)
        try {
            shown.dispatchKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_DOWN))
            shown.dispatchPointer(MotionEvent.ACTION_DOWN, 0.5f, 0.5f)
            shown.dispatchPointer(MotionEvent.ACTION_CANCEL, 0.5f, 0.5f)
            assertEquals(listOf(KeyEvent.KEYCODE_DPAD_DOWN), nestedKeys)
            assertEquals(listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_CANCEL), nestedTouches)
            assertTrue(keys.isEmpty())
            assertTrue(touches.isEmpty())
        } finally { KeyWindows.remove(content); popup.dismiss() }
    }

    @Test fun detachThenCloseIsIdempotentAndRemovesTheDisplay() {
        val shown = open()
        val id = shown.display.displayId
        assertFalse(shown.detachSurface())
        acknowledgeDetach(shown)
        assertTrue(shown.detachSurface())
        assertTrue(shown.detachSurface())
        assertTrue(shown.isShowing)
        shown.close()
        shown.close()
        shown.detachSurface()
        shadowOf(Looper.getMainLooper()).idle()
        assertEquals(1, closes)
        assertNull(activity.get().getSystemService(DisplayManager::class.java).getDisplay(id))
        assertFalse(shown.dispatchKey(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A)))
        assertFalse(shown.dispatchPointer(MotionEvent.ACTION_DOWN, 0f, 0f))
    }

    @Test fun ownBackDispatcherRespectsNestedLifecycleCallbacks() {
        var fallback = 0
        var nested = 0
        val owner = MenuOwner { fallback++ }
        val callback = object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() { nested++ }
        }
        owner.onBackPressedDispatcher.addCallback(owner, callback)
        owner.resume()
        owner.onBackPressedDispatcher.onBackPressed()
        assertEquals(1, nested)
        assertEquals(0, fallback)
        callback.isEnabled = false
        owner.onBackPressedDispatcher.onBackPressed()
        assertEquals(1, fallback)
        owner.destroy()
    }

    @Test fun producerAcknowledgmentMustFollowDetachAndCanArriveOffTheUiThread() {
        val callbacks = MenuDisplayCallbacks()
        callbacks.onPaused()
        assertFalse(callbacks.awaitPaused(0))
        callbacks.beginDetach()
        assertFalse(callbacks.awaitPaused(0))
        val producer = Thread { callbacks.onPaused() }.apply { start() }
        assertTrue(callbacks.awaitPaused(500))
        producer.join()
        assertTrue(callbacks.awaitPaused(0))
    }

    @Test fun stoppedProducerAlsoAcknowledgesQuiescence() {
        val callbacks = MenuDisplayCallbacks()
        callbacks.onStopped()
        callbacks.beginDetach()
        assertTrue(callbacks.awaitPaused(0))
    }
}
