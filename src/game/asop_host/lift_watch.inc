// SPDX-License-Identifier: GPL-3.0-only
// Original lift raise fallback loop shared by provider and fixture.
void RunLiftWatches(DWORD now) {
    for (size_t i = 0; i < g_liftWatches.size();) {
        LiftWatch& w = g_liftWatches[i];
        const int32_t liftState = LiftStateOf(w.manager);
        LogLiftState("Retrieve", w.manager, liftState, w.lastState);
        if (liftState == kOpenIdle || now - w.spawnedAt > kLiftWatchMs) { g_liftWatches.erase(g_liftWatches.begin() + i); continue; }
        if (liftState == kClosedIdle && !w.opened && now - w.spawnedAt > kLiftOpenFallbackMs) {
            w.opened = true;
            Log("[asop] Retrieve: the lift is still down %lu ms after the spawn; raising it", now - w.spawnedAt);
            if (!CallLift(g_liftOpen, w.manager)) Log("[asop] Retrieve: lift open request faulted");
        }
        ++i;
    }
}
