# Evidence for sco-core ca2e640

Copied from the `ip-compliance` workflow run
<https://github.com/scubamount/sco-core/actions/runs/38113298363> (artifact
`ip-compliance-evidence`) for commit `ca2e640` on `main`. These are the files the run wrote, with one
edit: in the three `*-audit.json` notes, the runner's temp path before each SBOM's name is removed
(`tools/ip_audit.py` now writes only the name). The SBOMs and hashes are unchanged; the assessment that uses them is [../../sdk-ip-assessment.md](../../sdk-ip-assessment.md).

| File | What |
|---|---|
| `repo-audit.json` | `tools/ip_audit.py repo`: Gates 1, 2, 4 over 491 tracked files |
| `core-audit.json`, `game-sc-audit.json`, `legacy-audit.json` | `tools/ip_audit.py zip` per profile |
| `sco-sdk-*.spdx.json` | SPDX 2.3 SBOMs, bound to the commit and each zip's sha256 |
| `SHA256SUMS` | The three zips' hashes. Two CI builds matched each other, and a local macOS build matched these |

Same commit, all passed: `sdk` (run 38113298231), `cmake` (run 38113298326, including
`kernel-only`) and `test` (run 38113298350, `tools/test.sh` with ASan, UBSan and TSan).
sc-offline's consumer build: run 38112659589 (PR #104: Windows build, bridges, sdk-headers).

Nothing here is confidential. Contributor or legal correspondence, if any, is kept out of the
repository (IP_POLICY.md).
