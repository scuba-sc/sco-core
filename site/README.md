# Public documentation site proof

Astro Starlight renders the SDK contract catalogue and explicitly selected public
engineering guides. A small Starlight Search override keeps the native dialog and
lazy Pagefind UI, with task-query normalization and category controls. Search runs
locally against the static build; no hosted search service is configured.

The home page opens with a full-width deepwater landing: **sco-core SDK**, the
original public-contract tagline, **Start a plugin**, **Browse API**, and four
language mapping links. Four thinly ruled library links lead to the documentation
hubs. The original snapshot banner, library summaries, release and archive details
continue below the hero, including source, preview status and evidence limits.
Documentation sidebar groups start collapsed; Starlight expands the group
containing the current page.

The landing uses a deepwater background with static violet/teal nebula clouds,
a restrained warm core, white system typography and teal bracket/core details
in both themes. Seeded stars vary size, brightness and color; sparse warm points,
selective cached glows and brighter foreground stars strengthen depth without
adding a background-animation loop. The documentation body and built-in Starlight
header keep the existing charcoal and warm-neutral themes. Search retains its
native dialog and shortcuts; the native theme selector also stays available in
the mobile splash header. The home-only Hero override falls back to Starlight's
default Hero on other hero routes, including 404. Responsive text, actions and
hubs use normal flow rather than a scaled fixed-size composition. See
[the design contract](DESIGN.md) for typography, focus states and layout.

The decorative home canvases run a local worker particle field with a simple
**Pause animation** / **Resume animation** control. Pause holds the current phase;
reduced motion keeps a static frame. The control is hidden until a frame is ready
and remains hidden for reduced motion or worker failure. The static CSS background
and all server-rendered links remain usable with JavaScript off or initialization
unavailable. Hidden/offscreen fields stop recurring work; pagehide and
back/forward-cache restoration manage the worker lifecycle. The runtime caps
particles at 12,000 desktop and 3,000 mobile, bounds pixel density and transferred
buffers, allows at most one worker request in flight, and limits updates to 30fps.
Both canvases share a 1,200,000 backing-pixel ceiling, DPR at most 1.5 and a
3072px dimension ceiling. The shared position buffer stays within 96KB.
The enlarged galaxy is clipped to the hero. Most stars remain behind the copy;
a sparse foreground layer and thin, short trails cross some text to suggest
depth. Both canvases share one simulation snapshot, ignore pointer events and
cannot spill over lower sections. Four orbital layers turn in 40, 60, 80 and
96 seconds, with continuous 480-second full-field closure. Cached rotations and
harmonics reduce repeated trigonometry. Per-plane culling includes glow/trail
extents; seeded style buckets and six 32px glow sprites are cached once.
These bounds are not a speed benchmark. There is no telemetry panel, GSAP, video,
remote asset, font download or new dependency. Documentation and search results
stay still; existing brief control feedback respects reduced motion.

Native CSS scroll timelines recede the home hero with its text at full opacity,
fading only its two decorative particle planes as they move at different depths.
Task and hub arrows use decorative inline SVG icons rather than font glyphs.
Home body blocks fade near viewport edges and stay opaque through the central
reading area. Reverse scrolling restores
them; focused or targeted sections remain fully visible. Reduced motion and
browsers without scroll-timeline support show still, visible content. These
home-only effects add no scroll listener or recurring JavaScript loop.

`node site/scripts/test_field_model.mjs` checks deterministic finite sampling,
sampling-order independence, wrap continuity, bounds and agreement within 0.01
reference pixels of an independent exact-trigonometric model. The Python site
suite runs this focused check too. With Node 24,
`node site/scripts/benchmark_field.mjs` compares warm CPU math for the same
visual model. It excludes initialization, transfer, scheduling, paint, GPU and
power; it is not a whole-page frame-rate measurement. Larger selective glows
cover more pixels even when their submission count stays similar.

The workflow publishes to `https://scuba-sc.github.io/sco-core/` through the
existing `docs-site` workflow and GitHub Pages' workflow build mode. Pull requests
build and validate a reviewable `sco-docs-site` artifact with read-only permissions;
they never publish. Only a push to `main`, or a manual workflow dispatch targeting
`main`, uploads the validated `site/dist` Pages artifact and deploys it.

The deploy job alone receives `pages: write` and `id-token: write`, uses the
`github-pages` environment restricted to `main`, and serializes deployments without
cancelling an in-progress publication. Checkout retains no credentials. Catalogue
generation, locked dependency installation, search-path sanitization, private
sentinel checks and built-site validation all precede artifact upload. Failed
validation prevents publication. Publication remains a **development preview**;
it does not create or label an SDK release.

