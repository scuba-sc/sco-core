<!-- Template for the release-block named kit-download-ps; edit here, then run tools/release_docs.py --write. -->
```powershell
gh release download {{download_tag}} -R scuba-sc/sco-core -p '{{archive_combined}}' -p SHA256SUMS
if ($LASTEXITCODE -ne 0) { throw 'SDK download failed' }
$expected = ((Get-Content SHA256SUMS | Where-Object { $_ -match (' ' + [regex]::Escape('{{archive_combined}}') + '$') }) -split '\s+')[0]
if (!$expected -or (Get-FileHash {{archive_combined}} -Algorithm SHA256).Hash -ine $expected) { throw 'SHA256 mismatch; stop' }
Expand-Archive {{archive_combined}} -DestinationPath sdk-kit
$SDK = (Join-Path (Resolve-Path sdk-kit).Path ([IO.Path]::GetFileNameWithoutExtension('{{archive_combined}}'))) -replace '\\', '/'
if (!(Test-Path "$SDK/sdk-catalogue.json")) { throw 'Not the SDK folder' }
```
