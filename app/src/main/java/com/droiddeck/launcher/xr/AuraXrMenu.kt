package com.droiddeck.launcher.xr

import android.content.Context
import android.content.Intent
import android.os.Bundle
import com.droiddeck.launcher.AndroidXrActivity
import com.droiddeck.launcher.SessionActivity
import com.droiddeck.launcher.session.SessionService
import com.droiddeck.launcher.session.SessionState

object AuraXrMenu {
    const val ACTION_OPEN = "com.droiddeck.launcher.OPEN_AURA_XR_MENU"
    private const val EXTRA_SESSION = "auraXrMenuSession"

    fun openIntent(context: Context, sessionId: String): Intent =
        Intent(context, SessionActivity::class.java).setAction(ACTION_OPEN)
            .putExtra(EXTRA_SESSION, sessionId).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

    internal fun requestedSession(intent: Intent?): String? =
        intent?.takeIf { it.action == ACTION_OPEN }?.getStringExtra(EXTRA_SESSION)?.takeIf(::matchesSession)

    internal fun matchesSession(sessionId: String?): Boolean = XrPlatform.isAuraPuck() &&
        !sessionId.isNullOrBlank() && sessionId == SessionState.sessionId && SessionState.running &&
        !SessionState.stopRequested && SessionState.mode == SessionService.MODE_STEAM

    internal fun consume(intent: Intent) {
        intent.action = null
        intent.removeExtra(EXTRA_SESSION)
    }
}

internal class AuraXrMenuReturn {
    private var sessionId: String? = null

    fun open(intent: Intent?): Boolean {
        val requested = AuraXrMenu.requestedSession(intent) ?: return false
        sessionId = requested
        return true
    }

    fun isPending(): Boolean {
        if (!AuraXrMenu.matchesSession(sessionId)) cancel()
        return sessionId != null
    }

    fun takeReturnIntent(context: Context): Intent? {
        if (!isPending()) return null
        cancel()
        return AndroidXrActivity.launchIntent(context)
    }

    fun cancel() { sessionId = null }

    fun save(state: Bundle) {
        if (isPending()) state.putString(STATE_SESSION, sessionId)
    }

    fun restore(state: Bundle?) {
        sessionId = state?.getString(STATE_SESSION)?.takeIf(AuraXrMenu::matchesSession)
    }

    private companion object {
        const val STATE_SESSION = "auraXrMenuReturnSession"
    }
}
