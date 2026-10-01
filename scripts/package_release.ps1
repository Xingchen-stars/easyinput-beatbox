[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$FirmwareImage,
    [Parameter(Mandatory = $true)][string]$BuildFirmwareDirectory,
    [string]$ReleaseDirectory = (Join-Path $PSScriptRoot '../artifacts/release-v1.0.0-browser-hmac')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$buildSource = (Resolve-Path -LiteralPath $BuildFirmwareDirectory).Path
$expectedHash = 'dea3720fbe7e20b45eaed2784bea83cc235f8046dd11ec80223afb1b1fe784f8'
$imageHash = (Get-FileHash -LiteralPath $FirmwareImage -Algorithm SHA256).Hash.ToLowerInvariant()
if ($imageHash -ne $expectedHash) { throw 'This is not the fixed v1.0.0 image.' }
if ((Get-Item -LiteralPath $FirmwareImage).Length -ne 985264) { throw 'Wrong firmware size.' }
$compiledImage = Join-Path $buildSource 'build/easyinput_beatbox.bin'
if ((Get-FileHash -LiteralPath $compiledImage -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'Build directory image does not match the fixed image.'
}
$sourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $repo 'firmware/main') -Recurse -File)
if ($sourceFiles.Count -ne 42) { throw 'The fixed main source inventory changed.' }
$firmwareRoot = Join-Path $repo 'firmware'
foreach ($file in $sourceFiles) {
    $relative = $file.FullName.Substring($firmwareRoot.Length + 1)
    $built = Join-Path $buildSource $relative
    if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash) {
        throw "Build source mismatch: $relative"
    }
}
foreach ($relative in @('CMakeLists.txt', 'sdkconfig.defaults', 'partitions.csv')) {
    $local = Join-Path $firmwareRoot $relative
    if ((Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $buildSource $relative) -Algorithm SHA256).Hash) {
        throw "Build configuration source mismatch: $relative"
    }
    $sourceFiles += Get-Item -LiteralPath $local
}
Write-Output "Matched $($sourceFiles.Count) firmware source/configuration files with the actual build."
$buildDescription = Get-Content -LiteralPath (Join-Path $buildSource 'build/project_description.json') -Raw | ConvertFrom-Json
$binaryName = 'easyinput_beatbox_v1.0.0_browser_hmac.bin'
$copies = @(
    @($FirmwareImage, $binaryName),
    @((Join-Path $firmwareRoot 'partitions.csv'), 'partitions.csv'),
    @((Join-Path $buildSource 'sdkconfig'), 'sdkconfig.build'),
    @((Join-Path $buildSource 'dependencies.lock'), 'dependencies.lock'),
    @((Join-Path $repo 'docs/flashing-and-recovery.md'), 'flashing-and-recovery.md'),
    @((Join-Path $PSScriptRoot 'verify_release.ps1'), 'verify_release.ps1'),
    @((Join-Path $repo 'LICENSE'), 'LICENSE'),
    @((Join-Path $buildDescription.idf_path 'LICENSE'), 'LICENSE-ESP-IDF.txt'),
    @((Join-Path $buildSource 'managed_components/espressif__led_strip/LICENSE'), 'LICENSE-led-strip.txt'),
    @((Join-Path $repo 'docs/distribution-notices.md'), 'THIRD-PARTY-NOTICES.md'),
    @((Join-Path $repo 'assets/samples/tr707/README.md'), 'SAMPLE-PROVENANCE.md')
)
foreach ($pair in $copies) { Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $release $pair[1]) }
$utf8 = New-Object System.Text.UTF8Encoding($false)
$sourceChecksums = foreach ($file in ($sourceFiles | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($repo.Length + 1).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
[IO.File]::WriteAllLines((Join-Path $release 'FIRMWARE-SOURCE.sha256'), $sourceChecksums, $utf8)
$manifest = [ordered]@{
    version = '1.0.0'
    variant = 'browser-key-hmac-whitebox-backup'
    saved_date = '2026-10-01'
    release_status = 'prerelease-recoverable-backup'
    hardware = [ordered]@{ product = 'EasyInput V2.0'; chip = 'esp32s3'; flash_bytes = 16777216; psram_bytes = 8388608 }
    firmware = [ordered]@{
        file = $binaryName; bytes = 985264; sha256 = $expectedHash
        address = '0x430000'; partition = 'beatbox'; partition_bytes = 2097152
        idf = '5.5.5'; build_command = 'idf.py -D COMPONENTS=main build'
        main_source_files_matched = 42; source_config_files_matched = $sourceFiles.Count
        source_hash_format = 'raw bytes; firmware Git attributes preserve line endings'
    }
    update_scope = @('beatbox application only')
    excluded = @('factory', 'bootloader', 'partition-table writes', 'NVS', 'private browser keys', 'raw device logs')
    verified = @('source match', 'build', 'image checksum', 'flash hash', 'normal boot protocol v3', 'BLE advertisement', 'S7-window enrollment', 'known-browser HMAC reconnection', 'USB fallback user confirmation', '27 web tests', 'TypeScript', 'Vite build')
    pending = @('automatic reconnect', 'trust after power cycle', 'all physical key/web feedback', 'BPM presets and A/B/Fill regression', 'three-credential rotation', 'long duration and range')
    security_boundary = 'Application HMAC authorization; enrollment and ordinary traffic are not BLE link encrypted.'
    repository = 'https://github.com/Xingchen-stars/easyinput-beatbox'
    tag = 'v1.0.0'
    licenses = [ordered]@{ beatbox = 'MIT'; easyinput_factory = 'PolyForm-Noncommercial-1.0.0' }
}
[IO.File]::WriteAllText((Join-Path $release 'manifest.json'), ($manifest | ConvertTo-Json -Depth 8) + "`n", $utf8)
$packageNames = @($binaryName, 'partitions.csv', 'flash_args.beatbox-update', 'sdkconfig.build', 'dependencies.lock',
    'FIRMWARE-SOURCE.sha256', 'manifest.json', 'README.md', 'flashing-and-recovery.md', 'verify_release.ps1',
    'LICENSE', 'LICENSE-ESP-IDF.txt', 'LICENSE-led-strip.txt', 'THIRD-PARTY-NOTICES.md', 'SAMPLE-PROVENANCE.md')
$checksums = foreach ($name in $packageNames) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $release $name) -Algorithm SHA256).Hash.ToLowerInvariant(), $name
}
[IO.File]::WriteAllLines((Join-Path $release 'SHA256SUMS.txt'), $checksums, $utf8)
& (Join-Path $PSScriptRoot 'verify_release.ps1') -ReleaseDirectory $release
$firmwareZip = Join-Path $release 'easyinput-beatbox-firmware-v1.0.0-browser-hmac.zip'
$webZip = Join-Path $release 'easyinput-beatbox-web-v1.0.0.zip'
foreach ($zip in @($firmwareZip, $webZip)) {
    if (Test-Path -LiteralPath $zip) { throw "Refusing to overwrite an existing release archive: $zip" }
}
$zipFiles = @($packageNames | ForEach-Object { Join-Path $release $_ }) + (Join-Path $release 'SHA256SUMS.txt')
Compress-Archive -LiteralPath $zipFiles -DestinationPath $firmwareZip -CompressionLevel Optimal
$webStage = Join-Path $repo ('.cache/release-web-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $webStage -Force | Out-Null
Copy-Item -Path (Join-Path $repo 'app/dist/*') -Destination $webStage -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $webStage 'LICENSE')
Copy-Item -LiteralPath (Join-Path $repo 'app/src/assets/icons/drums/LICENSE') -Destination (Join-Path $webStage 'LICENSE-qlementine-icons.txt')
Copy-Item -LiteralPath (Join-Path $repo 'app/node_modules/lucide/LICENSE') -Destination (Join-Path $webStage 'LICENSE-lucide.txt')
Copy-Item -LiteralPath (Join-Path $repo 'docs/distribution-notices.md') -Destination (Join-Path $webStage 'THIRD-PARTY-NOTICES.md')
Compress-Archive -Path (Join-Path $webStage '*') -DestinationPath $webZip -CompressionLevel Optimal
Write-Output "Created firmware and web ZIPs. Source ZIP and Git bundle must be created from the final tag."
