# Container contents

`game.inventory.contents` 1.0 (`sc_container_contents.h`) copies a bounded
read-only snapshot of stored items. It is independent of `game.inventory`, whose
records describe attachments to equipped item ports. Neither frozen `sco_api.h`
nor the existing item-port record/table layouts change.

The caller supplies a live plugin handle, nonzero session-local container ID,
output size and capacity (0..64). The service validates provider output in its own
storage before copying it. Items have opaque session IDs, class IDs, positive
quantities and optional integer volume in micro-SCU (one millionth of one standard
cargo unit). Providers convert their source units before publication; unknown
native scaling does not authorize VOLUME_KNOWN. No engine pointer crosses the ABI.
Generation is nonzero and provider-scoped. It is a read receipt, not an edit token.

`total_seen` distinguishes a genuinely empty container from a zero-capacity query.
TRUNCATED means more items exist than were returned; VOLUME_KNOWN means the integer
volumes use the contract's micro-SCU unit. Unknown volumes are zero and the flag is
clear; plugins must not label unknown as empty. Complete known-volume snapshots
must sum exactly; partial snapshots cannot exceed used volume. Overflow, duplicate
item IDs, invalid flags and malformed provider records fail without partial output.

Every call is game-thread-only. Null/released/foreign plugin handles, zero IDs,
short output or capacity above 64 return BAD_ARG. Missing backend returns
UNAVAILABLE; unknown containers return NOT_FOUND; malformed data or exceptions
return FAILED. For a sufficiently sized output every failure clears the known
prefix while preserving caller size and future tail. Reentrant reads fail.

Hosts configure a `ContainerContentsBackend` before publishing the service. The
game-pack startup uses `ConfigureProductionContainerContents` followed by
`StartProductionContainerContents`; configuration is game-thread-only, refused
while active, and cleared at service stop. Hosts do not publish a competing table.
The direct `StartContainerContents` entry remains for standalone host fixtures.
The
host owns its callback/context until stop; callback output is copied immediately.
The default game-pack composition publishes an unavailable service with capability
`game.inventory.contents.read` OFF. No production native container adapter ships
in this increment. Synthetic provider/real-host consumer and C/C++ ABI fixtures
are the evidence; not run in game.

The C++ `sco::sdk::game::ContainerContents` wrapper consumes the same table. No C#
or Lua route is supported. Studio mocks and game hosts use the same public layout;
mock readiness is not evidence of native game readiness.
