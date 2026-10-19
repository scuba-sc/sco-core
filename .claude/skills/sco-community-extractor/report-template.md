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
