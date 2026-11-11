# Experimental Rust plugin support

This dependency-free, low-level crate mirrors `sco_api.h` 1.1. It does not change
the C ABI or offer a safe Rust SDK. The broader Rust refactor is deferred to 2.5.0.
These bindings and the example are experimental, not a claim of game acceptance,
plugin safety, distribution approval or stable Rust API. Other host and game service
headers do not yet have Rust bindings; the scaffolder does not yet support Rust.
These files are in the current unpublished development core and combined plugin
kits; the standalone source kits, game overlay and published stable SDK 2.4.0
archives do not contain them. Check the selected archive's inventory.

For the supported product target, use the x86_64-pc-windows-msvc Rust toolchain
(Rust 1.99 or newer) with the Visual Studio x64 build tools. From an extracted core
or combined development kit that contains these files:

```sh
cargo test --offline --locked --manifest-path rust/sco-sdk/Cargo.toml
cargo test --offline --locked --manifest-path examples/rust_hello/Cargo.toml
cargo build --offline --locked --release --manifest-path examples/rust_hello/Cargo.toml
```

With the default Cargo configuration, place
`examples/rust_hello/target/release/rust_hello.dll` and its `plugin.ini` in an
otherwise empty `rust_hello` plugin folder. A configured target directory changes
the root, and an explicitly configured target triple adds a target-triple
directory before `release`. Build the kit's checker as described
in [Build the examples](../README.md#build-the-examples), then use `sco-plugin-check` on
that folder before testing in a host. Build Linux and macOS shared objects only for
development tests; they are not Windows game plugins. The example uses the target's
default C runtime linkage. Verify the corresponding Microsoft Visual C++ runtime is
available to the host, or independently validate a static CRT build for your target.

The current MCP language-filtered API and example searches advertise `c`, `cpp`,
`csharp` and `lua`, not Rust; the CLI and MCP scaffolder do not generate Rust.
Read this guide and `examples/rust_hello` directly, and inspect the underlying C
contract for the raw bindings. A missing Rust filter is not evidence of an absent
C contract or a live provider.

All declarations are unsafe. Follow the lifetime, threading, ownership and capability
rules in `sco_api.h` and `docs/api-v1.md`. Inspect the table size before *each*
member access and check the entire pointer extent; never make a reference to a full
`Api` when an older host may supply a shorter allocation. Check nullable callbacks.
Use C calling convention and exact unmangled exports. The example's release profile
sets `panic = "abort"`; do not allow panics or foreign exceptions to unwind across the
ABI. A panic aborts the process, so callback code must avoid panics. The host removes
registered commands at unload; plugin-owned callback state must stay valid until then.
Strings and callback inputs are borrowed for the durations stated in the C contract.

`cargo test` checks Rust layouts against the frozen C ABI pins and refusal of a real
short allocation. The `experimental-rust-qualification` workflow runs for pull requests
and manual dispatch, checking the exact pull-request head. It is configured to build
unpublished legacy, core and game-sc development archives on
each of Linux, Windows and macOS with Rust 1.99.1. On each system it tests the legacy
and core archives, then the core with game-sc overlaid. Those checks run both Rust crate
tests, build the example and invoke it with `sco-plugin-check` against full and short
host tables. The workflow's configuration does not establish that a run passed; inspect
the result for the exact source revision. The Rust files in the core profile remain
experimental, and inclusion in an archive does not establish platform support.

The checker does not actually close the dynamic library. These checks do not replace
review, real-host validation or in-game acceptance. GPL-3.0-only applies; see the kit's
licence and `sco_api.h`.
