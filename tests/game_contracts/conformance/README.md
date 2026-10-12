# Real-host SDK conformance

Repository-only fixtures. The native C consumers compile against public SDK
headers only and load from shared libraries through the real neutral host,
service registry, command/event/task runtime, and native loader. The executable
links `sco_host` and `sco_plugins`, **not** game implementation libraries.

The game contract declarations select the tests, so CMake registers this case
with `SCO_GAME_SC=ON`. That setting is not a request to run a game provider; only
the named target below is built. `SCO_GAME_SC=OFF` and the neutral source archive
omit these game-contract fixtures. None of the files here ship in SDK archives.

```sh
cmake -S . -B build-conformance -DSCO_GAME_SC=ON \
  -DSCO_SANITIZE=address,undefined -DCMAKE_BUILD_TYPE=Debug
cmake --build build-conformance --target test_sdk_conformance -j 4
ctest --test-dir build-conformance -R '^sdk_conformance$' --output-on-failure -V
```

For MSVC x64, omit `SCO_SANITIZE`, configure with `-A x64`, and add
`--config Debug` / `-C Debug` to the build / CTest commands. Windows x64 is the
supported product platform; macOS/Linux are synthetic development-test platforms.

## Coverage and boundaries

| Consumer workflow | Public calls exercised | Evidence in this directory |
|---|---|---|
| Read-only inspector | `query_service`, `has`; `game.actors.local_player/health`; `game.entities.alive/get_transform` | `consumer.c:Inspect`, `host.cpp:main` absence/version/tail/null/capability/failure cases; unknown maximum remains `unknown` |
| Owned NPC lifecycle | `game.actors.spawn_npc/despawn`; `game.entities.alive` | `consumer.c:Spawn/Observe/Remove`; two real native owners; pending, streamed, removal-started, remove-before-streaming, repeated spawn/removal and consumer-unload cleanup |
| Actor-death consumption | root `subscribe/unsubscribe` and `sc_game_actor_died` | `consumer.c:Death/Stats`; complete/short/null/zero-ID payloads, copy-before-retention, owner cleanup after unload |
| Host contract failures | `provide_service`, `invoke_raw`, `invoke`, `run_on_game_thread` | real prefix-ownership rejection; wrong-thread raw call; worker queued invoke and task cancelled on native unload, no completion callback; released handle rejected |
| Native provider lifetime | `query_service`, common loader `BeginUnload/CanUnload/UnloadNative/MarkCrashed` | existing `svc_provider` native fixture receives the real host API; handed-out table blocks the reload reservation and explicit unload; injected fault withdraws lookup/scheduling, retains module/state, and requires restart |

The engine tables in `host.cpp` are an explicitly synthetic bounded model. Their
thread/ownership/delayed-operation answers prove consumer handling, **not** the
real game providers' enforcement or timing. NPC locations/classes/IDs and death
payloads are authored synthetic data. `alive=0` is not claimed to distinguish
pending, removed, disabled capability, or wrong-thread use.

Fault injection enters the existing loader's attributed-fault path directly.
It does not test fault attribution, stack walking, arbitrary stale pointer calls,
native process integrity, or recovery. Existing `test_plugins` owns actual
fault/attribution coverage and `test_shutdown_drain` owns terminal shutdown
admission/drain coverage; these fixtures do not replace either. No cached table
is called after the provider fault. This test validates the same reservation
used by `app::Reload`, not the complete app reload cascade/UI/command routes.

C/C++/C#/Lua language parity, NativeAOT, archive-only examples, supported Windows
execution, and exact-build in-game acceptance remain separate checks. A passing
synthetic test must not be cited as gameplay, game provider, or process-safety
certification. Semantic contracts remain in `sdk/contracts/` and
`docs/contracts.md`; this fixture neither extends the frozen ABI nor asserts
semantics those records leave unspecified.
