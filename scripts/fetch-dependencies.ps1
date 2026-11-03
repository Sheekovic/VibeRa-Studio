$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
$archives = Join-Path $root 'vendor/archives'
$sources = Join-Path $root 'vendor/source'
New-Item -ItemType Directory -Path $archives,$sources -Force | Out-Null
foreach ($dependency in $lock.dependencies) {
    $archive = Join-Path $archives $dependency.archive
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -Uri $dependency.url -OutFile $archive -UseBasicParsing -TimeoutSec 120
    }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $dependency.sha256) { throw "Archive checksum mismatch: $($dependency.name). Existing archive was preserved." }
    $source = Join-Path $sources $dependency.sourceDirectory
    if (-not (Test-Path -LiteralPath $source)) { Expand-Archive -LiteralPath $archive -DestinationPath $sources }
    Write-Output "Verified $($dependency.name) $($dependency.revision)"
}
& (Join-Path $PSScriptRoot 'apply-dependency-patches.ps1')
