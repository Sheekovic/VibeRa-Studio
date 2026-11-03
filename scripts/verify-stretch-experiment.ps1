# SPDX-License-Identifier: AGPL-3.0-or-later
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Installed MSVC C++ tools required'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build=Join-Path $root 'build/windows/experiments/stretch-experiment'
$header=Join-Path $root 'vendor/source/tracktion_engine-4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86/modules/tracktion_engine/3rd_party/signalsmith-stretch/signalsmith-stretch.h'
if((Get-FileHash -LiteralPath $header).Hash -ne '1188667959AC19DD40C0A6ABBCE694E44705615EC4F0E8DB8AF0F1CFB4C5DEA7'){
    throw 'Pinned experimental stretch header changed; review provenance before testing'
}
Push-Location $root
try{
    & $cmake -S experiments/stretch -B $build -G 'Visual Studio 17 2022' -A x64 *> build/stretch-configure.log
    if($LASTEXITCODE -ne 0){throw 'Stretch configure failed; see build/stretch-configure.log'}
    & $cmake --build $build --config Release --parallel 2 *> build/stretch-build.log
    if($LASTEXITCODE -ne 0){throw 'Stretch build failed; see build/stretch-build.log'}
    $exe=Join-Path $build 'Release/VibeRaStretchExperiment.exe'
    $job=Start-Process -FilePath $exe -ArgumentList @(('"'+(Join-Path $build 'verification.txt')+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'build/stretch-test.log') -RedirectStandardError (Join-Path $root 'build/stretch-error.log')
    $ownedHandle=$job.Handle
    $created=$job.StartTime.ToUniversalTime().ToString('o')
    $job.WaitForExit()
    $result=$job.ExitCode
    @{exitCode=$result;pid=$job.Id;creation=$created;executableSha256=(Get-FileHash -LiteralPath $exe).Hash;
      engineRevision='4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86';headerSha256=(Get-FileHash -LiteralPath $header).Hash;
      purpose='Offline pitch/time and prepared streaming candidate matrix; not application integration, complete real-time DSP audit or listening'} |
      ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $build 'provenance.json')
    if($result -ne 0){throw 'Stretch experiment failed; see build/stretch-test.log'}
    Write-Output "Verified isolated stretch experiment: $build"
}finally{Pop-Location}
