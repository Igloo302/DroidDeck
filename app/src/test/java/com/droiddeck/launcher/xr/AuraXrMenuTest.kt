package com.droiddeck.launcher.xr

import android.content.Intent
import android.os.Bundle
import com.droiddeck.launcher.AndroidXrActivity
import com.droiddeck.launcher.AuraXrActivity
import com.droiddeck.launcher.SessionActivity
import com.droiddeck.launcher.session.SessionService
import com.droiddeck.launcher.session.SessionState
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowBuild

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], application = android.app.Application::class)
class AuraXrMenuTest {
    private val context get() = RuntimeEnvironment.getApplication()
    private val originalSession = SessionState.sessionId
    private val originalRunning = SessionState.running
    private val originalStopRequested = SessionState.stopRequested
    private val originalMode = SessionState.mode

    @Before fun setup() {
        ShadowBuild.setManufacturer("XREAL")
        ShadowBuild.setModel("Android XR Puck")
        ShadowBuild.setDevice("axrpuck")
        SessionState.sessionId = "current-steam-session"
        SessionState.running = true
        SessionState.stopRequested = false
        SessionState.mode = SessionService.MODE_STEAM
    }

    @After fun tearDown() {
        SessionState.sessionId = originalSession
        SessionState.running = originalRunning
        SessionState.stopRequested = originalStopRequested
        SessionState.mode = originalMode
    }

    private fun request() = AuraXrMenu.openIntent(context, SessionState.sessionId!!)

    @Test fun menuTargetsExistingSessionTaskWithoutClearingIt() {
        val intent = request()
        assertEquals(SessionActivity::class.java.name, intent.component?.className)
        assertEquals(AuraXrMenu.ACTION_OPEN, intent.action)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK, intent.flags)
        assertEquals(SessionState.sessionId, AuraXrMenu.requestedSession(intent))
    }

    @Test fun returnTargetsAuraTaskWithoutClearingItsActivity() {
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(request()))
        val intent = pending.takeReturnIntent(context)!!
        assertEquals(AuraXrActivity::class.java.name, intent.component?.className)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK, intent.flags)
        assertFalse(pending.isPending())
        assertNull(pending.takeReturnIntent(context))
    }

    @Test fun genericAndroidXrLaunchKeepsItsOriginalComponentAndFlags() {
        for ((manufacturer, model, device) in listOf(
            Triple("Samsung", "Galaxy XR", "other-headset"),
            Triple("XREAL", "Other headset", "axrpuck"),
            Triple("Other", "Android XR Puck", "axrpuck"),
            Triple("XREAL", "Android XR Puck", "other-device"),
        )) {
            ShadowBuild.setManufacturer(manufacturer)
            ShadowBuild.setModel(model)
            ShadowBuild.setDevice(device)
            val intent = AndroidXrActivity.launchIntent(context)
            assertEquals(AndroidXrActivity::class.java.name, intent.component?.className)
            assertEquals(0, intent.flags)
            assertNull(AuraXrMenu.requestedSession(request()))
        }
    }

    @Test fun caseInsensitiveAuraIdentityUsesItsDedicatedTask() {
        ShadowBuild.setManufacturer("xreal")
        ShadowBuild.setModel("ANDROID XR PUCK")
        ShadowBuild.setDevice("AXRPUCK")
        val intent = AndroidXrActivity.launchIntent(context)
        assertEquals(AuraXrActivity::class.java.name, intent.component?.className)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK, intent.flags)
        assertNotNull(AuraXrMenu.requestedSession(request()))
    }

    @Test fun menuRequiresItsActionAndMatchingNonemptySessionId() {
        assertNull(AuraXrMenu.requestedSession(null))
        assertNull(AuraXrMenu.requestedSession(Intent()))
        assertNull(AuraXrMenu.requestedSession(request().setAction(Intent.ACTION_VIEW)))
        assertNull(AuraXrMenu.requestedSession(Intent().setAction(AuraXrMenu.ACTION_OPEN)))
        assertNull(AuraXrMenu.requestedSession(AuraXrMenu.openIntent(context, "old-session")))
        assertNull(AuraXrMenu.requestedSession(AuraXrMenu.openIntent(context, "")))
    }

    @Test fun menuRequiresRunningNonstoppingSteamSession() {
        val incoming = request()
        SessionState.running = false
        assertNull(AuraXrMenu.requestedSession(incoming))
        SessionState.running = true
        SessionState.stopRequested = true
        assertNull(AuraXrMenu.requestedSession(incoming))
        SessionState.stopRequested = false
        SessionState.mode = SessionService.MODE_DESKTOP
        assertNull(AuraXrMenu.requestedSession(incoming))
        SessionState.mode = SessionService.MODE_STEAM
        assertNotNull(AuraXrMenu.requestedSession(incoming))
    }

    @Test fun consumedRequestCannotReopenMenuButKeepsLaunchOriginExtras() {
        val intent = request().putExtra(SessionActivity.EXTRA_RETURN_HOME, true)
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(intent))
        AuraXrMenu.consume(intent)
        assertNull(AuraXrMenu.requestedSession(intent))
        assertTrue(intent.getBooleanExtra(SessionActivity.EXTRA_RETURN_HOME, false))
        assertTrue(pending.isPending())
    }

    @Test fun subpanelAndExternalActivityRoundTripKeepsReturnUntilExplicitClose() {
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(request()))
        assertTrue(pending.isPending())
        assertFalse(pending.open(Intent(Intent.ACTION_VIEW)))
        val state = Bundle()
        pending.save(state)
        val restored = AuraXrMenuReturn().apply { restore(state) }
        assertTrue(restored.isPending())
        assertNotNull(restored.takeReturnIntent(context))
    }

    @Test fun explicitStopOrBackgroundCancelsReturn() {
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(request()))
        pending.cancel()
        assertFalse(pending.isPending())
        assertNull(pending.takeReturnIntent(context))
    }

    @Test fun endingSessionInvalidatesReturnEvenIfItLaterRunsAgain() {
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(request()))
        SessionState.running = false
        assertFalse(pending.isPending())
        SessionState.running = true
        assertNull(pending.takeReturnIntent(context))
    }

    @Test fun replacementSessionCannotInheritPendingOrSavedReturn() {
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(request()))
        val state = Bundle()
        pending.save(state)
        SessionState.sessionId = "replacement-session"
        assertNull(pending.takeReturnIntent(context))
        val restored = AuraXrMenuReturn().apply { restore(state) }
        assertFalse(restored.isPending())
    }

    @Test fun stopRequestedPreventsReturningToLiveXrActivity() {
        val pending = AuraXrMenuReturn()
        assertTrue(pending.open(request()))
        SessionState.stopRequested = true
        assertNull(pending.takeReturnIntent(context))
    }
}
