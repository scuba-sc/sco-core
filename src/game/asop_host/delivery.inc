// SPDX-License-Identifier: GPL-3.0-only
// Existing delivery loop, compiled by Windows provider and synthetic primitive fixture.
void StartDelivery(const DeliverRequest& r, DWORD now) {
    const int ship = FleetShipIndexOfUrnId(r.urn);
    const char* cls = ship >= 0 ? FleetShipClass(ship) : nullptr;
    if (!cls) {
        Log("[asop] Deliver: not a ship of the fleet list (id %016llX %016llX); ignored", static_cast<unsigned long long>(r.urn[0]),
            static_cast<unsigned long long>(r.urn[1]));
        return;
    }
    if (ShipIsBusy(ship)) {
        Log("[asop] Deliver %s: already stored, out or being delivered this session; ignored", cls);
        return;
    }
    const double offset[3] = { 0, 0, kDeliverySpawnHeight };
    uint64_t id = 0;
    if (const char* err = SpawnEntityNearPlayer(cls, offset, id)) {
        Log("[asop] Deliver %s: spawn failed: %s", cls, err);
        return;
    }
    RegisterPlayerVehicle(id);
    g_deliveries.push_back({ ship, r.atc, r.location, id, r.at, now });
    Log("[asop] Deliver %s: spawned 0x%llX %.0f m above you, storing it at location %u in %lu ms", cls,
        static_cast<unsigned long long>(id), kDeliverySpawnHeight, r.location, kDeliverySettleMs);
}

void RunDeliveries(DWORD now) {
    for (size_t i = 0; i < g_deliveries.size();) {
        Delivery& d = g_deliveries[i];
        const char* cls = FleetShipClass(d.ship);
        if (now - d.spawnedAt < kDeliverySettleMs) { ++i; continue; }
        if (!EntityAlive(d.vehicle)) {
            if (now - d.spawnedAt < kDeliveryGiveUpMs) { ++i; continue; }
            Log("[!] [asop] Deliver %s: the ship never appeared; given up", cls ? cls : "?");
            UnregisterPlayerVehicle(d.vehicle);
            g_deliveries.erase(g_deliveries.begin() + i);
            continue;
        }
        StoredShip s;
        s.ship = d.ship;
        s.vehicle = d.vehicle;
        s.location = d.location;
        s.atc = d.atc;
        s.storedAt = now;
        s.cls = cls ? ClassByName(cls) : 0;
        if (const char* err = StoreShip(d.atc, d.vehicle, d.location, s.inventory)) {
            Log("[!] [asop] Deliver %s: store failed: %s", cls ? cls : "?", err);
        } else {
            g_stored.push_back(s);
            Log("[asop] Deliver %s: stored 0x%llX at location %u (ATC 0x%llX)", cls ? cls : "?", static_cast<unsigned long long>(d.vehicle),
                d.location, static_cast<unsigned long long>(d.atc));
            // The terminal shows the station and "Awaiting Delivery", then "Stored" once its own
            // claim request has timed out .
            RequestRefresh(d.requestedAt + kDeliverySettleMs + kRefreshAfterStoreMs);
            RequestRefresh(d.requestedAt + kClaimTimeoutSeconds * 1000 + kRefreshAfterClaimMs);
        }
        UnregisterPlayerVehicle(d.vehicle);
        g_deliveries.erase(g_deliveries.begin() + i);
    }
}

