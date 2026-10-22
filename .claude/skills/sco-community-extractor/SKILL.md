---
name: sco-community-extractor
description: >
  Audits a custom or modded sc-offline / sco-core repository to extract mapped engine
  natives, raw memory offsets, vtable dispatches, string anchors, DataCore schemas,
  and SDK gap requirements into a clean-room handoff report (<YYYYMMDD>-<username>-scubamountreport.md)
  for the sco-core SDK team.
---

# SCO Community Native & SDK Gap Extractor (`sco-community-extractor`)

## Mission & Architecture Context
You are analyzing a community-modified build of **`sc-offline`** and/or **`sco-core`** to help expand the upstream **`sco-core` SDK** (`https://scuba-sc.github.io/sco-core`).

In the upstream architecture:
- **`sco-core`** is the base plain-C ABI (`include/sco_api.h`), C++20/C#/Lua SDK wrapper layer (`include/sco/`, `sdk/`), signature registry (`sco-sigcheck`), and host service provider (`sco.storage`, `sco.ui`, `sco.datacore`, `sco.ipc`, `sco.net`, and Star Citizen `game.*` services).
- **`sc-offline`** is the reference consumer, launcher, and modular plugin showcase (`plugins/*`) built on top of `sco-core`.

When community developers build new features on their own copy of `sc-offline`, they frequently bypass the `sco-core` SDK—placing raw memory reads, vtable calls, pattern scans, custom detours, or runtime table generators directly inside the host `src/` directory, custom plugins, or local `sco-core` patches because the official SDK does not expose those surfaces yet.

Your job is to **audit this workspace**, catalog every game native, vtable slot, struct offset, string anchor, and SDK friction point—whether it maps to an existing `sco-core` service or is completely new—and compile a comprehensive, clean-room Markdown report named:
`<YYYYMMDD>-<username>-scubamountreport.md`

---

## Mandatory Execution Protocol

### Step 1: Initialize the Persistent Scratchpad (DO THIS FIRST)
Before scanning a single source file, create a working scratchpad file in the repository root named **`.scubamount-scratchpad.md`**.
- **Rule:** Never rely on your context window memory alone to hold offsets, vtable indices, or function signatures until the end.
- As you inspect `git log`, `git diff`, `src/`, `plugins/`, `include/`, and `data/`, **append every raw finding immediately to `.scubamount-scratchpad.md`** with exact `file:line` citations, hex offsets, and string literals.
- Only synthesize the final `<YYYYMMDD>-<username>-scubamountreport.md` after your scratchpad audit is 100% complete.

### Step 2: Determine Username & Exact Build Provenance
1. Ask the user for their preferred `<username>` for the report filename (or check `git config user.name` and confirm with the user). Determine today's UTC date as `YYYYMMDD`.
2. Locate the target game installation's **`LIVE/build_manifest.id`** (or build constants in the repo) and extract all four identity fields:
   - Four-part `Version` (e.g., `4.10.197.2878` — never record just `"4.10"` or `"4.10.197"`)
   - `Branch`
   - `RequestedP4ChangeNum` (P4 CL)
   - `BuildDateStamp`
3. Compute or record the **SHA-256 hash of the target `StarCitizen.exe`** binary this build was authored/tested against. (If the binary is unavailable on disk, explicitly record `StarCitizen.exe SHA-256: UNKNOWN`).
4. Inspect git lineage:
   - Identify the base `sc-offline` tag/commit (e.g., `v0.7.8`, `v0.7.8.3`) and base `sco-core` SDK version/commit (e.g., `sdk-v2.3.0`).
   - Run `git status` and `git diff --stat <base>..HEAD` to measure exact commits, files changed, and insertions/deletions.
   - Check if the `sco-core` submodule or vendor directory has local modifications or `.patch` files.

### Step 3: Deep Audit of Both `plugins/` AND Host `src/`
Community forks often keep `plugins/*` clean against `sco_api.h` while hiding hundreds of raw memory dereferences and vtable dispatches inside the host `src/` shell. **You must audit both `plugins/` and `src/` (plus any `.patch` files, code generators, and `.toml` DataCore packs).**

