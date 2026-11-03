# SPDX-License-Identifier: AGPL-3.0-or-later
param([string]$Candidate='', [string]$Linear='', [string]$RubberBand='', [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Evidence='tune-correction', [string]$BuildDirectory='tune-experiment', [switch]$KnownPitch, [switch]$TraceAllocations, [switch]$PreparedResampler, [switch]$FlushDenormals, [switch]$PreparedOutput, [switch]$StereoApart, [switch]$LinkedPhase, [ValidateSet('Default','Dense','Short','DenseDouble')][string]$SignalProfile='Default', [switch]$SignalPhaseCorrection, [switch]$ResourceAudit)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root ('build/'+$Evidence)
if(Test-Path -LiteralPath $output){throw 'Use a fresh evidence name; retained experiment results must not be overwritten'}
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Installed MSVC C++ tools required'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if ($BuildDirectory -notmatch '^[a-zA-Z0-9_-]+$') {throw 'Use a single experiment name inside the canonical compilation cache'}
$build=Join-Path $root ('build/windows/experiments/'+$BuildDirectory)
$candidatePath=if($Candidate){(Resolve-Path -LiteralPath $Candidate).Path}else{Join-Path $root 'vendor/source/tracktion_engine-4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86/modules/tracktion_engine/3rd_party/signalsmith-stretch'}
$linearPath=if($Linear){(Resolve-Path -LiteralPath $Linear).Path}else{''}
$rubberPath=if($RubberBand){(Resolve-Path -LiteralPath $RubberBand).Path}else{''}
if($rubberPath -and $SignalProfile -ne 'Default'){throw 'Signalsmith profiles require a Signalsmith-only experiment'}
if($SignalPhaseCorrection){
    if($rubberPath){throw 'Phase-frequency experiment is Signalsmith only'}
    & python (Join-Path $root 'scripts/apply-tune-signal-patch.py') $candidatePath
    if($LASTEXITCODE -ne 0){throw 'Signalsmith research patch refused; source preserved'}
} else {
    $signalPatch=Get-Content -LiteralPath (Join-Path $root 'experiments/tune/signalsmith-phase-frequency.json') -Raw | ConvertFrom-Json
    if((Get-FileHash -LiteralPath (Join-Path $candidatePath 'signalsmith-stretch.h')).Hash.ToLowerInvariant() -eq $signalPatch.preparedSha256){throw 'Research phase-frequency patch present; acknowledge with -SignalPhaseCorrection'}
}
if($PreparedOutput -and -not $PreparedResampler){throw 'Output-source experiment also requires -PreparedResampler'}
if($LinkedPhase -and (-not $PreparedOutput -or $StereoApart)){throw 'Linked-phase experiment requires prepared output and mid/side processing'}
if($ResourceAudit -and (-not $rubberPath -or -not $PreparedResampler)){throw 'Resource auditing requires the pinned Rubber Band preparation'}
if($PreparedResampler){
    if(-not $rubberPath){throw 'The prepared-resampler experiment requires the pinned Rubber Band candidate'}
    $patchArgs=@($rubberPath)
    if($PreparedOutput){$patchArgs+='--output-source'}
    if($LinkedPhase){$patchArgs+='--linked-phase'}
    if($ResourceAudit){$patchArgs+='--resource-audit'}
    & python (Join-Path $root 'scripts/apply-tune-resampler-patch.py') @patchArgs
    if($LASTEXITCODE -ne 0){throw 'Candidate patch validation failed; unexpected source is preserved'}
}
$bqHash=if($rubberPath){(Get-FileHash -LiteralPath (Join-Path $rubberPath 'src/common/BQResampler.cpp')).Hash}else{''}
$r3Hash=if($rubberPath){(Get-FileHash -LiteralPath (Join-Path $rubberPath 'src/finer/R3LiveShifter.cpp')).Hash}else{''}
$phaseHash=if($rubberPath){(Get-FileHash -LiteralPath (Join-Path $rubberPath 'src/finer/PhaseAdvance.h')).Hash}else{''}
$allocatorHash=if($rubberPath){(Get-FileHash -LiteralPath (Join-Path $rubberPath 'src/common/Allocators.h')).Hash}else{''}
if($rubberPath -and -not $LinkedPhase){
    $phasePatch=Get-Content -LiteralPath (Join-Path $root 'experiments/tune/rubberband-linked-phase.json') -Raw | ConvertFrom-Json
    if($phaseHash.ToLowerInvariant() -eq $phasePatch.preparedSha256){throw 'Linked-phase patch is present; acknowledge it with -LinkedPhase'}
}
if($rubberPath -and -not $PreparedOutput){
    $outputPatch=Get-Content -LiteralPath (Join-Path $root 'experiments/tune/rubberband-output-source.json') -Raw | ConvertFrom-Json
    if($r3Hash.ToLowerInvariant() -eq $outputPatch.preparedSha256){throw 'Output-source patch is present; acknowledge it with -PreparedOutput'}
}
if($rubberPath -and -not $PreparedResampler){
    $patch=Get-Content -LiteralPath (Join-Path $root 'experiments/tune/rubberband-preparation.json') -Raw | ConvertFrom-Json
    if($bqHash.ToLowerInvariant() -eq $patch.preparedSha256){throw 'Candidate is patched; use -PreparedResampler to acknowledge the experimental source explicitly'}
}
$flushOption=if($FlushDenormals){'ON'}else{'OFF'}
$apartOption=if($StereoApart){'ON'}else{'OFF'}
$resourceOption=if($ResourceAudit){'ON'}else{'OFF'}
& $cmake -S (Join-Path $root 'experiments/tune') -B $build -G 'Visual Studio 17 2022' -A x64 ("-DSTRETCH_ROOT="+$candidatePath) ("-DLINEAR_ROOT="+$linearPath) ("-DRUBBERBAND_ROOT="+$rubberPath) ("-DTUNE_FLUSH_DENORMALS="+$flushOption) ("-DTUNE_CHANNELS_APART="+$apartOption) ("-DTUNE_SIGNAL_PROFILE="+$SignalProfile) ("-DTUNE_RESOURCE_AUDIT="+$resourceOption) *> (Join-Path $root ('build/'+$Evidence+'-configure.log'))
if($LASTEXITCODE -ne 0){throw 'Correction configure failed'}
& $cmake --build $build --config Release --target VibeRaTuneCorrectionExperiment --parallel 2 *> (Join-Path $root ('build/'+$Evidence+'-build.log'))
if($LASTEXITCODE -ne 0){throw 'Correction build failed'}
$exe=Join-Path $build 'Release/VibeRaTuneCorrectionExperiment.exe'
New-Item -ItemType Directory -Path $output | Out-Null
$jobArgs=@(('"'+$output+'"'))
if($KnownPitch){$jobArgs+='--known-pitch'}
$previousTrace=[Environment]::GetEnvironmentVariable('VIBERA_TUNE_ALLOCATION_TRACE','Process')
try {
    [Environment]::SetEnvironmentVariable('VIBERA_TUNE_ALLOCATION_TRACE',$(if($TraceAllocations){'1'}else{$null}),'Process')
    $job=Start-Process -FilePath $exe -ArgumentList $jobArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
} finally {
    [Environment]::SetEnvironmentVariable('VIBERA_TUNE_ALLOCATION_TRACE',$previousTrace,'Process')
}
$ownedHandle=$job.Handle
$created=$job.StartTime.ToUniversalTime().ToString('o')
$job.WaitForExit()
$backend=if($rubberPath){'Rubber Band LiveShifter'}else{'Signalsmith Stretch'}
$rubberSourceHash=if($rubberPath){(Get-FileHash -LiteralPath (Join-Path $rubberPath 'single/RubberBandSingle.cpp')).Hash}else{''}
[pscustomobject]@{exitCode=$job.ExitCode;pid=$job.Id;creation=$created;executableSha256=(Get-FileHash -LiteralPath $exe).Hash;backend=$backend;rubberBandSource=$rubberPath;rubberBandSourceSha256=$rubberSourceHash;resamplerSourceSha256=$bqHash;shifterSourceSha256=$r3Hash;phaseSourceSha256=$phaseHash;allocatorSourceSha256=$allocatorHash;resourceAudit=[bool]$ResourceAudit;preparedResampler=[bool]$PreparedResampler;preparedOutput=[bool]$PreparedOutput;linkedPhase=[bool]$LinkedPhase;stereoApart=[bool]$StereoApart;flushDenormals=[bool]$FlushDenormals;signalsmithSource=$candidatePath;signalProfile=$SignalProfile;signalPhaseCorrection=[bool]$SignalPhaseCorrection;linearInclude=$linearPath;knownPitch=[bool]$KnownPitch;allocationTrace=[bool]$TraceAllocations;signalsmithHeaderSha256=(Get-FileHash -LiteralPath (Join-Path $candidatePath 'signalsmith-stretch.h')).Hash;purpose='Synthetic offline pitch correction, stereo/latency/prepared processing; not application or vocal/listening qualification'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'provenance.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $output 'stdout.log')
if($job.ExitCode -ne 0){throw 'Correction experiment failed; retained verification/stderr/provenance'}
