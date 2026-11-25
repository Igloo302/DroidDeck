package com.droiddeck.launcher

import android.app.NativeActivity
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import android.widget.Toast
import android.view.KeyEvent
import android.view.Surface
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import com.droiddeck.launcher.input.XrGamepadInput
import com.droiddeck.launcher.input.JoyCon
import com.droiddeck.launcher.gpu.SteamVr
import com.droiddeck.launcher.session.SessionService
import com.droiddeck.launcher.session.SessionState
import com.droiddeck.launcher.wayland.CompositorHost
import com.droiddeck.launcher.wayland.WaylandCompositor
import com.droiddeck.launcher.xr.SteamVrHandVisuals
import com.droiddeck.launcher.xr.AuraXrSettings
import com.droiddeck.launcher.xr.AuraXrMenuPresentation
import com.droiddeck.launcher.xr.XrPlatform
import com.droiddeck.launcher.session.SessionPrefs
import java.io.File
import java.lang.ref.WeakReference
import java.util.concurrent.atomic.AtomicBoolean

open class AndroidXrActivity : NativeActivity() {
    private lateinit var gamepad: XrGamepadInput
    private var steamVrStarted = false
    @Volatile private var closing = false
    @Volatile private var xrVisible = false
    @Volatile private var steamVrStatus = SteamVr.Status.UNAVAILABLE
    private var sessionId: String? = null
    private val menuRequested = AtomicBoolean()
    private val menuLock = Any()
    @Volatile private var menuPresentation: AuraXrMenuPresentation? = null
    @Volatile private var menuRevision = 0
    private var menuPending = false
    private var unregisterBack: (() -> Unit)? = null
    private val auraMenu: Boolean get() = this is AuraXrActivity && XrPlatform.isAuraPuck()
    override fun onCreate(savedInstanceState: Bundle?) {
        applyAndroidXrDefaults(intent)
        gamepad = XrGamepadInput(this) { SessionState.padBridge }
        sessionId = SessionState.sessionId
        publishVisibility(true)
        super.onCreate(savedInstanceState)
        if (auraMenu) auraActivity = WeakReference(this)
        if (auraMenu && Build.VERSION.SDK_INT >= 33) {
            val callback = OnBackInvokedCallback { if (!onXrBack()) finish() }
            onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback)
            unregisterBack = { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(callback) }
        }
    }

    override fun onResume() {
        super.onResume()
        gamepad.resume()
    }

    override fun onPause() {
        onXrMenuClosed()
        gamepad.pause()
        super.onPause()
    }

    fun onXrInputFocus(focused: Boolean) = gamepad.focus(focused)
    fun onXrGamepadMenu(): Int = gamepad.menuSequence
    fun onXrJoyConState(): IntArray? = gamepad.nativeJoyConState()
    fun onXrViewZoom(): Float = AuraXrSettings.read(this).viewZoomPercent / 100f

    fun onXrBack(canceled: Boolean = false): Boolean {
        if (!auraMenu) return false
        if (canceled) return true
        if (closing || !sessionAlive()) return false
        runOnUiThread {
            if (closing || isFinishing || isDestroyed || !sessionAlive()) return@runOnUiThread
            if (menuRequested.get()) {
                val presentation = menuPresentation
                if (presentation != null) presentation.back() else onXrMenuClosed()
            }
            else {
                gamepad.setMenuOpen(true)
                menuRequested.set(true)
            }
        }
        return true
    }

    fun onXrMenuSupported(): Boolean = auraMenu
    fun onXrMenuRequested(): Boolean = auraMenu && menuRequested.get() && !closing && sessionAlive()

    fun onXrMenuSurface(surface: Surface?, width: Int, height: Int): Boolean {
        if (surface == null) {
            val previous = synchronized(menuLock) {
                menuRevision++
                menuPending = false
                menuPresentation.also {
                    if (it != null && !it.detachSurface()) return false
                    menuPresentation = null
                }
            }
            if (previous != null) runOnUiThread { previous.close() }
            return true
        }
        if (!onXrMenuRequested()) return false
        val revision = synchronized(menuLock) {
            if (menuPresentation != null) return true
            if (menuPending) return false
            menuPending = true
            menuRevision
        }
        runOnUiThread {
            synchronized(menuLock) {
                if (revision != menuRevision || !onXrMenuRequested()) return@synchronized
                menuPresentation = runCatching {
                    SessionActivity.openAuraMenu(this, surface, width, height) {
                        if (revision == menuRevision) onXrMenuClosed()
                    }
                }.onFailure { Log.e("DroidDeckXR", "Could not create the immersive menu", it) }.getOrNull()
                menuPending = false
                if (menuPresentation == null) onXrMenuClosed()
            }
        }
        return false
    }

    fun onXrMenuClosed() {
        menuRequested.set(false)
        onXrMenuSurface(null, 0, 0)
        gamepad.setMenuOpen(false)
    }

    fun onXrMenuPointer(action: Int, x: Float, y: Float) {
        val target = menuPresentation ?: return
        runOnUiThread {
            if (menuPresentation === target && menuRequested.get()) target.dispatchPointer(action, x, y)
        }
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (!onXrBack()) super.onBackPressed()
    }

    fun onXrGamepadKey(device: Int, source: Int, action: Int, code: Int, scan: Int,
                       repeat: Int, meta: Int, flags: Int, downTime: Long, eventTime: Long): Boolean {
        val event = KeyEvent(downTime, eventTime, action, code, repeat, meta, device, scan, flags, source)
        if (!menuRequested.get()) return gamepad.key(event)
        if (code == KeyEvent.KEYCODE_BACK && JoyCon.side(event.device) < 0) return false
        gamepad.key(event)
        val key = JoyCon.normalize(event)
        val trigger = JoyCon.vrButton(JoyCon.side(event.device), scan) == JoyCon.VR_TRIGGER
        val target = menuPresentation
        if (!trigger && target != null) runOnUiThread {
            if (menuPresentation === target && menuRequested.get()) target.dispatchKey(key)
        }
        return true
    }

    fun onXrGamepadMotion(device: Int, source: Int, action: Int, downTime: Long,
                          eventTime: Long, axes: FloatArray): Boolean {
        val event = XrGamepadInput.nativeMotionEvent(device, source, action, downTime, eventTime, axes) ?: return false
        val handled = gamepad.motion(event)
        val target = menuPresentation
        if (menuRequested.get() && target != null) {
            runOnUiThread {
                try {
                    if (menuPresentation === target && menuRequested.get()) target.dispatchMotion(event)
                } finally { event.recycle() }
            }
            return true
        }
        event.recycle()
        return handled
    }

    fun onXrActive(active: Boolean) {
        xrVisible = active
        if (active) {
            check(CompositorHost.isStarted) { "DroidDeck compositor is not running" }
            SessionService.resume(this)
            WaylandCompositor.nativeSetCardboard(false, 1, 0.064f, 3f, 4f, 0f, 0f, 90f, 0f, 0f, false, 0f)
            WaylandCompositor.nativeSetFrameGenArmed(false, 0, 0)
            WaylandCompositor.nativeSetZeroCopy(false)
            CompositorHost.attachXr()
            WaylandCompositor.nativeVsync(System.nanoTime())
            check(WaylandCompositor.nativeWaitSurface(3000)) { "XR frame GPU handshake timed out" }
        }
        publishVisibility(active)
        if (active) startSteamVr()
    }

    private fun startSteamVr() {
        if (!steamVrStarted) {
            steamVrStarted = true
            Thread({
                try {
                    val started = synchronized(steamVrLock) {
                        if (closing || !sessionAlive()) false else {
                            steamVrOwner = this
                            SteamVr.run(this, "start")
                        }
                    }
                    if (!started) {
                        if (!closing) leaveXr("SteamVR start failed", sessionAlive())
                        return@Thread
                    }
                    val startupDeadline = SystemClock.elapsedRealtime() + 30_000
                    var lastRunning: Long? = null
                    while (!closing) {
                        if (!sessionAlive()) {
                            leaveXr("Steam session ended", false)
                            break
                        }
                        if (!xrVisible) { lastRunning = SystemClock.elapsedRealtime(); Thread.sleep(500); continue }
                        steamVrStatus = SteamVr.status(this)
                        val now = SystemClock.elapsedRealtime()
                        if (steamVrStatus == SteamVr.Status.RUNNING) lastRunning = now
                        val deadline = lastRunning?.plus(5_000) ?: startupDeadline
                        if (steamVrStatus != SteamVr.Status.RUNNING && now >= deadline) {
                            leaveXr("SteamVR stopped or did not finish starting", true)
                            break
                        }
                        Thread.sleep(1000)
                    }
                } finally {
                    synchronized(steamVrLock) {
                        // A retiring activity must not stop a replacement's SteamVR session.
                        if (steamVrOwner === this) {
                            if (sessionAlive()) SteamVr.run(this, "stop")
                            steamVrOwner = null
                        }
                    }
                }
            }, "xr-steamvr-start").start()
        }
    }

    private fun publishVisibility(visible: Boolean) {
        xrVisibility.update(this, visible && !closing) { anyVisible ->
            SessionService.setXrActivityVisible(this, anyVisible)
        }
    }

    private fun sessionAlive() = SessionState.running && !SessionState.stopRequested &&
        sessionId != null && SessionState.sessionId == sessionId

    private fun leaveXr(reason: String, failed: Boolean) {
        Log.w("DroidDeckXR", reason)
        closing = true
        runOnUiThread {
            if (failed) Toast.makeText(this, getString(R.string.cardboard_steamvr_failed) + "\n" + reason, Toast.LENGTH_LONG).show()
            finish()
        }
    }

    fun onXrError(reason: String) {
        leaveXr(reason, true)
    }

    fun onXrFrame(frameTimeNanos: Long) {
        WaylandCompositor.nativeVsync(frameTimeNanos)
    }

    fun onXrStatus(): Int = steamVrStatus.ordinal

    fun onXrHandModel(hand: Int): ByteArray? = runCatching {
        SteamVrHandVisuals.model(SessionPrefs.activeSteamRoot(this), hand != 0)
    }.onFailure { Log.w("DroidDeckXR", "SteamVR reference hand model unavailable", it) }.getOrNull()

    fun onXrShowHands(): Boolean = SteamVrHandVisuals.visible(
        File(filesDir, ".wayland-rt/droiddeck-steamvr/scene-focus.json"))

    override fun onDestroy() {
        closing = true
        onXrMenuClosed()
        if (auraActivity.get() === this) auraActivity.clear()
        unregisterBack?.invoke()
        unregisterBack = null
        gamepad.close()
        publishVisibility(false)
        super.onDestroy()
    }

    override fun onStop() {
        publishVisibility(false)
        super.onStop()
    }

    companion object {
        private var auraActivity = WeakReference<AndroidXrActivity>(null)

        fun stopAuraSession(sessionId: String?) {
            val activity = auraActivity.get() ?: return
            if (activity.auraMenu && sessionId != null && activity.sessionId == sessionId)
                activity.leaveXr("SteamVR stopped from DroidDeck menu", false)
        }

        fun launchIntent(context: Context): Intent = if (XrPlatform.isAuraPuck())
            Intent(context, AuraXrActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        else Intent(context, AndroidXrActivity::class.java)

        private val xrVisibility = com.droiddeck.xr.XrActivityVisibility()
        private val steamVrLock = Any()
        private var steamVrOwner: AndroidXrActivity? = null
    }
}

internal fun applyAndroidXrDefaults(intent: Intent) {
    if (!intent.hasExtra("xr_input")) intent.putExtra("xr_input", "auto")
}
