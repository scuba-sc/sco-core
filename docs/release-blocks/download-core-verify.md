<!-- Template for the release-block named download-core-verify; edit here, then run tools/release_docs.py --write. -->
```sh
env -u GITHUB_TOKEN -u GH_TOKEN gh release download {{download_tag}} -R scuba-sc/sco-core -p '{{archive_core}}' -p SHA256SUMS
python3 - <<'PY'
import hashlib
from pathlib import Path
import zipfile
archive = Path("{{archive_core}}")
entries = dict(line.split(None, 1)[::-1] for line in Path("SHA256SUMS").read_text().splitlines() if line.strip())
if hashlib.sha256(archive.read_bytes()).hexdigest() != entries[archive.name]:
    raise SystemExit("SHA256 mismatch: stop; do not extract or build")
if Path(archive.stem).exists():
    raise SystemExit("Extraction destination already exists; choose an empty directory")
with zipfile.ZipFile(archive) as sdk:
    sdk.extractall(".")
print(archive.name + ": SHA256 OK")
PY
```
