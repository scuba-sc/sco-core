<!-- Template for the release-block named download-combined; edit here, then run tools/release_docs.py --write. -->
```sh
gh release download {{tag}} -R scuba-sc/sco-core -p {{archive_combined}} -p SHA256SUMS
grep ' {{archive_combined}}$' SHA256SUMS | sha256sum -c -
```