## Local build

Prerequisites: Python 3.9+, Clang/C11 with the SDK's 64-bit layout, Node 22.12+
(Node 24 in CI), and npm. From the repository root:

```sh
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
python3 site/scripts/gen.py
(cd site && npm ci && npm run build)
python3 site/scripts/test_site.py --built site/dist
```

For a local preview, run `npm run preview` in `site/`, then open the printed URL
with the `/sco-core/` base. Search is available after building. Generated Markdown,
JSON, dependencies and build output are ignored, not committed.

## Publication boundary

`public-inputs.json` is the explicit engineering-document allowlist. The generator
never recursively copies repository contents, legal/design/release directories,
examples or tests. Evidence links refer to the catalogue's full source commit,
not a moving branch. Links between selected documents resolve to site pages;
other repository links point to pinned source, without importing those files.
Repository images are source links rather than fetched or copied assets.

Navigation is four library hubs generated by `gen.py` into `src/generated-sidebar.json`:
Start & SDK profiles; Language SDKs & cookbooks; Native & service reference; Host
integration & AI agents. Each hub has a landing page at `hubs/<key>/`. Every allowlisted
doc must be assigned to exactly one hub in `gen.py` (`HUB_DOCS`), or generation fails.
Grouping does not move pages: routes such as `guides/releases/sdk-v240/` are unchanged
and tested.

`/natives/` renders `sdk/native_index.json`, which is validated (allowed keys, `_0x` hash
shape, tiers, sources under `src/game/`) and copied unchanged to `native-index.json`.
Rows are server-rendered; one inline script adds subsystem/tier/text filters and resolves
`/natives/?_0x<HASH>` or `#_0x<HASH>` to a scrolled, marked row. Entries carry row names
and readiness only, never patterns or addresses, and nothing is verified in game.

Each stable record ID has one reference page, each service one index. Pages keep
limitations prominent, copy language status labels verbatim and index source
wrapper tokens alongside symbols and service names. Signatures and buffer prose
are catalogue evidence, not inferred parameter semantics. Unspecified behavior
remains unspecified; ABI tests do not prove runtime or in-game behavior.

Every generated page identifies SDK version, game-pack version and source SHA.
By default it is a **development preview**. `gen.py --release-tag sdk-v<version>`
accepts only an existing SDK tag resolving to the exact catalogue source SHA;
passing a label alone cannot turn a preview into a release.

The stdlib tests verify deterministic generation, exact record/service coverage,
status fidelity, immutable source links, generated/built internal links and
publication tokens. A temporary docs copy receives a private-directory filename
sentinel and a sentinel line in an unlisted engineering document; neither may
appear in generated content or the real built output, including gzip-compressed
Pagefind indexes/fragments. Negative arms prove the leak detector catches both
content and filenames. With Node present, `npm ci` must have been run so the
sentinel test can build with the same locked dependencies.

## Search, navigation and snapshot acceptance

Search accepts task phrases such as **spawn a ship**, **read inventory** and
**read container contents**, alongside exact symbols and service names. Visible
weighted task terms point to their canonical contract pages. A bounded local
normalizer removes task filler and corrects one-edit typos in recognized task
vocabulary, including transposed letters such as **spwan ship** and
**read inventroy**. Exact symbols, quoted searches and unknown terms are kept
intact. The editable query and normalization hint let the reader see what was
searched; this is a discovery aid, not an API alias or availability claim.

The labelled **Type** dropdown narrows results to **APIs**, **Guides**, **Languages**,
**Services** or **Overview**. Choose one category at a time; **All categories** clears the category selection without clearing the
query. The Type dropdown and reset wrap above results in narrow panes. Loading and retry messages
cover search initialization, and an empty result state keeps the input and filters
available. All search assets and queries stay local to the built site.

`python3 site/scripts/test_site.py --built site/dist` also drives a fresh headless
Chrome/Chromium through Node's built-in WebSocket/CDP (no added npm package).
Set `CHROME_BIN` when the browser is not in a standard location. A missing browser
is an explicit skipped check, not keyboard/search acceptance. The runner checks:

- Home layout at 360, 390, 430, 600, 820, 1024, 1366, 1440 and 1920px in both
  themes, with one semantic page title, working local links, mobile theme/search
  controls and no document overflow. Landing checks exercise the actual local
  worker, pause/resume, reduced motion, suspension, failures and page lifecycle;
  markup alone does not establish runtime behavior.
