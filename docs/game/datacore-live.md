# Live DataCore host backend

C++/C#/Lua typed wrappers are unsupported; C table access is supported.

`game.datacore_live` 1.0 provides bounded scalar-pool access through a copied
host backend. It supplies no native engine adapter. Without verified callbacks,
fresh offline admission and a live pool/session, operations return unavailable.

Calls require the game thread and a live plugin owner. Output records require
their full declared extent. The backend owns pool bounds and layout validation.
Writes additionally require a separate host opt-in, OFF by default.

Start cannot replace a live backend. Stop withdraws the service, clears callbacks
and write permission. A callback exception or native fault latches the provider
unavailable until process restart. Synthetic pool fixtures are not native engine
evidence. Not run in game.
