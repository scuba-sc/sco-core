# counter_service (native C): a provider

The provider half of a two-plugin example. It owns one counter and shares it only
through a versioned service table, `counter_service.counter` 1.0, declared in
[`counter_service.h`](counter_service.h): `add(delta, &value)` and `get(&value)`,
after a `uint32_t size` so the table can grow at the end. It also registers one
command for people, `counter_service.value`. The consumer is
[`counter_panel`](../counter_panel). The design rules behind the pair are in the
plugin design guide (`docs/plugin-design.md` in the sco-core repository).

Plain C against `sco_api.h` only: no game header, no capability, no game memory.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # Windows: add -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix out
<sdk>/bin/sco-plugin-check out/data/plugins/counter_service --invoke counter_service.value
```

The checker shows `service counter_service.counter 1.0` and
`invoke  counter_service.value -> ok "counter is 0"`. With `--short-tables` (a 1.0
host's `sco_api`, which has no `provide_service`) it still passes: the plugin logs
`this host has no provide_service (sco_api 1.0): counter_service.counter is not
published` and keeps its command. A real 1.0 host never loads it, because
`plugin.ini` says `api = 1.1`. The checker loads one folder at a time
and does not connect one plugin's service to another; the host does. The two
plugins together are exercised by sco-core's `host_sim_examples` test, which loads
both through the real neutral host and calls `counter_panel.bump`.

Once any plugin has looked the table up, the host keeps this plugin loaded for the
rest of the game session (`sco.reload` refuses it); restart the game to load a new
build. Not run in game.
