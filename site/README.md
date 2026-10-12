# Public documentation site proof

Astro Starlight renders the SDK contract catalogue and explicitly selected public
engineering guides. Starlight's built-in Pagefind search runs locally against the
static build; no hosted search service is configured.

The workflow publishes to `https://scubamount.github.io/sco-core/` through the
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
upstream assets retain their upstream licenses. No site files enter SDK zips.
Regenerate the inventory with `python3 site/scripts/licenses.py` after an approved
lockfile change; the tests reject inventory drift.

The build remaps absolute builder-path prefixes in Pagefind's prebuilt WASM data
strings to an equal-length neutral prefix. The sanitizer refuses paths outside
the WASM data section and leaves code, lengths and memory offsets unchanged;
negative tests and a real Node WebAssembly compilation exercise that boundary.
This upstream build-metadata adjustment avoids publishing builder machine paths.