Log the following into `.scubamount-scratchpad.md`:

#### Category A — Existing `sco-core` Surface Touchpoints & Friction
Scan for usage of `sco_api.h`, `include/sco/`, `sc_*.h`, and `tools/sdk-headers-allow.txt`:
- Which `game.*` or `sco.*` services are consumed (`game.actors`, `game.vehicles`, `game.world`, `game.entities`, `game.inventory`, `game.loadout`, `game.gamerules`, `game.creative`, `game.input`, `sco.datacore`, `sco.ui`, `sco.ipc`, `sco.net`)?
- Where did the author have to work around an SDK limitation? (e.g., missing provider sockets like `SetRelocationBackend` or `SetLoadoutBackend`, needing `BestEffort` pack policies, needing deep-clone entity/record modes, or missing parameter/enum registries).
- If they patched `sco-core` directly, record every file and C/C++ signature they added or changed.

#### Category B — Unmapped Engine Natives, Vtables & Raw Bypasses (HIGHEST PRIORITY)
Search the entire codebase for raw pointer arithmetic (`reinterpret_cast`, `uintptr_t`, `*(uint64_t*)(base + 0x...)`), vtable dispatches (`vt[index]`, `*(*(void***)ptr + 0x...)`), IDA/Ghidra byte patterns, MinHook/detour installations, and RVA tables:
1. **Functions & Signatures:**
   - Function name/role, RVA or module offset, byte pattern + mask, calling convention (`__fastcall`, etc.), parameter types, and return type.
2. **Deterministic Anchor Strings (CRITICAL FOR FAST RE-ANCHORING):**
   - For every native function or subsystem, extract any nearby **string literals, CVar names, log format strings, console commands, or error tokens** (e.g., `"Game2.dcb"`, `"DoubleEntityRemovalAttempt"`, `"SPI_Player_OnRespawnLoadoutCompleted"`). The upstream ingestion pipeline uses these strings to re-anchor functions across game patches in `<2 seconds` via raw PE string-to-code xref scanning.
3. **Vtable Slots & Class Layouts:**
   - Engine class name (e.g., `CEntitySystem`, `IPhysicalWorld`, `CZone`, `CDataCoreLoader`, `CItemPortContainer`, `CShipServicesProvider`, `CReputationManager`).
   - Exact vtable slot index and byte offset (e.g., `vt[36]` / `+0x120`, `vt[37]` / `+0x128`, `vt[56]` / `+0x1C0`).
   - Struct member offsets, field sizes, and backing container types (e.g., SwissTable maps, value pools, entity ID arrays).
4. **Enums, Events & Game Actions:**
   - Any mapped `EGameActions`, `EItemType`, gamerules parameters, Subsumption callbacks, or lifecycle events.
5. **DataCore Runtime & TOML Pack Schemas:**
   - How runtime DataCore pools or `.dcb` records are read, mutated, or overridden via `.toml` packs.

### Step 4: Enforce Verification Honesty & Clean-Room Hygiene
1. **Four-State Verification Tagging:** Every single native, offset, vtable slot, and feature in your report MUST be tagged with one of:
   - `VERIFIED_IN_GAME`: Executed and confirmed working inside a live offline Star Citizen session on the stated build.
   - `RESOLVED_STATIC_ONLY`: Signature/offset resolves cleanly against the target binary or type dump, but has not been executed in-game.
   - `EXPERIMENTAL_UNVERIFIED`: Authored, mocked, or hypothesized without binary or in-game confirmation.
2. **Strict Clean-Room & Safety Filtering:**
   - **NEVER** include raw proprietary game binary dumps, full DataCore GUID dumps, player DNA binary blobs, encryption keys, account/telemetry data, or online server/anti-cheat circumvention code in the report.
   - Express all findings strictly as **interoperability facts** (function roles, RVA/pattern locators, vtable indices, struct field offsets, string xrefs, and clean C/C++20 SDK contract proposals).

