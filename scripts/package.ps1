param([ValidateSet('Release','Debug')][string]$Configuration='Release',
      [string]$InputDirectory='', [string]$Provenance='', [string]$Module='rack-pattern-sampler', [switch]$Patch)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'engineering-owner.ps1')
$taskOwnerExit=Invoke-VibeRaEngineeringOwner -Purpose 'package' -Script $PSCommandPath -Parameters $PSBoundParameters
if($null -ne $taskOwnerExit){exit $taskOwnerExit}
& python (Join-Path $PSScriptRoot 'dev.py') document-check
if($LASTEXITCODE -ne 0){throw 'Verification document integrity failed before packaging.'}
& python (Join-Path $PSScriptRoot 'module-gate.py') --module $Module --phase $(if($Patch){'patch'}else{'candidate'})
if($LASTEXITCODE -ne 0){throw 'The complete module candidate is not ready; no distribution files were changed.'}
$dist=Join-Path $root 'dist/VibeRa Studio'
$stage=Join-Path $root 'build/package-stage'
$previous=Join-Path $root 'dist/.previous'
if (-not $InputDirectory) {$InputDirectory=Join-Path $root 'build/package-input'}
if (-not $Provenance) {$Provenance=Join-Path $root 'build/distribution-candidate.json'}
foreach ($path in @($stage,$previous)) {if (Test-Path -LiteralPath $path) {throw "Unfinished replacement exists; reconcile its recorded state first: $path"}}
if (Test-Path -LiteralPath $dist) {
    foreach ($process in Get-CimInstance Win32_Process) {
        if ($process.ExecutablePath -and $process.ExecutablePath.StartsWith($dist+'\',[StringComparison]::OrdinalIgnoreCase)) {
            throw "Canonical distribution is in use by PID $($process.ProcessId); close that instance before replacement. No process was closed."
        }
    }
    & python (Join-Path $PSScriptRoot 'check-distribution-retirement.py') $dist
    if ($LASTEXITCODE -ne 0) {throw 'Canonical folder contains missing, modified or unmanifested content; preserve and reconcile user work before replacement. No folder was moved.'}
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
& python (Join-Path $PSScriptRoot 'distribution.py') prepare $InputDirectory $Provenance $stage
if ($LASTEXITCODE -ne 0) {throw 'Exact source/binary/runtime identity refused; current distribution preserved'}
& (Join-Path $PSScriptRoot 'verify-distribution.ps1') -Directory $stage -Output (Join-Path $root 'build/distribution-verification/staging') -Quick
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {throw 'Staging verification failed; current distribution preserved'}
New-Item -ItemType Directory -Force -Path (Split-Path $dist -Parent) | Out-Null
if (Test-Path -LiteralPath $dist) {Move-Item -LiteralPath $dist -Destination $previous}
Move-Item -LiteralPath $stage -Destination $dist
try {
    & (Join-Path $PSScriptRoot 'verify-distribution.ps1') -Directory $dist -Output (Join-Path $root 'build/distribution-verification/current')
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {throw 'Final distribution verification failed'}
} catch {
    $verificationFailure=$_
    # Only our generated candidate is disposable. Roll back an in-use replacement
    # transaction, never keep a permanent version-numbered backup.
    if (Test-Path -LiteralPath $previous) {
        if ([IO.Path]::GetFullPath($dist) -ne [IO.Path]::GetFullPath((Join-Path $root 'dist/VibeRa Studio'))) {throw 'Unsafe rollback path'}
        & python (Join-Path $PSScriptRoot 'check-distribution-retirement.py') $dist
        if ($LASTEXITCODE -ne 0) {throw "Rollback preserved both folders because candidate content changed. Reconcile user work before removal. Original verification failure: $verificationFailure"}
        Remove-Item -LiteralPath $dist -Recurse -Force
        Move-Item -LiteralPath $previous -Destination $dist
    }
    throw
}
if (Test-Path -LiteralPath $previous) {
    if ([IO.Path]::GetFullPath($previous) -ne [IO.Path]::GetFullPath((Join-Path $root 'dist/.previous'))) {throw 'Unsafe previous path'}
    & python (Join-Path $PSScriptRoot 'check-distribution-retirement.py') $previous
    if ($LASTEXITCODE -ne 0) {throw 'Verified candidate preserved; temporary previous folder changed during verification. Preserve/reconcile that user content before retirement.'}
    Remove-Item -LiteralPath $previous -Recurse -Force
}
# Standalone packaging has the same one-folder postcondition as build.ps1.
# The cleaner inspects exact generated paths and refuses active/linked outputs.
& (Join-Path $PSScriptRoot 'clear-build-runnables.ps1')
& python (Join-Path $PSScriptRoot 'audit-distribution.py')
if ($LASTEXITCODE -ne 0) {throw 'Package is verified, but obsolete output reconciliation is incomplete; inspect build/single-folder-current-inventory.json'}
Write-Output "Single runnable distribution updated: $dist"
