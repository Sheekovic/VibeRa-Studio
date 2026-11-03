# SPDX-License-Identifier: AGPL-3.0-or-later
param(
    [ValidateSet('SelfTest','ObserverSelfTest','TimeoutSelfTest','Preview','Live','Core')][string]$Mode='Preview',
    [ValidateRange(1,1000)][double]$ThresholdMs=5.333333,
    [ValidateRange(1,600)][int]$TimeoutSeconds=180,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Evidence
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$helper=Join-Path $root 'build/windows/Release/VibeRaCallbackProbe.exe'
$target=Join-Path $root 'build/windows/VibeRaTests_artefacts/Release/VibeRaTests.exe'
$output=Join-Path $root ('build/'+$Evidence)
if(Test-Path -LiteralPath $output){throw 'Use a fresh evidence name; retained failures must not be overwritten'}
if(-not (Test-Path -LiteralPath $helper)){throw 'Compile the current source with scripts/build.ps1 -CompileOnly first'}
$targetArgs=@()
if($Mode -in @('SelfTest','ObserverSelfTest','TimeoutSelfTest')){
    if($Mode -eq 'TimeoutSelfTest' -and $TimeoutSeconds -ge 5){throw 'Timeout self-test requires a limit below five seconds'}
    $target=$helper
    $targetArgs=if($Mode -eq 'TimeoutSelfTest'){@('--timeout-fixture')}else{@('--fixture')}
}
else {
    if(-not (Test-Path -LiteralPath $target)){throw 'Current compiled acceptance executable is missing'}
    $targetArgs=@(switch($Mode){
        'Preview' {@('--sampler-preview-check')}
        'Live' {@('--sampler-live-check')}
        'Core' {@()}
    })
    $targetArgs+=('"'+(Join-Path $output 'target-evidence')+'"')
}
New-Item -ItemType Directory -Path $output | Out-Null
$arguments=@(('"'+$output+'"'),$ThresholdMs.ToString([Globalization.CultureInfo]::InvariantCulture),
    $TimeoutSeconds.ToString(),('"'+$target+'"'))+$targetArgs
$previousPause=[Environment]::GetEnvironmentVariable('VIBERA_CALLBACK_OBSERVER_PAUSE_MS','Process')
try {
    if($Mode -eq 'ObserverSelfTest'){[Environment]::SetEnvironmentVariable('VIBERA_CALLBACK_OBSERVER_PAUSE_MS','40','Process')}
    $job=Start-Process -FilePath $helper -ArgumentList $arguments -WorkingDirectory $root -PassThru -WindowStyle Hidden
} finally {
    [Environment]::SetEnvironmentVariable('VIBERA_CALLBACK_OBSERVER_PAUSE_MS',$previousPause,'Process')
}
$ownedHandle=$job.Handle
$creation=$job.StartTime.ToUniversalTime().ToString('o')
$job.WaitForExit()
$job.Refresh()
$text=Get-Content -LiteralPath (Join-Path $output 'callback-stacks.txt') -Raw
$expectedExit=if($Mode -eq 'TimeoutSelfTest'){124}else{0}
$selfTestVerified=$Mode -in @('SelfTest','ObserverSelfTest','TimeoutSelfTest') -and $text -match 'fixtureStall' -and
    $text -match ('Owned target exit='+$expectedExit)
$observerMatch=[regex]::Match($text,'maxObserverWaitMs=([0-9.]+)')
$observerMaximum=if($observerMatch.Success){[double]::Parse($observerMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)}else{-1}
$observerVerified=$Mode -eq 'ObserverSelfTest' -and $observerMaximum -ge 35 -and $text -match 'Intentional observer-only waitMs=40'
$report=[ordered]@{
    purpose='Owned synthetic callback stack diagnosis; suspension affects timing; not a release-performance pass'
    mode=$Mode;thresholdMs=$ThresholdMs;pid=$job.Id;creation=$creation;exitCode=$job.ExitCode;expectedExitCode=$expectedExit
    supervisorSha256=(Get-FileHash -LiteralPath $helper).Hash
    target=$target;targetSha256=(Get-FileHash -LiteralPath $target).Hash
    symbolizedFixtureVerified=$selfTestVerified
    observerPauseVerified=$observerVerified;maxObserverWaitMs=$observerMaximum
    beaconSourceSha256=(Get-FileHash -LiteralPath (Join-Path $root 'tests/CallbackStallProbe.h')).Hash
    supervisorSourceSha256=(Get-FileHash -LiteralPath (Join-Path $root 'tests/CallbackStallSupervisor.cpp')).Hash
    nativeDesktop=$false;listening=$false;hardware=$false
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'process.json') -Encoding UTF8
Write-Output $text
if($job.ExitCode -ne $expectedExit){throw 'Unexpected diagnostic target result; retain traces and inspect target.log'}
if($Mode -in @('SelfTest','ObserverSelfTest','TimeoutSelfTest') -and -not $selfTestVerified){
    throw 'Intentional wait did not produce a symbolized owned fixture stack'
}
if($Mode -eq 'ObserverSelfTest' -and -not $observerVerified){throw 'Injected observer pause was not attributed; retain observer-waits.csv'}
