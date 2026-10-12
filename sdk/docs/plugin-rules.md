# Plugin rules

sc-offline is an offline mod: while it runs, the game never connects to Cloud Imperium Games' servers. Plugins follow the same scope as sc-offline's own code ([CONTRIBUTING](https://github.com/scubamount/sc-offline/blob/main/CONTRIBUTING.md#what-fits-this-project)). Offline play stays the default.

## What a plugin may connect to

Only these two, and only through the host's services:

- **Other processes on the same PC, by local IPC** (shared memory), for example a bridge to Titanfall 2 (Northstar) or Minecraft.
- **Other players running sc-offline, privately, over a LAN or a VPN they choose** (Tailscale, ZeroTier), through sco-core's `sco.net` service: co-presence. Each player runs their own offline game. Peers exchange their own authenticated messages (poses, spawn announcements), and each game shows remote players as "ghost" entities it spawns itself. Co-presence doesn't use or change the game's own netcode or servers.

Both are host services, `sco.ipc` 1.0 ([IPC](../../docs/ipc.md)) and `sco.net` 1.0 ([multiplayer messages](../../docs/net.md)); a plugin can't open or join a session, only use the channels the product's session gives it. The design is [design/multiplayer.md](../../docs/design/multiplayer.md). A native plugin can technically open its own sockets or shared memory (it runs with the game's rights), so this is a review rule, not a sandbox.

## What a plugin may not do

These are permanent:

- Connect to Cloud Imperium Games' servers or online services, or take part in public or official online play.
- Get around anti-cheat or skip any anti-cheat step, or bypass signature checks.
- Tamper with accounts or entitlements.
- Force the game's host type or override its network context.
- Run console commands or other commands from a peer without authentication.
- Send telemetry.
- Help anyone cheat in the official game.
- Open network connections or shared memory of its own, outside `sco.net` and the IPC service.
- Ship pieces of the game: no parts of `StarCitizen.exe`, dumps, or files extracted from `Data.p4k`. Entity and class names in text files are fine.
- Collect or send personal data.

## What a plugin should do

- Use only `sco_api.h` (native) or the `sco` table (Lua). The C++ headers in sco-core's `include/sco/` are internal and change without notice.
- Check `has()` before offering a feature, and say why it is off, instead of failing.
- Keep callbacks short. `tick` runs about 10 times a second on the game's main thread; slow work there stalls the game.
- Name everything with its id: commands are `<id>.<action>`, and the host prefixes log lines and status messages with it.

## Native trust and supported usage

Native plugins are **trusted in-process code**, with the host process's full rights.
There is no native sandbox: custom unmanaged threads, direct OS calls and raw
service-table calls outside host dispatch are not intercepted by the callback guard.
The guard is a fault-attribution boundary for host-entered code, not proof that
native memory corruption is contained or that the process can safely continue.

Supported native usage requires:

- Follow each service's thread, reentrancy, ownership and buffer-lifetime contract.
  Any-thread service lookup does not make every table method any-thread.
- Keep a legacy table only within its supported provider lifetime. Successful lookup
  prevents administrative unload/reload of that native provider; it does **not**
  authorize calls after a provider fault, during shutdown, or across host sessions.
  A mapped DLL alone does not preserve the provider's backing state.
- Treat C++ and C# `ServiceRef` wrappers as borrowed pointer holders, not counted
  leases. Withdrawing a service stops future lookup, not calls through cached pointers.
- Use host-owned callbacks/tasks for dispatch. A plugin that creates workers must
  stop and join them before its state is destroyed; the host does not track or drain
  arbitrary unmanaged threads. Do not call peer providers from unload callbacks or
  after terminal shutdown admission has closed.
- On a native fault, stop using cached tables and restart. Do not retry raw calls or
  infer recovery from a diagnostic report. Out-of-contract native calls are unsupported,
  even when they happen to work.

See the [service lifetime audit](../../docs/design/service-table-lifetime.md) for the
current administrative, fault and shutdown distinctions.

## What the host guarantees

- Nothing loads unless the player sets `plugins = on`. Each plugin can be switched off on its own.
- Every plugin is listed in `mod.log` and `sc-offline.exe status` with its state.
- Host-entered native callbacks have fault attribution and scheduling suppression, not a recovery guarantee. An attributed provider fault revokes new lookup and host work; an exposed provider enters a degraded, restart-required state with backing state retained. Other fault paths disable/quarantine the owner. Cached raw calls, arbitrary workers and corrupted process state cannot be made safe by that guard; continued execution may be unsafe or require a controlled stop. Native plugins have the host's full rights (see [supported usage](#native-trust-and-supported-usage)).
- A native plugin is session-lifetime once anything has queried one of its services: the host refuses to reload or unload it while the game runs (`sco.reload` answers `provides a service in use; restart the game to reload it`), because a caller may still hold the service table. Restart the game to pick up a new build of such a plugin; a provider nobody ever asked a service of reloads while the game runs.
- A Lua plugin runs in a sandbox with no file, process or network access, and an instruction budget per callback. Once `sco.net` ships, one exception: a Lua plugin whose `plugin.ini` declares `requires = sco.net` may read the peer list and publish and subscribe to `<plugin-id>.<channel>` messages. It never gets a socket, an address or session control (hosting, joining, admitting or removing peers), and gets no IPC.
- A data pack never runs code.
- sc-offline's self-update never touches `data\plugins\`.

## License

sco-core is GPL-3.0-only. The project expects plugins that include its headers or compile its SDK sources to be distributed under GPL-3.0-compatible terms. Whether the GPL applies to a particular plugin depends on how it is built and on applicable law; this is not legal advice. No linking exception is granted.
