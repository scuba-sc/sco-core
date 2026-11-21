<!-- Template for the release-block named kit-download-sh; edit here, then run tools/release_docs.py --write. -->
```sh
set -e
gh release download {{download_tag}} -R scuba-sc/sco-core -p '{{archive_combined}}' -p SHA256SUMS
grep ' {{archive_combined}}$' SHA256SUMS | sha256sum -c -
mkdir sdk-kit && unzip -q {{archive_combined}} -d sdk-kit
SDK="$PWD/sdk-kit/$(basename {{archive_combined}} .zip)"
test -f "$SDK/sdk-catalogue.json"
```
