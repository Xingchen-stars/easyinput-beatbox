[CmdletBinding()]
param(
    [string]$ReleaseDirectory = $(if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'SHA256SUMS.txt')) {
        $PSScriptRoot
    } else { Join-Path $PSScriptRoot '../artifacts/release-v1.0.0-browser-hmac' }),
    [string]$ChecksumFile = 'SHA256SUMS.txt'
)
$ErrorActionPreference = 'Stop'
$directory = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$checksumPath = Join-Path $directory $ChecksumFile
$count = 0
foreach ($line in Get-Content -LiteralPath $checksumPath) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
    if ($line -notmatch '^([0-9a-fA-F]{64})\s+\*?(.+)$') { throw 'Invalid checksum line.' }
    $expected = $Matches[1].ToLowerInvariant()
    $relative = $Matches[2]
    if ([IO.Path]::IsPathRooted($relative)) { throw 'Absolute checksum path is not allowed.' }
    $target = [IO.Path]::GetFullPath((Join-Path $directory $relative))
    $prefix = $directory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Checksum target escapes release directory.'
    }
    $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "SHA-256 mismatch: $relative" }
    Write-Output "PASS $relative"
    $count++
}
if ($count -eq 0) { throw 'No files were checked.' }
Write-Output "Verified $count files. No hardware was accessed."
