package com.droiddeck.launcher.xr

import android.content.Intent
import com.droiddeck.launcher.AndroidXrActivity
import com.droiddeck.launcher.AuraXrActivity
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

    @Test fun menuRequiresItsCurrentNonemptySession() {
        assertTrue(AuraXrMenu.matchesSession(SessionState.sessionId))
        assertFalse(AuraXrMenu.matchesSession(null))
        assertFalse(AuraXrMenu.matchesSession(""))
        assertFalse(AuraXrMenu.matchesSession("old-session"))
    }

    @Test fun menuRequiresRunningNonstoppingSteamSession() {
        val session = SessionState.sessionId
        SessionState.running = false
        assertFalse(AuraXrMenu.matchesSession(session))
        SessionState.running = true
        SessionState.stopRequested = true
        assertFalse(AuraXrMenu.matchesSession(session))
        SessionState.stopRequested = false
        SessionState.mode = SessionService.MODE_DESKTOP
        assertFalse(AuraXrMenu.matchesSession(session))
        SessionState.mode = SessionService.MODE_STEAM
        assertTrue(AuraXrMenu.matchesSession(session))
    }

    @Test fun aNewSessionInvalidatesTheStoppedMenuHost() {
        val original = SessionState.sessionId
        SessionState.sessionId = "replacement-session"
        assertFalse(AuraXrMenu.matchesSession(original))
        assertTrue(AuraXrMenu.matchesSession(SessionState.sessionId))
    }

    @Test fun genericHeadsetsKeepTheirOriginalLaunchAndCannotOpenAuraMenu() {
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
            assertFalse(AuraXrMenu.matchesSession(SessionState.sessionId))
        }
    }

    @Test fun caseInsensitiveAuraIdentityUsesItsDedicatedXrTask() {
        ShadowBuild.setManufacturer("xreal")
        ShadowBuild.setModel("ANDROID XR PUCK")
        ShadowBuild.setDevice("AXRPUCK")
        val intent = AndroidXrActivity.launchIntent(context)
        assertEquals(AuraXrActivity::class.java.name, intent.component?.className)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK, intent.flags)
        assertTrue(AuraXrMenu.matchesSession(SessionState.sessionId))
    }
}
