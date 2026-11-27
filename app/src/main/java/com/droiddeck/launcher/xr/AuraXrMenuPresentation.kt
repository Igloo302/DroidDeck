package com.droiddeck.launcher.xr

import android.app.Presentation
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.ColorFilter
import android.graphics.drawable.Drawable
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.activity.OnBackPressedDispatcher
import androidx.activity.OnBackPressedDispatcherOwner
import androidx.activity.setViewTreeOnBackPressedDispatcherOwner
import androidx.compose.ui.platform.ComposeView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.savedstate.SavedStateRegistry
import androidx.savedstate.SavedStateRegistryController
import androidx.savedstate.SavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import com.droiddeck.launcher.R
import com.droiddeck.launcher.ui.KeyWindows
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class AuraXrMenuPresentation private constructor(
    context: Context,
    private val output: VirtualDisplay,
    private val displayCallbacks: MenuDisplayCallbacks,
    private val width: Int,
    private val height: Int,
    private val content: (Context) -> ComposeView,
    private val onKey: (KeyEvent) -> Boolean,
    private val onMotion: (MotionEvent) -> Boolean,
    private val onBack: () -> Unit,
    private val onClosed: () -> Unit,
) : Presentation(context, output.display, R.style.SessionTheme) {
    private val outputLock = Any()
    private var released = false
    private var detached = false
    private var closed = false
    private val owner = MenuOwner(onBack)
    private var compose: ComposeView? = null
    private val cursor = MenuCursor()
    private var pointerDownTime = 0L
    private var pointerTarget: View? = null

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        window?.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window?.setBackgroundDrawableResource(android.R.color.transparent)
        window?.decorView?.let {
            it.setViewTreeLifecycleOwner(owner)
            it.setViewTreeSavedStateRegistryOwner(owner)
            it.setViewTreeViewModelStoreOwner(owner)
            it.setViewTreeOnBackPressedDispatcherOwner(owner)
        }
        val frame = FrameLayout(context)
        val view = content(context)
        compose = view
        frame.addView(view, FrameLayout.LayoutParams(-1, -1))
        setContentView(frame)
        setCancelable(false)
        setCanceledOnTouchOutside(false)
        setOnDismissListener { close() }
        owner.resume()
        view.requestFocus()
    }

    fun focus(): Boolean = compose?.requestFocus() == true
    fun dispatchKey(event: KeyEvent): Boolean = !closed && onKey(event)
    fun dispatchMotion(event: MotionEvent): Boolean = !closed && onMotion(event)

    internal fun dispatchWindowKey(event: KeyEvent): Boolean =
        !closed && topWindow()?.dispatchKeyEvent(event) == true

    internal fun back() { if (!closed) owner.onBackPressedDispatcher.onBackPressed() }

    private fun topWindow(): View? = KeyWindows.topOn(display.displayId)?.rootView ?: window?.decorView

    fun dispatchPointer(action: Int, x: Float, y: Float): Boolean {
        if (closed || !x.isFinite() || !y.isFinite()) return false
        val px = x.coerceIn(0f, 1f) * (width - 1)
        val py = y.coerceIn(0f, 1f) * (height - 1)
        val now = SystemClock.uptimeMillis()
        val actual = if (action == MotionEvent.ACTION_MOVE && pointerTarget == null) MotionEvent.ACTION_HOVER_MOVE else action
        if (actual == MotionEvent.ACTION_DOWN) {
            pointerDownTime = now
            pointerTarget = topWindow()
        }
        val target = pointerTarget ?: topWindow() ?: return false
        val location = IntArray(2).also(target::getLocationOnScreen)
        cursor.move(target, px - location[0], py - location[1], action != MotionEvent.ACTION_CANCEL)
        val event = MotionEvent.obtain(pointerDownTime.takeIf { it > 0 } ?: now, now, actual,
            px - location[0], py - location[1], 0).apply {
            source = if (actual == MotionEvent.ACTION_HOVER_MOVE) android.view.InputDevice.SOURCE_MOUSE
                else android.view.InputDevice.SOURCE_TOUCHSCREEN
        }
        return try {
            if (actual == MotionEvent.ACTION_HOVER_MOVE) target.dispatchGenericMotionEvent(event)
            else target.dispatchTouchEvent(event)
        } finally {
            event.recycle()
            if (actual == MotionEvent.ACTION_UP || actual == MotionEvent.ACTION_CANCEL) {
                pointerTarget = null
                pointerDownTime = 0
            }
        }
    }

    fun detachSurface(): Boolean {
        synchronized(outputLock) {
            if (!released && !detached) {
                detached = true
                displayCallbacks.beginDetach()
                output.surface = null
            }
        }
        return displayCallbacks.awaitPaused(500)
    }

    fun close() {
        check(Looper.myLooper() == Looper.getMainLooper())
        if (closed) return
        closed = true
        detachSurface()
        pointerTarget = null
        cursor.clear()
        owner.destroy()
        compose?.disposeComposition()
        compose = null
        dismiss()
        synchronized(outputLock) {
            if (!released) { released = true; output.release() }
        }
        onClosed()
    }

    companion object {
        private val callbackHandler by lazy {
            Handler(HandlerThread("xr-menu-display").apply { start() }.looper)
        }

        internal fun create(
            context: Context, surface: Surface, width: Int, height: Int,
            content: (Context) -> ComposeView,
            onKey: (KeyEvent) -> Boolean, onMotion: (MotionEvent) -> Boolean,
            onBack: () -> Unit, onClosed: () -> Unit,
        ): AuraXrMenuPresentation {
            check(Looper.myLooper() == Looper.getMainLooper())
            require(width > 0 && height > 0 && surface.isValid)
            val manager = context.getSystemService(DisplayManager::class.java)
            val callbacks = MenuDisplayCallbacks()
            val output = requireNotNull(manager.createVirtualDisplay("DroidDeck XR menu", width, height,
                240, surface, DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY, callbacks, callbackHandler))
            return try {
                AuraXrMenuPresentation(context, output, callbacks, width, height, content, onKey, onMotion, onBack, onClosed)
            } catch (error: Throwable) {
                output.release()
                throw error
            }
        }
    }
}

