# sco-core documentation site

An Astro Starlight site, published at <https://scuba-sc.github.io/sco-core/>. It renders two inputs: the compiler-built SDK contract catalogue (`dist/sdk-catalogue.json`) and the guides listed in [`public-inputs.json`](public-inputs.json). `scripts/gen.py` turns them into Markdown under `src/content/docs/`, a sidebar in `src/generated-sidebar.json` and two downloads, `sdk-catalogue.json` and `native-index.json`. Nothing generated is committed. For the look and layout rules, see [DESIGN.md](DESIGN.md).

## Build locally

You need Python 3.9+, Clang with the SDK's 64-bit layout, Node 22.12 or later (CI uses Node 24) and npm. From the repository root:

```sh
python3 tools/sdk_catalogue.py build --out dist/sdk-catalogue.json --source-sha "$(git rev-parse HEAD)"
python3 site/scripts/gen.py
(cd site && npm ci && npm run build)
python3 site/scripts/test_site.py --built site/dist
```

Run `npm run preview` in `site/` and open the printed URL with the `/sco-core/` base. Search works after a build.

## How publishing works

The `docs-site` workflow builds the site and publishes it to GitHub Pages.

- A pull request builds and validates a `sco-docs-site` artifact with read-only permissions. It never publishes.
- A push to `main`, or a manual run on `main`, resolves the latest published release with `scripts/published_release.py` and passes it to `gen.py --published-metadata`. The validated `site/dist` is then deployed. Only the deploy job has `pages: write` and `id-token: write`.
- Failed validation stops the deploy. The checks include the private-sentinel scan and the built-site tests.
- The home page shows the published release and, in a second box, the development preview built from `main`. `gen.py --release-tag sdk-v<version>` builds a release page instead. It accepts only an existing tag that resolves to the catalogue's source commit.
- Every page carries a one-line banner. `gen.py` also writes the page `versions/` (Versions and evidence). It explains the version numbers, the banner and the "Not run in game." wording once.

Publishing the site does not tag or approve a release.

## What gets published

- `public-inputs.json` is an explicit allowlist of docs. The generator never copies repository contents recursively, and it refuses `docs/legal/`, `docs/design/` and `docs/releases/` except semantic-version release notes.
- Links between allowlisted docs resolve to site pages. Other repository links point at the source commit recorded in the catalogue. Repository images are links, not copied assets.
- Navigation is four hubs: Get started, Language SDKs, API reference, and Hosts, launchers and agents. Each has a page at `hubs/<key>/`. Every allowlisted doc is assigned to exactly one hub in `HUB_DOCS`, or generation fails. Grouping does not change a page's route.
- `/natives/` renders `sdk/native_index.json` after validating its keys, hash format, tiers and source paths, and copies it unchanged to `native-index.json`. Rows are server-rendered. One inline script adds filters and resolves `/natives/?_0x<HASH>` to one row. Entries hold row names and readiness only, never patterns or addresses.
- Each catalogue record has one reference page and each service one index page. Language pages list the status of every contract per language.

## Search

Search runs locally against the static build with Pagefind. A small Starlight Search override keeps the native dialog and adds task-query normalization and a Type filter (APIs, Guides, Languages, Services, Overview).

- Task phrases such as "spawn a ship" or "read inventory" find the canonical contract. The visible, weighted phrases come from `TASK_TERMS` in `gen.py`. They are search aids, not API aliases.
- The normalizer drops filler words and fixes one-letter typos in task words ("spwan ship"). It leaves exact symbols, quoted searches and unknown terms alone.
- Search is approximate, so an unknown query can return unrelated pages. A hit is not a claim that an alias exists.

## Tests

Run these from the repository root. The `.mjs` files are not `node --test` suites. `test_field_model.mjs` runs with `node`, and the browser checks run inside `test_site.py --built`.

| Script | What it guards |
| --- | --- |
| `scripts/test_site.py` | Deterministic generation, record and service coverage, status fidelity, source links, internal links, hubs, language pages, native index, and the leak scan. With `--built site/dist` it also checks the built HTML and drives headless Chrome (set `CHROME_BIN` if needed). A missing browser is a skipped check, not a pass. |
| `scripts/test_published_release.py` | Release discovery and refusal of incomplete or inconsistent release metadata. |
| `scripts/test_snapshot.py` | The reference snapshot tool described in [reference snapshots](../docs/reference-snapshots.md). |
| `scripts/test_field_model.mjs` | The landing particle model: finite, order-independent, wrapping and within 0.01 reference pixels of an exact model. `test_site.py` runs it too. |
| `scripts/test_navigation.mjs`, `test_landing.mjs`, `test_search.mjs` | Real-browser checks. `test_site.py --built` starts `test_navigation.mjs`, which loads the other two. Their header comments list the viewport widths and queries. |

The leak scan puts a sentinel file name and a sentinel line into a temporary docs copy. Neither may appear in generated content or in the built output, including the gzip-compressed Pagefind index.

`node site/scripts/benchmark_field.mjs` (Node 24) times the particle math only. It is not a frame-rate measurement.

## Dependencies and licences

`package.json` pins the direct dependencies: Starlight 0.42.6, Astro 7.4.12 and Pagefind 1.5.2 (MIT). The selector parser is overridden to 7.1.6 because of an upstream CPU-exhaustion advisory. Install only with `npm ci`; `package-lock.json` fixes the transitive versions.

`dependency-licenses.json` lists the declared licence of every locked dependency. It records upstream declarations, not a legal conclusion. Regenerate it with `python3 site/scripts/licenses.py` after a lockfile change; the tests reject drift. Dependencies are build tooling and do not enter SDK archives.

Site code is GPL-3.0-only. Built upstream assets and the adapted Starlight Search component keep their upstream licences, listed in the root `THIRD_PARTY_NOTICES.md`.

The build remaps builder paths inside Pagefind's prebuilt WASM data strings to an equal-length neutral prefix (`scripts/sanitize_search.py`), so no builder path is published. It refuses paths outside the data section and leaves code and offsets unchanged.
