<!-- Template for the release-block named kit-extract-ps; edit here, then run tools/release_docs.py --write. -->
```powershell
Expand-Archive {{archive_combined}} -DestinationPath sdk-kit
$SDK = (Join-Path (Resolve-Path sdk-kit).Path ([IO.Path]::GetFileNameWithoutExtension('{{archive_combined}}'))) -replace '\\', '/'
if (Test-Path "$SDK/sdk-catalogue.json") { "SDK ok: $SDK" } else { "not the SDK folder: $SDK" }
```