### Step 4b: Scour Commit History for Head-Start Patterns
Before generating the final report or staging a Draft PR, inspect the repository's git history so any proposed SDK contracts or host adapters match upstream conventions:
1. Run `git log -n 20 --oneline` on `upstream/main` (or `origin/main`) in `sco-core` and/or `sc-offline`.
2. Identify the 2–3 most recent commits that added or extended an SDK contract, host provider socket, or signature row (`git show <sha> --stat`).
3. Inspect one exemplar commit in full (`git show <sha>`) and record the exact file triplet (C11 header, C++20 wrapper, catalogue/test files) in `.scubamount-scratchpad.md` so your proposed contract or Draft PR follows the identical structure.

---

### Step 5: Generate `<YYYYMMDD>-<username>-scubamountreport.md`
Synthesize everything from `.scubamount-scratchpad.md` into `<YYYYMMDD>-<username>-scubamountreport.md` using the exact template below (also available standalone at `report-template.md`):

````markdown
---
report_schema: scubamount-community-handoff-v1
date_utc: "YYYY-MM-DD"
contributor_username: "<username>"
target_game_build:
  version: "<e.g. 4.10.197.2878>"
  branch: "<e.g. sc-alpha-4.10.x>"
  p4_changelist: "<e.g. 12913470>"
  build_date_stamp: "<YYYY-MM-DD or UNKNOWN>"
  starcitizen_exe_sha256: "<64-hex sha256 or UNKNOWN>"
fork_lineage:
  sc_offline_base_ref: "<tag or commit SHA, e.g. v0.7.8.3 / e0f0d8f>"
  sco_core_base_ref: "<tag or commit SHA, e.g. sdk-v2.3.0 / cce4895>"
  head_commit: "<commit SHA>"
  measured_commits_ahead: <int>
  measured_files_changed: <int>
  has_local_sco_core_patch: <true|false>
summary_counts:
  plugins_audited: <int>
  host_src_files_audited: <int>
  raw_memory_reads_found: <int>
  vtable_dispatches_found: <int>
  unique_native_functions_mapped: <int>
  proposed_sdk_additions: <int>
---

# Scubamount SDK & Native Extraction Report — `<username>` (`YYYY-MM-DD`)

## 1. Executive Summary & Architecture Overview
- **What this build adds/modifies:** <High-level summary of custom features, host changes, and plugins>
- **Where engine interactions live:** <Specify how much lives in host `src/*` vs `plugins/*` vs local `sco-core` patches>
- **Verification summary:** <Specify what was tested in-game vs. static/mock only>

---

## 2. Mapped Engine Natives & Function Signatures
| ID | Subsystem / Class | Function Name / Role | RVA / Offset | Byte Pattern / Signature | Calling Conv & Prototype | Anchor Strings / CVars (for `<2s` PE xref) | Source Location (`file:line`) | Verification State |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `NAT-01` | `<e.g. DataCore>` | `<e.g. CDataCoreLoader::InitializeBinary>` | `<0x...>` | `<48 89 5C 24 ...>` | `__fastcall bool(void* thisPtr, ...)` | `"Game2.dcb"` | `src/...:120` | `VERIFIED_IN_GAME` |

---

## 3. Vtable Dispatches & Struct Field Layouts
### 3.1 Vtable Dispatches
| Class / Interface | Vtable Slot (Index & Hex Offset) | Method Role & Signature | Caller Context (`file:line`) | Anchor / Corroboration | Verification State |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `<e.g. CEntitySystem>` | `vt[36]` (`+0x120`) | `GetEntityById(EntityId id)` | `src/host/...:88` | `<string or xref>` | `VERIFIED_IN_GAME` |

### 3.2 Struct & Memory Offsets
| Struct / Class Name | Field Offset (`+0x...`) | Size & Type | Field Purpose / Backing Container | Source Location (`file:line`) | Verification State |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `<e.g. CZone>` | `+0x30` | `uint64_t` | `<Zone parent ptr / transform>` | `src/host/...:210` | `RESOLVED_STATIC_ONLY` |

---