- Service, C-symbol, wrapper-token and editorial task-phrase queries against the
  actual Pagefind browser worker, JavaScript, WASM and compressed index. The existing
  seven query cases retain their first-20 acceptance, source, preview, examples,
  capability absence and catalogue language-status checks. Task queries entered
  through the shipped search UI, including the ship/inventory typos above, must
  place the expected canonical contract in the first three results.
- Every Type filter must return only that category, with representative matches.
  Switching categories replaces the previous selection; deselecting searches all.
  Reset restores all categories, and an empty query result keeps usable controls.
  Normalization checks preserve exact symbols, quotes and unknown terms.
- Search shortcut focus, Tab/Enter navigation, Escape closure, focus containment
  and restoration, rapid query/filter changes and reopening. A cold first opening
  with deferred idle initialization must focus the input after Pagefind loads.
  Real long tables/code must fit desktop and narrow widths without document
  overflow. For an additional slow-CPU probe, set `SCO_DOCS_CPU_THROTTLE=6`
  (CDP slowdown).
- No nonlocal page/search requests or script exceptions, all generated/built
  deep links/anchors, ID route stability under record reordering, and refusal of
  retired task-search targets and duplicate IDs. Task phrases are editorial
  discovery terms for canonical contracts, never newly supported API aliases.
- Four static language views (`c`, `cpp`, `csharp`, `lua`) preserve every record's
  status, including unsupported Lua game mutations. These are metadata views,
  not new runtime capabilities or a claim that every method is available.
- Private sentinel exclusion from generated/built files, compressed search and
  captured build logs, plus negative detector tests for plaintext/gzip/filenames.

Search is approximate: an unknown query can return unrelated existing entries.
The test records this behavior and verifies those hits resolve to the same
snapshot; it does **not** assert zero results or treat a search hit as alias
support. Check the symbol and language/capability status. There is no established
historical deployed-site URL inventory in this proof: current canonical-ID
stability is tested, but any eventual old-site alias/redirect migration must be
specified and tested separately. No new private/internal docs are imported to
improve search ranking. The compiler-backed catalogue producer remains the
signature/semantic drift gate; renderer tests alone cannot establish those facts.

See [reference snapshots](../docs/reference-snapshots.md) for source/archive
binding and `python3 site/scripts/test_snapshot.py` for its targeted tests and
one archived C `hello` build. All-example compilation against the intended
candidate, permitted snapshot integration into the canonical release inventory,
and exact-hash maintainer approval remain owed. Neither this sidecar nor the
site artifact is automatically included in SDK ZIPs or approved for publication.

See [agent integration](../docs/agent-integration.md) for the kit-first client
setup and the [MCP walkthrough](../docs/sdk-mcp-tutorial.md) for optional
scaffold/validation calls and native checker steps. Both guides belong to the
Host integration & AI agents hub; neither announces or approves a release.
`python3 tools/sco-core-mcp/test_integration.py` provides the
local initialization/annotations proof. Administrator settings, runner setup and
an observed Copilot code-review tool call remain owed. No Pages deployment,
Copilot settings, setup workflow, release tag, signing or runtime ABI is changed.

## Dependencies and licensing

Direct dependencies are exactly pinned: Starlight 0.42.6, Astro 7.3.8 and
Pagefind 1.5.2 (MIT). The selector parser is overridden to 7.1.6 to avoid its
upstream CPU-exhaustion advisory; the real site build exercises the override.
`package-lock.json` fixes transitive resolutions and integrity
hashes; builds install only with `npm ci`. `dependency-licenses.json` records every
locked dependency's package name, version and declared license, including optional
platform packages. It describes upstream license declarations, not a blanket
legal conclusion. Dependencies are build tooling, not vendored repository files
or SDK archive payload. Repository-authored site code is GPL-3.0-only; built
upstream assets and the adapted Starlight Search component retain their upstream
licenses and attribution in `THIRD_PARTY_NOTICES.md`. No site files enter SDK zips.
Regenerate the inventory with `python3 site/scripts/licenses.py` after an approved
lockfile change; the tests reject inventory drift.

The build remaps absolute builder-path prefixes in Pagefind's prebuilt WASM data
strings to an equal-length neutral prefix. The sanitizer refuses paths outside
the WASM data section and leaves code, lengths and memory offsets unchanged;
negative tests and a real Node WebAssembly compilation exercise that boundary.
This upstream build-metadata adjustment avoids publishing builder machine paths.
