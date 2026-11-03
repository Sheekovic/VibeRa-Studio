# SPDX-License-Identifier: AGPL-3.0-or-later
param()
$ErrorActionPreference='Stop'
$tuneRoot=Split-Path $PSScriptRoot -Parent
$tuneVswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$tuneVs=& $tuneVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $tuneVs){throw 'Installed MSVC C++ tools required'}
$tuneCmake=Join-Path $tuneVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$tuneBuild=Join-Path $tuneRoot 'build/windows/experiments/tune-experiment'
& $tuneCmake -S (Join-Path $tuneRoot 'experiments/tune') -B $tuneBuild -G 'Visual Studio 17 2022' -A x64 *> (Join-Path $tuneRoot 'build/tune-configure.log')
if($LASTEXITCODE -ne 0){throw 'Tune configure failed'}
& $tuneCmake --build $tuneBuild --config Release --parallel 2 *> (Join-Path $tuneRoot 'build/tune-build.log')
if($LASTEXITCODE -ne 0){throw 'Tune build failed'}
$tuneExe=Join-Path $tuneBuild 'Release/VibeRaTuneExperiment.exe'
$tuneJob=Start-Process -FilePath $tuneExe -ArgumentList @(('"'+(Join-Path $tuneBuild 'verification.txt')+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $tuneRoot 'build/tune-test.log') -RedirectStandardError (Join-Path $tuneRoot 'build/tune-error.log')
$tuneOwnedHandle=$tuneJob.Handle
$tuneCreated=$tuneJob.StartTime.ToUniversalTime().ToString('o')
$tuneJob.WaitForExit()
[pscustomobject]@{exitCode=$tuneJob.ExitCode;pid=$tuneJob.Id;creation=$tuneCreated;executableSha256=(Get-FileHash -LiteralPath $tuneExe).Hash;purpose='Original offline synthetic pitch detector and scale target mapping; not audio correction or application integration'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $tuneBuild 'provenance.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $tuneRoot 'build/tune-test.log')
if($tuneJob.ExitCode -ne 0){throw 'Tune experiment failed; see verification/error evidence'}
