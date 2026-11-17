<!-- Template for the release-block named kit-extract-sh; edit here, then run tools/release_docs.py --write. -->
```sh
mkdir sdk-kit && unzip -q {{archive_combined}} -d sdk-kit
SDK="$PWD/sdk-kit/$(basename {{archive_combined}} .zip)"
test -f "$SDK/sdk-catalogue.json" && echo "SDK ok: $SDK" || echo "not the SDK folder: $SDK"
```