## 4. Events, Enums & DataCore Schemas
- **Lifecycle Events & Callbacks:** <List hooked listeners, e.g., respawn action completion, loadout transitions, zone events>
- **Enums & Action Tables:** <List any discovered `EGameActions`, `EItemType`, CVar parameters, or generated tables>
- **DataCore Live / TOML Override Patterns:** <Document runtime schemas (e.g., SwissTable maps, pool structures, or `.toml` pack value overrides)>

---

## 5. `sco-core` SDK Friction & Proposed API Contracts
### 5.1 Existing `sco-core` Services Used & Workarounds Required
- <Detail which existing `game.*` or `sco.*` services were used from `sco_api.h` and `tools/sdk-headers-allow.txt`, and where SDK limitations forced workarounds>

### 5.2 Proposed Plain-C (`sco_api.h`) & C++20 (`include/sco/`) Contracts
```c
// Provide concrete proposed C structs/function tables or provider sockets
// that would allow this mod's host src/ bypasses to become clean sco-core plugins.
```

---

## 6. Clean-Room & Provenance Attestation
- [ ] All claims are tagged honestly (`VERIFIED_IN_GAME`, `RESOLVED_STATIC_ONLY`, or `EXPERIMENTAL_UNVERIFIED`).
- [ ] No proprietary binary blobs, raw DataCore GUID dumps, player DNA blobs, or encryption keys are embedded in this report.
- [ ] Offline-only scope respected (no live server, account, telemetry, or anti-cheat circumvention).
````

### Step 6: Automated Clean-Room Submission (`gh` PR or Issue)
Once `<YYYYMMDD>-<username>-scubamountreport.md` is written to disk, do **NOT** push the user's private working branch directly if it contains proprietary menu/plugin features. Instead, choose the appropriate automated lane below after confirming with the user:

#### Lane A: Clean-Worktree Draft PR (When you authored clean SDK contract or host adapter code)
If you drafted or extended public-safe SDK headers/wrappers (matching the `git show` commit pattern from Step 4b) alongside your report:
1. Create a temporary, isolated git worktree off `origin/main` so ZERO private plugin/menu files can leak:
   ```bash
   git fetch origin main
   git worktree add ../sco-clean-pr -b feat/sdk-handoff-<YYYYMMDD>-<username> origin/main
   ```
2. Copy or cherry-pick **only** the public-safe SDK contract, catalogue, and test files into `../sco-clean-pr` (never copy private `plugins/*` or custom builder extensions).
3. Verify `git -C ../sco-clean-pr status` and `git -C ../sco-clean-pr diff` to prove no proprietary or private mod code is staged.
4. Commit the clean contract changes, push the isolated branch to the user's fork, and open a Draft PR embedding the generated report as the PR body:
   ```bash
   gh pr create --repo scuba-sc/sco-core --draft \
     --title "feat(sdk): <short capability summary> [community-handoff]" \
     --body-file "<YYYYMMDD>-<username>-scubamountreport.md"
   ```
5. Clean up the temporary worktree (`git worktree remove ../sco-clean-pr`).

#### Lane B: Automated `sdk-gap` Issue (When reporting raw natives, offsets, or SDK gaps only)
If the contributor only has raw offsets, vtable slots, or anchor strings inside their private plugin and has not written the C11/C++20 contract:
1. Verify that `<YYYYMMDD>-<username>-scubamountreport.md` passes all Clean-Room checks in Section 6.
2. Submit the structured report directly as an SDK-gap issue via GitHub CLI:
   ```bash
   gh issue create --repo scuba-sc/sco-core \
     --title "[SDK-Gap] <subsystem/capability summary> (<build_version>)" \
     --label "sdk-gap" \
     --body-file "<YYYYMMDD>-<username>-scubamountreport.md"
   ```

#### Lane C: Offline Fallback (If `gh` CLI is unauthenticated or user prefers manual handoff)
If `gh auth status` fails or the user declines automated submission, leave `<YYYYMMDD>-<username>-scubamountreport.md` in the workspace root and tell the user:
> **"Your report is ready at `<YYYYMMDD>-<username>-scubamountreport.md`. Please attach this `.md` file to a new issue on `scuba-sc/sco-core` (or send it to the SDK team) so their ingestion agent can anchor your natives and expand `sco-core`."**
