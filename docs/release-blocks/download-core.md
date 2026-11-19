<!-- Template for the release-block named download-core; edit here, then run tools/release_docs.py --write. -->
```sh
gh release download {{download_tag}} -R scuba-sc/sco-core -p {{archive_core}} -p SHA256SUMS
grep ' {{archive_core}}$' SHA256SUMS | sha256sum -c -
```
