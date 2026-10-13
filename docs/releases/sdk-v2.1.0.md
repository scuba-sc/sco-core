# sdk-v2.1.0 preparation

Status: version and scoped distribution preparation; final candidate bytes require
signed schema-v2 maintainer approval before tag or publication.

## Coordinates and distribution

- Core SDK, legacy combined SDK and neutral core-source kit: 2.1.0.
- Optional game-sc public overlay: 0.4.0, following the additive-service minor
  rule in [game-pack policy](../game-pack.md). Its compatibility descriptor binds
  the exact core 2.1.0 archive SHA-256; changed bytes retain a new package identity.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.
- Candidate profiles: core, game-sc, legacy and core-source only. The neutral
  source kit retains game exclusions. host-source-sc publication is excluded;
  game implementation/product export remains held.

The [SDK2.1 scoped maintainer decision](../legal/release-decisions/2026-10-12-sdk-v2.1.0-scope.md)
binds the reviewed engineering source and supplies no licence grant or counsel opinion.
Final publication requires a separate exact-byte signed approval.

## Engineering scope and limits

The [CHANGELOG](../../CHANGELOG.md#sdk-v210-2026-10-12) preserves the complete
engineering entries and their limits: additive game contracts, renderer-neutral
widgets and language adapters, content access and default-off diagnostics, plus
host-only lifecycle/admission foundations. Controls and warnings remain unchanged.

Native Windows and portable synthetic CI fixtures provide no-game engineering
evidence, not engine or gameplay acceptance. Real-game, anti-cheat/official-network,
private-LAN/firewall and GPU renderer acceptance remain unproven. No offline consumer
pin or legacy deletion is part of this preparation.
