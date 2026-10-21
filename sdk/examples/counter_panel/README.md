# counter_panel (native C): a consumer

The consumer half of a two-plugin example. It owns no counter: its commands
`counter_panel.bump` and `counter_panel.show` call the table that
[`counter_service`](../counter_service) publishes as `counter_service.counter` 1.0.
The design rules behind the pair are in the plugin design guide
(`docs/plugin-design.md` in the sco-core repository).

- `plugin.ini` says `requires = counter_service.counter`. The host loads
  `counter_service` first (this folder sorts before it) and refuses `counter_panel`,
  with the reason in `mod.log`, when no installed plugin can provide the service.
- The table is looked up with `query_service` when a command runs, not kept.
- Each member is checked before the call: the table's `size` covers it and it is
  not NULL (`COUNTER_SERVICE_HAS` in the provider's header).
- `SCO_NOT_FOUND` and `SCO_UNAVAILABLE` are normal answers: the commands reply
  with the reason and return `SCO_UNAVAILABLE`.

It includes the provider's header as `../counter_service/counter_service.h`, so
keep the two folders side by side, or copy the header into your own project.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # Windows: add -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix out
<sdk>/bin/sco-plugin-check out/data/plugins/counter_panel
```

`sco-plugin-check` checks one folder alone, so the provider is absent there. That
makes the degraded path visible:

```text
invoke  counter_panel.bump -> unavailable "counter_service.counter is not installed"
OK: 0 failure(s)
```

With both plugins installed, `counter_panel.bump` answers `counter is 1`. sco-core's
`host_sim_examples` test loads both through the real neutral host and checks that
reply. Not run in game.