internal class MenuDisplayCallbacks : VirtualDisplay.Callback() {
    private val paused = CountDownLatch(1)
    @Volatile private var detaching = false
    @Volatile private var stopped = false

    fun beginDetach() {
        detaching = true
        if (stopped) paused.countDown()
    }

    // Android only guarantees that no more buffers arrive after the pause callback.
    override fun onPaused() { if (detaching) paused.countDown() }
    override fun onStopped() { stopped = true; paused.countDown() }

    fun awaitPaused(timeoutMs: Long): Boolean = try {
        paused.await(timeoutMs, TimeUnit.MILLISECONDS)
    } catch (_: InterruptedException) {
        Thread.currentThread().interrupt()
        false
    }
}

internal class MenuOwner(onBack: () -> Unit) : LifecycleOwner, SavedStateRegistryOwner,
    ViewModelStoreOwner, OnBackPressedDispatcherOwner {
    private val registry = LifecycleRegistry(this)
    private val saved = SavedStateRegistryController.create(this)
    override val lifecycle: Lifecycle get() = registry
    override val savedStateRegistry: SavedStateRegistry get() = saved.savedStateRegistry
    override val viewModelStore = ViewModelStore()
    override val onBackPressedDispatcher = OnBackPressedDispatcher(onBack)

    init { saved.performRestore(null); registry.currentState = Lifecycle.State.CREATED }
    fun resume() { registry.currentState = Lifecycle.State.RESUMED }
    fun destroy() { registry.currentState = Lifecycle.State.DESTROYED; viewModelStore.clear() }
}

private class MenuCursor : Drawable() {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private var root: View? = null
    private var pointerX = 0f
    private var pointerY = 0f
    private var shown = false

    fun move(target: View, x: Float, y: Float, shown: Boolean) {
        if (target !== root) {
            root?.overlay?.remove(this)
            root = target
            target.overlay.add(this)
        }
        setBounds(0, 0, target.width, target.height)
        pointerX = x; pointerY = y; this.shown = shown
        invalidateSelf()
    }

    fun clear() { root?.overlay?.remove(this); root = null }

    override fun draw(canvas: Canvas) {
        if (!shown) return
        val density = root?.resources?.displayMetrics?.density ?: 1f
        paint.color = Color.BLACK
        canvas.drawCircle(pointerX, pointerY, 7f * density, paint)
        paint.color = Color.WHITE
        canvas.drawCircle(pointerX, pointerY, 5f * density, paint)
    }

    override fun setAlpha(alpha: Int) { paint.alpha = alpha }
    override fun setColorFilter(colorFilter: ColorFilter?) { paint.colorFilter = colorFilter }
    @Deprecated("Deprecated in Java") override fun getOpacity(): Int = PixelFormat.TRANSLUCENT
}
