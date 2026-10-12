# Security policy

## Supported versions

| Version | Security support |
|---|---|
| `sdk-v2.0.0` / plugin ABI 1.x | Supported release line for security reports and fixes. The current frozen plugin ABI is 1.1. |
| Older SDK release lines | No separate maintenance commitment; report issues that also affect the supported line. |

SDK packaging versions and plugin ABI versions are different coordinates; support
does not certify every game build or game-service behavior. See the
[sdk-v2.0.0 release notes](docs/releases/sdk-v2.0.0.md) and
[ABI compatibility rules](docs/api-v1.md#compatibility).

## Reporting a vulnerability privately

Use [GitHub Security Advisories for scubamount/sco-core](https://github.com/scubamount/sco-core/security/advisories).
When **Report a vulnerability** is available on the repository's
[Security page](https://github.com/scubamount/sco-core/security), submit a private
report through GitHub private vulnerability reporting. Do not put exploit details,
credentials, personal data, proprietary files or game dumps in public issues/PRs.

Private vulnerability reporting is **currently disabled** (checked during this
policy's preparation). The owner must enable it before the public private-report
intake button is available; this PR does not change that setting. GitHub Security
Advisories are the intended private coordination/publication route, **not a claim
that a non-collaborator can open a private advisory while intake is disabled**.
If no private-report button is offered, wait for the owner to provide that GitHub
intake rather than publishing sensitive details in an issue. Repository
collaborators can coordinate through a private draft advisory.

Include the affected release/commit, platform, component, minimal synthetic
reproduction, impact and any suggested mitigation. Prefer project-authored
fixtures: no proprietary executable, memory/asset dump, personal identifier or
live secret. The maintainer coordinates a fix and disclosure through GitHub;
there is no promised response time or automatic bounty.

## Scope and trust model

sco-core is an **offline-only** framework. Supported reports include issues in
its host/loader, SDK tooling, documented local IPC and private co-presence
boundaries, handling of untrusted manifests/bytes, and violations of documented
security or disclosure guarantees. See [Plugin rules](sdk/docs/plugin-rules.md)
and [local IPC hostile-peer rules](docs/ipc.md#trust-a-hostile-peer).

**Native plugins run in process with the host's full privileges.** They are
trusted native code, not sandboxed extensions. Host-entered callback guards offer
fault attribution and withdrawal, not native-memory security isolation or safe
recovery. Cached tables, unmanaged threads and direct OS calls are not intercepted;
restart after a native fault. Exact contract:
[Native trust and supported usage](sdk/docs/plugin-rules.md#native-trust-and-supported-usage)
and [Crash containment](docs/plugins.md#crash-containment).
A malicious native plugin exercising those already-granted privileges is not,
by itself, a sandbox escape. A breach of a documented boundary still merits a report.

Lua's sandbox and no-code data packs have different contracts; do not infer their
restrictions apply to native plugins. An SDK service name, read-only MCP
annotation or retained DLL is not proof of safe execution or recovery.

The [runtime MCP design](docs/design/runtime-mcp.md) is a proposal, **not implemented**.
The existing MCP server remains catalogue-only. Future runtime admission requires
explicit host/client opt-in, positive offline policy, authenticated same-PC IPC,
metadata minimization and a verified protected audit boundary. No guarantee against
an administrator/elevated agent or compromised native host is claimed.

## Out of scope

- Official/public online play, CIG server or account/entitlement interaction.
- Anti-cheat bypass/circumvention, signature-check bypass, cheating, or instructions
  for forcing the game's host type/network context.
- Treating full-rights native plugins as sandboxed, or treating native crash
  continuation as a security guarantee.

These are scope restrictions, not authorization to test live services or anti-cheat.
Report supported-component defects using offline synthetic evidence only.
