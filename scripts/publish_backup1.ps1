# Additive backup correction: no old tags or release assets are replaced.
# Run only with the repository owner's publishing authorization.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ExpectedCommit,
    [string]$ReleaseDirectory = (Join-Path $PSScriptRoot '../artifacts/release-v1.0.0-browser-hmac')
)
$ErrorActionPreference = 'Stop'
$env:GIT_TERMINAL_PROMPT = '0'
$lines = "protocol=https`nhost=github.com`n`n" | & git -c credential.interactive=never credential fill 2>$null
$credential = @{}
foreach ($line in $lines) { if ($line -match '^([^=]+)=(.*)$') { $credential[$Matches[1]] = $Matches[2] } }
if (-not $credential.password) { throw 'Saved GitHub credential unavailable.' }
$headers = @{ Authorization = 'Bearer ' + $credential.password; Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28'; 'User-Agent' = 'Beatbox-Release-Backup' }
$user = Invoke-RestMethod -Uri 'https://api.github.com/user' -Headers $headers
if ($user.login -ne 'Xingchen-stars') { throw 'Account mismatch.' }
$base = 'https://api.github.com/repos/Xingchen-stars/easyinput-beatbox'
$remoteCommit = Invoke-RestMethod -Uri ($base + '/commits/v1.0.0-backup.1') -Headers $headers
if ($remoteCommit.sha -ne $ExpectedCommit) { throw 'Backup correction tag mismatch.' }
$release = Invoke-RestMethod -Uri ($base + '/releases/tags/v1.0.0') -Headers $headers
$directory = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
& (Join-Path $PSScriptRoot 'verify_release.ps1') -ReleaseDirectory $directory -ChecksumFile 'BACKUP1-ASSETS.sha256'
$assets = @(
    @{ name = 'easyinput-beatbox-source-v1.0.0-backup1.zip'; path = (Join-Path $directory 'easyinput-beatbox-source-v1.0.0-backup1.zip') },
    @{ name = 'easyinput-beatbox-history-v1.0.0-backup1.bundle'; path = (Join-Path $directory 'easyinput-beatbox-history-v1.0.0-backup1.bundle') },
    @{ name = 'BACKUP1-ASSETS.sha256'; path = (Join-Path $directory 'BACKUP1-ASSETS.sha256') },
    @{ name = 'v1.0.0-backup1.md'; path = (Join-Path $PSScriptRoot '../docs/releases/v1.0.0-backup1.md') }
)
foreach ($item in $assets) {
    $hash = (Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash.ToLowerInvariant()
    $asset = @($release.assets | Where-Object { $_.name -eq $item.name }) | Select-Object -First 1
    if (-not $asset) {
        $uri = $release.upload_url.Split('{')[0] + '?name=' + [Uri]::EscapeDataString($item.name)
        $asset = Invoke-RestMethod -Method Post -Uri $uri -Headers $headers -ContentType 'application/octet-stream' -InFile $item.path -TimeoutSec 60
    }
    if ($asset.state -ne 'uploaded' -or $asset.size -ne (Get-Item -LiteralPath $item.path).Length -or $asset.digest -ne ('sha256:' + $hash)) {
        throw 'Backup asset mismatch. Existing assets are never replaced.'
    }
    Write-Output "REMOTE SHA256 PASS $($item.name)"
}
$notes = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../docs/releases/v1.0.0.md') -Raw -Encoding UTF8
$tree = 'https://github.com/Xingchen-stars/easyinput-beatbox/blob/v1.0.0-backup.1/'
$notes = $notes.Replace('(../release-v1.0.0-checklist.md)', '(' + $tree + 'docs/release-v1.0.0-checklist.md)').Replace('(v1.0.0-backup1.md)', '(' + $tree + 'docs/releases/v1.0.0-backup1.md)').Replace('(../../artifacts/release-v1.0.0-browser-hmac/README.md)', '(' + $tree + 'artifacts/release-v1.0.0-browser-hmac/README.md)')
$notes = "在线网页：https://xingchen-stars.github.io/easyinput-beatbox/`n`n" + $notes
$body = @{ body = $notes } | ConvertTo-Json
Invoke-RestMethod -Method Patch -Uri ($base + '/releases/' + $release.id) -Headers $headers -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($body)) | Out-Null
$final = Invoke-RestMethod -Uri ($base + '/releases/' + $release.id) -Headers $headers
$final | Select-Object html_url,tag_name,prerelease,@{Name='asset_count';Expression={$_.assets.Count}} | ConvertTo-Json
