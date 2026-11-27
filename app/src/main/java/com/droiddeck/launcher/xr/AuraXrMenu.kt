package com.droiddeck.launcher.xr

import com.droiddeck.launcher.session.SessionService
import com.droiddeck.launcher.session.SessionState

internal object AuraXrMenu {
    fun matchesSession(sessionId: String?): Boolean = XrPlatform.isAuraPuck() &&
        !sessionId.isNullOrBlank() && sessionId == SessionState.sessionId && SessionState.running &&
        !SessionState.stopRequested && SessionState.mode == SessionService.MODE_STEAM
}
