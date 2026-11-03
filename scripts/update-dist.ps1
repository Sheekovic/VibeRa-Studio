# Owner-requested quick refresh of dist/VibeRa Studio from the latest compile-only build
# (2026-11-03). Copies the app, worker, Content, CoProducer and the skill; keeps everything
# else in place. This is NOT a qualified release: provenance.json still describes the last
# qualified package, and QUICK-UPDATE.txt records the untested source commit.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist\VibeRa Studio'
$build = Join-Path $root 'build\windows\VibeRa_artefacts\Release'
$running = Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($dist, [StringComparison]::OrdinalIgnoreCase) }
if ($running) {
    throw "Close VibeRa Studio first (running from dist: PID $($running.ProcessId -join ', '))."
}
if (-not (Test-Path (Join-Path $build 'VibeRa Studio.exe'))) { throw "No build at $build; run scripts/build.ps1 -CompileOnly first." }
foreach ($file in 'VibeRa Studio.exe', 'VibeRa Plugin Worker.exe') {
    Copy-Item -LiteralPath (Join-Path $build $file) -Destination (Join-Path $dist $file) -Force
}
foreach ($folder in 'Content', 'CoProducer') {
    $source = Join-Path $build $folder
    if (Test-Path $source) {
        robocopy $source (Join-Path $dist $folder) /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Copying $folder failed ($LASTEXITCODE)" }
    }
}
robocopy (Join-Path $root 'skills\vibera-studio') (Join-Path $dist 'skills\vibera-studio') /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying the skill failed ($LASTEXITCODE)" }
$commit = (git -c safe.directory="$root" -C $root rev-parse HEAD).Trim()
$dirty = (git -c safe.directory="$root" -C $root status --porcelain --untracked-files=no) -ne $null
$lines = @(
    'Quick update from a compile-only build (owner request; not a qualified release, untested).',
    "Source commit: $commit$(if ($dirty) { ' plus uncommitted changes' })",
    "Updated: $(Get-Date -Format 'yyyy-MM-dd HH:mm')",
    'provenance.json and SOURCE.txt describe the last qualified package.'
)
[IO.File]::WriteAllLines((Join-Path $dist 'QUICK-UPDATE.txt'), $lines, [Text.UTF8Encoding]::new($false))
Write-Output "dist updated to $commit"
$LASTEXITCODE = 0
