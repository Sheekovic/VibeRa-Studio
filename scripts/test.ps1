param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'engineering-owner.ps1')
$taskOwnerExit=Invoke-VibeRaEngineeringOwner -Purpose 'tests' -Script $PSCommandPath -Parameters $PSBoundParameters
if($null -ne $taskOwnerExit){exit $taskOwnerExit}
if (-not (Test-Path -LiteralPath (Join-Path $root "build/windows/VibeRaTests_artefacts/$Configuration/VibeRaTests.exe"))) {
    Write-Output 'Testing canonical distribution bytes; this does not qualify unbuilt working-tree changes.'
    & (Join-Path $PSScriptRoot 'verify-distribution.ps1')
    return
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$ctest = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
Push-Location $root
try {
    & $ctest --preset $Configuration.ToLowerInvariant() -j 1
    if ($LASTEXITCODE -ne 0) { throw 'Acceptance tests failed.' }
} finally { Pop-Location }
