# External write: run only when the repository owner has authorized publishing.
# Reuses Git Credential Manager; never saves or prints its token.
[CmdletBinding()]
param(
    [string]$Owner = 'Xingchen-stars',
    [string]$Repository = 'easyinput-beatbox',
    [string]$Tag = 'v1.0.0',
    [Parameter(Mandatory = $true)][string]$ExpectedCommit,
    [string]$ReleaseDirectory = (Join-Path $PSScriptRoot '../artifacts/release-v1.0.0-browser-hmac'),
    [string]$NotesFile = (Join-Path $PSScriptRoot '../docs/releases/v1.0.0.md')
)
$ErrorActionPreference = 'Stop'
$env:GIT_TERMINAL_PROMPT = '0'
$credentialLines = "protocol=https`nhost=github.com`n`n" | & git -c credential.interactive=never credential fill 2>$null
$credential = @{}
foreach ($line in $credentialLines) {
    if ($line -match '^([^=]+)=(.*)$') { $credential[$Matches[1]] = $Matches[2] }
}
if (-not $credential.password) { throw 'No saved GitHub credential available.' }
$headers = @{
    Authorization = 'Bearer ' + $credential.password
    Accept = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent' = 'Beatbox-Release-Backup'
}
$base = "https://api.github.com/repos/$Owner/$Repository"
$user = Invoke-RestMethod -Uri 'https://api.github.com/user' -Headers $headers
if ($user.login -ne $Owner) { throw 'Authenticated account does not match repository owner.' }
$repo = Invoke-RestMethod -Uri $base -Headers $headers
if ($repo.owner.login -ne $Owner -or -not $repo.permissions.push) { throw 'Target repository write permission mismatch.' }
$commit = Invoke-RestMethod -Uri "$base/commits/$Tag" -Headers $headers
if ($commit.sha -ne $ExpectedCommit) { throw 'Remote fixed tag does not match expected local commit.' }
$release = $null
try { $release = Invoke-RestMethod -Uri "$base/releases/tags/$Tag" -Headers $headers }
catch { if ([int]$_.Exception.Response.StatusCode -ne 404) { throw } }
if (-not $release) {
    $body = @{
        tag_name = $Tag
        target_commitish = $ExpectedCommit
        name = 'Beatbox v1.0.0 - Browser Key / Recoverable Backup'
        body = (Get-Content -LiteralPath $NotesFile -Raw -Encoding UTF8)
        draft = $false
        prerelease = $true
        generate_release_notes = $false
    } | ConvertTo-Json -Depth 5
    $release = Invoke-RestMethod -Method Post -Uri "$base/releases" -Headers $headers -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($body))
}
$directory = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
& (Join-Path $PSScriptRoot 'verify_release.ps1') -ReleaseDirectory $directory
& (Join-Path $PSScriptRoot 'verify_release.ps1') -ReleaseDirectory $directory -ChecksumFile 'RELEASE-ASSETS.sha256'
$assetNames = @(
    'easyinput_beatbox_v1.0.0_browser_hmac.bin',
    'easyinput-beatbox-firmware-v1.0.0-browser-hmac.zip',
    'easyinput-beatbox-web-v1.0.0.zip',
    'easyinput-beatbox-source-v1.0.0.zip',
    'easyinput-beatbox-history-v1.0.0.bundle',
    'SHA256SUMS.txt', 'RELEASE-ASSETS.sha256', 'manifest.json', 'flashing-and-recovery.md'
)
$uploadBase = $release.upload_url.Split('{')[0]
foreach ($name in $assetNames) {
    $path = Join-Path $directory $name
    $localHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $asset = @($release.assets | Where-Object { $_.name -eq $name }) | Select-Object -First 1
    if (-not $asset) {
        $uploadUri = $uploadBase + '?name=' + [Uri]::EscapeDataString($name)
        $asset = Invoke-RestMethod -Method Post -Uri $uploadUri -Headers $headers -ContentType 'application/octet-stream' -InFile $path -TimeoutSec 60
    }
    if ($asset.state -ne 'uploaded' -or $asset.size -ne (Get-Item -LiteralPath $path).Length) {
        throw "Remote asset size/state mismatch: $name"
    }
    if ($asset.digest -ne ('sha256:' + $localHash)) {
        throw "Remote asset digest mismatch: $name. Existing assets are never deleted or replaced."
    }
    Write-Output "REMOTE SHA256 PASS $name"
}
$finalRelease = Invoke-RestMethod -Uri "$base/releases/$($release.id)" -Headers $headers
$finalRelease | Select-Object html_url,tag_name,prerelease,@{Name='asset_count';Expression={$_.assets.Count}} | ConvertTo-Json
