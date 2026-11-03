param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$CompileOnly,
      [switch]$ReleaseCandidate, [switch]$Patch, [string]$Module='rack-pattern-sampler')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'engineering-owner.ps1')
$taskOwnerExit=Invoke-VibeRaEngineeringOwner -Purpose 'build' -Script $PSCommandPath -Parameters $PSBoundParameters
if($null -ne $taskOwnerExit){exit $taskOwnerExit}
& python (Join-Path $PSScriptRoot 'dev.py') document-check
if($LASTEXITCODE -ne 0){throw 'Verification document integrity failed before compilation.'}
if($CompileOnly -and $ReleaseCandidate){throw 'Choose incremental compilation or a module release candidate, not both.'}
if($Patch -and -not $ReleaseCandidate){throw '-Patch qualifies a patch release candidate; add -ReleaseCandidate.'}
if($ReleaseCandidate){
    # Patch releases (same major.minor) skip the complete suite; milestones keep it.
    $gatePhase=if($Patch){'patch'}else{'candidate'}
    if($Patch){
        & python (Join-Path $PSScriptRoot 'patch-release.py') check
        if($LASTEXITCODE -ne 0){throw 'Not a patch release; preserve the qualified distribution.'}
    }
    & python (Join-Path $PSScriptRoot 'module-gate.py') --module $Module --phase $gatePhase
    if($LASTEXITCODE -ne 0){throw 'The module checklist is incomplete; preserve the qualified distribution.'}
}
if (Test-Path -LiteralPath (Join-Path $root '.git') -PathType Leaf) {throw 'Integrate worktree source into the main checkout before building. Only main build/windows is authorized as a permanent compilation location.'}
& (Join-Path $PSScriptRoot 'apply-dependency-patches.ps1')
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake with Visual Studio C++ tools is required.' }
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
    & python (Join-Path $PSScriptRoot 'distribution.py') refresh-inputs
    if ($LASTEXITCODE -ne 0) {throw 'Cannot invalidate changed project build inputs'}
    & python (Join-Path $PSScriptRoot 'distribution.py') capture (Join-Path $root 'build/build-inputs.json')
    if ($LASTEXITCODE -ne 0) {throw 'Cannot record build inputs'}
    $taskCacheText=& python (Join-Path $PSScriptRoot 'dev.py') cache-state
    if($LASTEXITCODE -ne 0){throw 'CMake cache ownership/configuration is inconsistent'}
    $taskCacheState=$taskCacheText | ConvertFrom-Json
    if($taskCacheState.needsConfigure){
        & $cmake --preset windows
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    }
    & $cmake --build --preset $Configuration.ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    & python (Join-Path $PSScriptRoot 'distribution.py') compiled (Join-Path $root 'build/build-inputs.json')
    if ($LASTEXITCODE -ne 0) {throw 'Build inputs changed; do not qualify this compilation'}
    if ($ReleaseCandidate) {
        # One complete suite per candidate: reuse the last unfiltered fast-test run when every
        # binary it executed is byte-identical to this build; otherwise run it once now.
        $summaryCheck=Join-Path $PSScriptRoot 'fast-test-summary.py'
        if ($Patch) {
            # Affected tests since the released commit plus the core smoke set.
            $count=(& python (Join-Path $PSScriptRoot 'patch-release.py') tests | Select-Object -Last 1)
            if ($LASTEXITCODE -ne 0) {throw 'Patch tests failed; existing distribution preserved. See build/windows/fast-test'}
        } else {
        & python $summaryCheck --require-current
        if ($LASTEXITCODE -ne 0) {
            & python (Join-Path $PSScriptRoot 'fast-test.py') *> (Join-Path $root 'build/distribution-fast-test.log')
            & python $summaryCheck --require-current
            if ($LASTEXITCODE -ne 0) {throw 'Source tests failed; existing distribution preserved. See build/windows/fast-test'}
        }
        $count=& python $summaryCheck --count
        }
        & python (Join-Path $PSScriptRoot 'distribution.py') record (Join-Path $root 'build/build-inputs.json') $Configuration $count $count
        if ($LASTEXITCODE -ne 0) {throw 'Build source/binary checkpoint failed'}
        & (Join-Path $PSScriptRoot 'package.ps1') -Configuration $Configuration -Module $Module -Patch:$Patch
    }
} finally {
    Pop-Location
    # Successful packaging retires generated runnables itself. On failure retain
    # this actively investigated candidate in build/windows: deleting its linked
    # outputs forces a relink and invalidates exact-binary acceptance evidence.
    # The qualified distribution remains protected by the packaging transaction.
}
