# SDK consumer requirements report

This optional developer tool compares an explicit list of method/language needs with one compiler-built schema-2 catalogue and its exact registry. It ships beside the discovery MCP in the combined kit, the game-sc overlay and the host-source-sc archive. Core and core-source alone do not carry the catalogue or this workflow. SDK CI tests the tool and its extracted use; consumers are not required to create a declaration to scaffold, explore, build or load a plugin. It does not select a catalogue by version or timestamp, install an SDK, connect to a host, or run the game.

Build a catalogue at the desired source revision using `tools/sdk_catalogue.py build --out catalogue.json --source-sha <full-40-character-commit-sha>`. Record the SHA-256 of the **exact bytes** of that file and write a requirements JSON file, for example:

```json
{
  "schema_version": 1,
  "source_sha": "0123456789abcdef0123456789abcdef01234567",
  "catalogue_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "items": [
    {"id": "api-query-service", "language": "c"},
    {"id": "some-stable-method-id", "language": "lua", "accept_restricted": true}
  ]
}
```

Replace the sample coordinates and IDs with those from the desired catalogue. Languages are `c`, `cpp`, `csharp`, and `lua`. There must be 1–512 items; each ID/language pair must be unique. `accept_restricted` is an optional boolean acknowledging the exposed subset or adaptation for that item. Read its source evidence and contract before accepting it. The flag is ignored for nonrestricted mappings and cannot make an unsupported mapping pass. Unknown keys, malformed coordinates, duplicate JSON keys and invalid items are refused. Inputs must be regular files; limits are 256 KiB for requirements, 16 MiB for the catalogue, and 1 MiB for the registry.

```sh
python3 tools/sdk_consumer_requirements.py \
  --requirements requirements.json --catalogue catalogue.json \
  --registry sdk/status_registry.json > report.json
```

The tool validates the catalogue and registry with the source-tree MCP validators. It writes the hashed catalogue bytes to a closed temporary snapshot before validation, allowing the validator to reopen it on Windows. The requirements bind the catalogue's exact SHA-256 and full source SHA; the catalogue must bind the supplied registry's exact SHA-256. The source SHA is a declared coordinate compared between inputs; the tool does not independently inspect Git or authenticate either input. A mismatch, malformed input, or schema-1 catalogue exits 2 and emits no report on stdout. A complete report with an unknown ID, unsupported mapping, or unacknowledged restricted mapping exits 1; a report whose requested language mappings are supported or explicitly accepted exits 0. Exit 0 only describes this static comparison, not deployability.

The deterministic receipt includes input hashes, source coordinates, table version (packed major/minor), member extent, language mapping evidence and test references. References identify source material, not tests performed by this tool. Entrypoints have null layout coordinates. Static capability rows are assigned only when registry service, header, table version and explicit method list match. An empty method list is never attributed to a method; absent or ambiguous matches are `unestablished`. Matching uses the complete member suffix: for example, registry method `snapshot` does not imply a match to `base.snapshot`. As in the MCP, `include/sc_` headers are in game-status scope; other headers and entrypoints are `not_applicable`. Registry defaults and evidence levels are structural source claims; ON does not prove provider or runtime availability. Runtime and game checks are `not_run`, and provider and release status are `unestablished` regardless of the static result.

Run the focused tests with `python3 tools/test_sdk_consumer_requirements.py`.

Duplicate registry service rows matching a catalogue service/header are malformed,
including rows with different versions or conflicting defaults. The shared
validator refuses the entire input with exit 2 and no stdout report before any
capability row is assigned.

## Optional extracted-kit workflow

Start with a deliberately selected, checksum-verified kit. An explicitly selected
local development archive is also usable; keep it labelled unpublished. Record
its expected full source SHA and catalogue SHA-256 independently of the values
you are about to compare. A source coordinate, matching file hash or passing
report does not supply source rights, release approval or a live provider.

| Input | Combined kit or composed core + game-sc | Host-source-sc |
| --- | --- | --- |
| Tool | `tools/sdk_consumer_requirements.py` | same |
| Catalogue | `sdk-catalogue.json` | same |
| Registry | `status_registry.json` | `sdk/status_registry.json` |
| Example declaration | `examples/container_reader/consumer-requirements.template.json` | `sdk/examples/container_reader/consumer-requirements.template.json` |

Compose core and game-sc only after checking the overlay's `COMPATIBILITY.json.game-sc`
against the exact base archive hash. The report does not replace archive inventory
or composition checks. Run from an empty consumer directory; it needs only Python
3 and the extracted files, not Git, a compiler, a checkout or an MCP process.

The supplied declaration describes the actual `container_reader` C example:
three plugin entrypoints, command registration, service discovery and the copied
container snapshot operation. Its two identity fields are deliberately null.
The unbound template is refused, rather than silently selecting the installed
kit. Make a consumer-owned copy after checking the explicit expected identities:

```sh
# Set KIT and TEMPLATE to the selected extracted paths from the table above.
# Set EXPECTED_SOURCE_SHA and EXPECTED_CATALOGUE_SHA256 from your selected input receipt.
python3 - "$KIT" "$TEMPLATE" "$EXPECTED_SOURCE_SHA" "$EXPECTED_CATALOGUE_SHA256" <<'PY'
import hashlib, json, pathlib, re, sys
kit, template = map(pathlib.Path, sys.argv[1:3])
source, digest = sys.argv[3:5]
if not re.fullmatch(r"[0-9a-f]{40}", source) or not re.fullmatch(r"[0-9a-f]{64}", digest):
    raise SystemExit("Supply explicit expected source and catalogue identities")
raw = (kit / "sdk-catalogue.json").read_bytes()
if hashlib.sha256(raw).hexdigest() != digest or json.loads(raw)["source_sha"] != source:
    raise SystemExit("Selected catalogue does not match the expected identities")
declaration = json.loads(template.read_text(encoding="utf-8"))
declaration.update(source_sha=source, catalogue_sha256=digest)
with pathlib.Path("consumer-requirements.json").open("x", encoding="utf-8") as output:
    output.write(json.dumps(declaration, indent=2) + "\n")
PY

# Set REGISTRY to the selected kit's path from the table above.
python3 -I "$KIT/tools/sdk_consumer_requirements.py" \
  --requirements consumer-requirements.json \
  --catalogue "$KIT/sdk-catalogue.json" --registry "$REGISTRY" > report.json
```

An exit-zero report for this example means its C contracts are present. The
container snapshot row still records `OFF` / `synthetic_only`; the example's
runtime service lookup and full-member checks must still refuse absent providers.
This workflow does not execute the example or verify a container in a game.
An unsupported language or unknown method exits 1 with a complete report; a
wrong expected source, altered catalogue or mismatched registry exits 2 with
no stdout report. Read the exit code: shell redirection can leave an empty file.
