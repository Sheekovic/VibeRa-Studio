param([string]$Executable = (Join-Path $PSScriptRoot '../dist/VibeRa Studio/VibeRa Studio.exe'))
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$file = Get-Item -LiteralPath $Executable
$sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceCommit=$null
$sourceOrigin='working source (temporary compilation)'
$source=Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
$provenance=Join-Path $file.DirectoryName 'provenance.json'
if (Test-Path -LiteralPath $provenance) {
    $manifest=Get-Content -LiteralPath $provenance -Raw | ConvertFrom-Json
    $row=@($manifest.files | Where-Object {$_.path -eq $file.Name})
    if ($row.Count -ne 1 -or $row[0].sha256 -ne $sha256 -or $manifest.sourceCommit -notmatch '^[0-9a-f]{40}$') {throw 'Executable does not match recorded source/binary identity'}
    $sourceCommit=$manifest.sourceCommit
    $sourceLines=& git -c "safe.directory=$($root.Replace('\','/'))" -C $root show "${sourceCommit}:CMakeLists.txt"
    if ($LASTEXITCODE -ne 0) {throw 'Recorded application source is unavailable'}
    $source=$sourceLines -join "`n"
    $sourceOrigin='verified package manifest / recorded Git implementation'
}
$match = [regex]::Match($source, 'project\(VibeRa VERSION ([0-9.]+)')
if (-not $match.Success) { throw 'Cannot read canonical application version.' }
$expected = $match.Groups[1].Value
$info = $file.VersionInfo
if ($info.ProductVersion -ne $expected -or $info.FileVersion -ne $expected) {
    throw "Windows executable version mismatch: expected $expected, product $($info.ProductVersion), file $($info.FileVersion)"
}
[ordered]@{
    expectedVersion = $expected
    productVersion = $info.ProductVersion
    fileVersion = $info.FileVersion
    applicationSha256 = $sha256
    sourceCommit = $sourceCommit
    sourceOrigin = $sourceOrigin
    evidence = 'Actual Windows PE version resource; application/MCP/UI checked separately'
} | ConvertTo-Json
