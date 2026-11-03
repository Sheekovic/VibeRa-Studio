param([string]$Directory=(Join-Path $PSScriptRoot '../dist/VibeRa Studio'),
      [string]$Output=(Join-Path $PSScriptRoot '../build/distribution-verification/current'),
      [switch]$Quick, [switch]$ContinueAfterFailure)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'engineering-owner.ps1')
$taskOwnerExit=Invoke-VibeRaEngineeringOwner -Purpose 'package acceptance' -Script $PSCommandPath -Parameters $PSBoundParameters
if($null -ne $taskOwnerExit){exit $taskOwnerExit}
$folder=(Resolve-Path -LiteralPath $Directory).Path
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output=(Resolve-Path -LiteralPath $Output).Path
$app=Join-Path $folder 'VibeRa Studio.exe'
$runner=Join-Path $folder 'VibeRaTests.exe'
$receipts=@()
$failures=@()
function Invoke-Owned([string]$Executable,[string[]]$Arguments,[string]$Name,[int]$Seconds=300) {
    $log=Join-Path $Output ($Name+'.log')
    $process=Start-Process -FilePath $Executable -ArgumentList @($Arguments | ForEach-Object {'"'+$_+'"'}) -WorkingDirectory $folder -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError ($log+'.error')
    $handle=$process.Handle
    $receipt=[ordered]@{pid=$process.Id;creation=$process.StartTime.ToUniversalTime().ToString('o');purpose=$Name;executable=$Executable;sha256=(Get-FileHash -LiteralPath $Executable).Hash;state='running';workerPaths=@()}
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($log+'.process.json') -Encoding UTF8
    $end=[DateTime]::UtcNow.AddSeconds($Seconds)
    while (-not $process.WaitForExit(250) -and [DateTime]::UtcNow -lt $end) {
        # Read only child identity; never close or modify another instance.
        if ($Name -in @('installed-plugins','instrument','live-hosted','native-clips-instrument','native-clips-effect','rack-vst3-live','step-fill-vst3-live','channel-solo-vst3-live','channel-reorder-vst3-live','channel-management-vst3','step-velocity-vst3-live','step-velocity-vst3','swing-vst3-live','quantizer-vst3-live','loop-bounds-vst3-live','normalize-vst3-live','sampler-filter-vst3-live','clip-groups-vst3-live','note-mute-vst3-live','note-slice-vst3-live','note-stretch-vst3-live','mixer-routing-vst3-live','mixer-latency','mixer-sidechain-transport','mixer-sidechain-graph','mixer-dynamic-latency','mixer-bypass','plugin-latency-mcp','mixer-effect-opening','mixer-effect-opening-unsupported','mixer-chain-live-export','mixer-native-scalar-gui','mixer-native-scalar-installed','studio-channel-routing-provider','studio-channel-routing-same-config','mixer-plugin-save-capture','mixer-plugin-recovery','deferred-device-candidate-fault','mixer-default-dynamic-latency','plugin-parameters-gui')) {
        Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.ExecutablePath -and $_.ExecutablePath.EndsWith('VibeRa Plugin Worker.exe')) {
                if ((Split-Path $_.ExecutablePath -Parent) -ne $folder) {throw 'Plugin worker escaped the selected distribution'}
                $receipt.workerPaths=@($receipt.workerPaths + $_.ExecutablePath | Select-Object -Unique)
            }
        }
        }
    }
    $timeout=-not $process.HasExited
    if ($timeout) {
        $receipt.gracefulClose=$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {$receipt.retainedHandleTermination=$true; $process.Kill(); $process.WaitForExit()}
    }
    $receipt.state='terminal'; $receipt.exitCode=$process.ExitCode; $receipt.timedOut=$timeout
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($log+'.process.json') -Encoding UTF8
    $script:receipts+= $receipt
    if ($timeout -or $process.ExitCode -ne 0) {
        $message="$Name failed ($($process.ExitCode)); inspect $log"
        $script:failures+=$message
        if (-not $ContinueAfterFailure) {throw $message}
    }
}
$result=[ordered]@{finished=$false;passed=$false;directory=$folder;quick=[bool]$Quick;processes=@();failures=@();nativeDesktop=$false;listening=$false;hardware=$false;installedPlugins='not run'}
# Reusing an evidence folder must never expose the preceding run's green receipt
# while a new verification is still running or gets interrupted before finally.
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'verification.json') -Encoding UTF8
try {
    & python (Join-Path $PSScriptRoot 'distribution.py') verify $folder
    if ($LASTEXITCODE -ne 0) {throw 'Distribution file identity failed'}
    $result.applicationSha256=(Get-FileHash -LiteralPath $app).Hash
    $result.workerSha256=(Get-FileHash -LiteralPath (Join-Path $folder 'VibeRa Plugin Worker.exe')).Hash
    $version=(Get-Item -LiteralPath $app).VersionInfo.ProductVersion
    $result.version=$version
    $manifest=Get-Content -LiteralPath (Join-Path $folder 'provenance.json') -Raw | ConvertFrom-Json
    $browserLive=@($manifest.sourceHashes | Where-Object {$_.path -eq 'src/StudioBrowser.cpp'}).Count -gt 0
    $nativeClips=@($manifest.sourceHashes | Where-Object {$_.path -eq 'tests/PluginClipGraphTests.h'}).Count -gt 0
    $mixerRisk=@($manifest.sourceHashes | Where-Object {$_.path -eq 'tests/MixerRiskAcceptance.h'}).Count -gt 0
    $mixerModule=@($manifest.sourceHashes | Where-Object {$_.path -eq 'src/MixerAutomationTake.h'}).Count -gt 0
    $synthesizerModule=@($manifest.sourceHashes | Where-Object {$_.path -eq 'src/synth/SynthPlugin.cpp'}).Count -gt 0
    $noteReleaseWorker=@($manifest.sourceHashes | Where-Object {$_.path -eq 'tests/NoteReleaseWorkerAcceptance.h'}).Count -gt 0
    if ($noteReleaseWorker) {
        $releaseFixture=Join-Path $folder 'VibeRaReleaseInstrumentFixture.vst3'
        if (-not (Test-Path -LiteralPath $releaseFixture -PathType Leaf)) {throw 'Packaged original release fixture missing; no build-folder fallback'}
        Invoke-Owned $runner @('--note-release-worker-check',(Join-Path $Output 'note-release-worker'),$releaseFixture) 'note-release-worker' 60
    }
    if ($synthesizerModule) {
        $synthScope=if($Quick){'staging'}else{'current'}
        $synthOutput=Join-Path $root ('build/windows/distribution-synthesizer/'+$synthScope)
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-synthesizer-installed.py'),$app,$synthOutput) 'synthesizer-native-installed' 180
    }

    if ($mixerRisk) {
        $mixerFixture=Join-Path $folder 'VibeRaMixerLatencyFixture.vst3'
        if (-not (Test-Path -LiteralPath $mixerFixture -PathType Leaf)) {throw 'Packaged original Mixer fixture missing; acceptance cannot depend on a build folder'}
        Invoke-Owned $runner @('--mixer-risk-check',(Join-Path $Output 'mixer-latency'),$mixerFixture) 'mixer-latency' 120
        Invoke-Owned $runner @('--sidechain-transport-check',(Join-Path $Output 'mixer-sidechain-transport'),$mixerFixture) 'mixer-sidechain-transport' 90
        Invoke-Owned $runner @('--sidechain-graph-check',(Join-Path $Output 'mixer-sidechain-graph'),$mixerFixture) 'mixer-sidechain-graph' 90
        Invoke-Owned $runner @('--dynamic-latency-check',(Join-Path $Output 'mixer-dynamic-latency'),$mixerFixture) 'mixer-dynamic-latency' 90
        Invoke-Owned $runner @('--plugin-bypass-check',(Join-Path $Output 'mixer-bypass'),$mixerFixture) 'mixer-bypass' 60
        Invoke-Owned $runner @('--graph-publication-check',(Join-Path $Output 'mixer-graph-publication')) 'mixer-graph-publication' 30
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-plugin-latency-mcp.py'),$app,(Join-Path $Output 'plugin-latency-mcp'),$mixerFixture) 'plugin-latency-mcp' 90
    }
    Invoke-Owned $app @('--smoke-test',(Join-Path $Output 'workflow')) 'workflow'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'tests/mcp_acceptance.py'),$app,(Join-Path $Output 'mcp'),$version) 'mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-glue-mcp.py'),$app,(Join-Path $Output 'glue-mcp')) 'glue-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-rack-mcp.py'),$app,(Join-Path $Output 'rack-mcp')) 'rack-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-step-fill-mcp.py'),$app,(Join-Path $Output 'step-fill-mcp')) 'step-fill-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-channel-solo-mcp.py'),$app,(Join-Path $Output 'channel-solo-mcp')) 'channel-solo-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-channel-management-mcp.py'),$app,(Join-Path $Output 'channel-management-mcp')) 'channel-management-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-step-velocity-mcp.py'),$app,(Join-Path $Output 'step-velocity-mcp')) 'step-velocity-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-swing-mcp.py'),$app,(Join-Path $Output 'swing-mcp')) 'swing-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-quantizer-mcp.py'),$app,(Join-Path $Output 'quantizer-mcp')) 'quantizer-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-groove-mcp.py'),$app,(Join-Path $Output 'groove-mcp')) 'groove-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-voices-mcp.py'),$app,(Join-Path $Output 'sampler-voices-mcp')) 'sampler-voices-mcp'
    Invoke-Owned $runner @('--sampler-voice-modes-check',(Join-Path $Output 'sampler-voice-modes')) 'sampler-voice-modes' 60
    Invoke-Owned $runner @('--sampler-note-modes-check',(Join-Path $Output 'sampler-note-modes')) 'sampler-note-modes' 60
    Invoke-Owned $app @('--piano-note-modes-check',(Join-Path $Output 'piano-note-modes')) 'piano-note-modes' 60
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-note-modes-mcp.py'),$app,(Join-Path $Output 'sampler-note-modes-mcp')) 'sampler-note-modes-mcp' 90
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-voice-modes-mcp.py'),$app,(Join-Path $Output 'sampler-voice-modes-mcp')) 'sampler-voice-modes-mcp' 90
    Invoke-Owned $runner @('--sampler-filter-check',(Join-Path $Output 'sampler-filter')) 'sampler-filter' 60
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-filter-mcp.py'),$app,(Join-Path $Output 'sampler-filter-mcp')) 'sampler-filter-mcp' 90
    Invoke-Owned $runner @('--sampler-modulation-check',(Join-Path $Output 'sampler-modulation')) 'sampler-modulation' 60
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-modulation-mcp.py'),$app,(Join-Path $Output 'sampler-modulation-mcp')) 'sampler-modulation-mcp' 90
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-envelope-mcp.py'),$app,(Join-Path $Output 'sampler-envelope-mcp')) 'sampler-envelope-mcp' 90
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-automation-mcp.py'),$app,(Join-Path $Output 'sampler-automation-mcp')) 'sampler-automation-mcp' 90
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-sampler-controller-mcp.py'),$app,(Join-Path $Output 'sampler-controller-mcp')) 'sampler-controller-mcp' 90
    Invoke-Owned $runner @('--sampler-controller-check',(Join-Path $Output 'sampler-controller-core')) 'sampler-controller-core' 60
    Invoke-Owned $runner @('--sampler-envelope-check',(Join-Path $Output 'sampler-envelope')) 'sampler-envelope' 60
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-loop-bounds-mcp.py'),$app,(Join-Path $Output 'loop-bounds-mcp')) 'loop-bounds-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-normalize-mcp.py'),$app,(Join-Path $Output 'normalize-mcp')) 'normalize-mcp'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-clip-groups-mcp.py'),$app,(Join-Path $Output 'clip-groups-mcp')) 'clip-groups-mcp'
    Invoke-Owned $app @('--clip-groups-ui-check',(Join-Path $Output 'clip-groups-ui')) 'clip-groups-ui'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-note-mute-mcp.py'),$app,(Join-Path $Output 'note-mute-mcp')) 'note-mute-mcp'
    Invoke-Owned $app @('--note-mute-ui-check',(Join-Path $Output 'note-mute-ui')) 'note-mute-ui'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-note-slice-mcp.py'),$app,(Join-Path $Output 'note-slice-mcp')) 'note-slice-mcp'
    Invoke-Owned $app @('--note-slice-ui-check',(Join-Path $Output 'note-slice-ui')) 'note-slice-ui'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-note-stretch-mcp.py'),$app,(Join-Path $Output 'note-stretch-mcp')) 'note-stretch-mcp'
    Invoke-Owned $app @('--note-stretch-ui-check',(Join-Path $Output 'note-stretch-ui')) 'note-stretch-ui'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-note-stretch-batch.py'),$app,(Join-Path $Output 'note-stretch-batch')) 'note-stretch-batch'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-routing-mcp.py'),$app,(Join-Path $Output 'mixer-routing-mcp')) 'mixer-routing-mcp'
    Invoke-Owned $app @('--mixer-routing-ui-check',(Join-Path $Output 'mixer-routing-ui')) 'mixer-routing-ui'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-controls-mcp.py'),$app,(Join-Path $Output 'mixer-controls-mcp')) 'mixer-controls-mcp'
    Invoke-Owned $app @('--mixer-controls-ui-check',(Join-Path $Output 'mixer-controls-ui')) 'mixer-controls-ui'
    Invoke-Owned $runner @('--mixer-controls-check',(Join-Path $Output 'mixer-controls-audio')) 'mixer-controls-audio'
    Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-selection-mcp.py'),$app,(Join-Path $Output 'selection-mcp')) 'selection-mcp'
    # Current Mixer module: run the same owned workflows against staging AND final.
    # The provenance marker preserves compatibility with the prior qualified package.
    if ($mixerModule) {
        $parameterFixture=Join-Path $folder 'VibeRaParameterFixture.vst3'
        $busFixture=Join-Path $folder 'VibeRaBusChoiceFixture.vst3'
        if (-not (Test-Path -LiteralPath $busFixture -PathType Leaf)) {throw 'Packaged native bus fixture missing; no build-folder fallback'}
        Invoke-Owned $runner @('--plugin-main-io-model-check') 'plugin-main-io-model' 30
        Invoke-Owned $runner @('--plugin-bus-choice-check',(Join-Path $Output 'plugin-main-bus-choice'),$busFixture) 'plugin-main-bus-choice' 90
        Invoke-Owned $app @('--plugin-main-io-check',(Join-Path $Output 'plugin-main-io-application'),$busFixture) 'plugin-main-io-application' 90
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-plugin-io-mcp.py'),$app,(Join-Path $Output 'plugin-io-mcp'),$busFixture) 'plugin-io-mcp' 120
        $mixerFixture=Join-Path $folder 'VibeRaMixerLatencyFixture.vst3'
        foreach ($fixture in @($parameterFixture,$mixerFixture)) {
            if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) {throw 'Packaged original Mixer fixture missing; no build-folder fallback'}
        }
        # --prepare-seed includes the actual SDK GUI chain workflow once.
        # automation-native includes production send automation; the optional native probe is distinct.
        # The two-fixture Studio routing branch includes actual detector assignment and export.
        Invoke-Owned $runner @('--mixer-organisation-check',(Join-Path $Output 'mixer-organisation')) 'mixer-organisation' 30
        Invoke-Owned $runner @('--mixer-macro-configuration-check',(Join-Path $Output 'mixer-macro-configuration')) 'mixer-macro-configuration' 30
        Invoke-Owned $app @('--mixer-organisation-ui-check',(Join-Path $Output 'mixer-organisation-gui')) 'mixer-organisation-gui' 30
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-organisation-mcp.py'),$app,(Join-Path $Output 'mixer-organisation-mcp')) 'mixer-organisation-mcp' 60
        Invoke-Owned $runner @('--mixer-insert-order-check',(Join-Path $Output 'mixer-insert-order')) 'mixer-insert-order' 30
        Invoke-Owned $app @('--mixer-insert-order-ui-check',(Join-Path $Output 'mixer-insert-order-gui')) 'mixer-insert-order-gui' 60
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-insert-order-mcp.py'),$app,(Join-Path $Output 'mixer-insert-order-mcp')) 'mixer-insert-order-mcp' 60
        Invoke-Owned $runner @('--mixer-stereo-check',(Join-Path $Output 'mixer-stereo-controls')) 'mixer-stereo-controls' 30
        Invoke-Owned $runner @('--mixer-stereo-native-check',(Join-Path $Output 'mixer-stereo-native')) 'mixer-stereo-native' 60
        Invoke-Owned $app @('--mixer-stereo-ui-check',(Join-Path $Output 'mixer-stereo-gui')) 'mixer-stereo-gui' 45
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-stereo-mcp.py'),$app,(Join-Path $Output 'mixer-stereo-mcp')) 'mixer-stereo-mcp' 120
        Invoke-Owned $app @('--mixer-effects-ui-check',(Join-Path $Output 'mixer-effect-slots-gui')) 'mixer-effect-slots-gui' 30
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-effects-mcp.py'),$app,(Join-Path $Output 'mixer-effect-slots-mcp')) 'mixer-effect-slots-mcp' 45
        Invoke-Owned $app @('--mixer-effects-opening-check',(Join-Path $Output 'mixer-effect-opening'),$parameterFixture) 'mixer-effect-opening' 60
        Invoke-Owned $app @('--mixer-effects-opening-unsupported-check',(Join-Path $Output 'mixer-effect-opening-unsupported'),$parameterFixture) 'mixer-effect-opening-unsupported' 60
        Invoke-Owned $runner @('--mixer-chain-data-check',(Join-Path $Output 'mixer-chain-data')) 'mixer-chain-data' 30
        Invoke-Owned $runner @('--mixer-chain-file-jobs-check',(Join-Path $Output 'mixer-chain-file-jobs')) 'mixer-chain-file-jobs' 30
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-chain-mcp.py'),'--prepare-seed',$app,$parameterFixture,(Join-Path $Output 'mixer-chain-mcp')) 'mixer-chain-mcp' 90
        Invoke-Owned $runner @('--mixer-chain-controls-check',(Join-Path $Output 'mixer-chain-controls')) 'mixer-chain-controls' 30
        Invoke-Owned $runner @('--plugin-wet-mix-check',(Join-Path $Output 'plugin-host-wet-mix'),$mixerFixture) 'plugin-host-wet-mix' 45
        Invoke-Owned $runner @('--mixer-chain-live-export-check',(Join-Path $Output 'mixer-chain-live-export'),$parameterFixture) 'mixer-chain-live-export' 60
        Invoke-Owned $runner @('--mixer-automation-core-check',(Join-Path $Output 'mixer-automation-core')) 'mixer-automation-core' 60
        Invoke-Owned $runner @('--mixer-automation-native-check',(Join-Path $Output 'mixer-automation-native')) 'mixer-automation-native' 60
        Invoke-Owned $app @('--mixer-automation-ui-check',(Join-Path $Output 'mixer-automation-gui')) 'mixer-automation-gui' 60
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-mixer-automation-mcp.py'),$app,(Join-Path $Output 'mixer-automation-mcp')) 'mixer-automation-mcp' 90
        Invoke-Owned $app @('--controller-check',(Join-Path $Output 'controller-mapping-gui')) 'controller-mapping-gui' 60
        Invoke-Owned $runner @('--native-scalar-core-check') 'mixer-native-scalar-core' 60
        Invoke-Owned $app @('--native-scalar-history-check',(Join-Path $Output 'mixer-native-scalar-gui'),$parameterFixture) 'mixer-native-scalar-gui' 90
        Invoke-Owned $runner @('--automation-touch-core-check',(Join-Path $Output 'mixer-automation-take-core')) 'mixer-automation-take-core' 60
        Invoke-Owned $app @('--automation-touch-check',(Join-Path $Output 'mixer-automation-take-gui')) 'mixer-automation-take-gui' 90
        Invoke-Owned $app @('--studio-channel-routing-check',(Join-Path $Output 'studio-channel-routing-provider'),$parameterFixture,$mixerFixture) 'studio-channel-routing-provider' 90
        Invoke-Owned $app @('--studio-channel-routing-same-config-check',(Join-Path $Output 'studio-channel-routing-same-config'),$parameterFixture) 'studio-channel-routing-same-config' 90
        Invoke-Owned $runner @('--mixer-target-choices-check',(Join-Path $Output 'mixer-target-choices')) 'mixer-target-choices' 30
        Invoke-Owned $app @('--mixer-targets-ui-check',(Join-Path $Output 'mixer-target-choices-gui')) 'mixer-target-choices-gui' 60
        Invoke-Owned $app @('--plugin-save-capture-check',(Join-Path $Output 'mixer-plugin-save-capture'),$parameterFixture) 'mixer-plugin-save-capture' 60
        Invoke-Owned $runner @('--plugin-recovery-check',(Join-Path $Output 'mixer-plugin-recovery'),$parameterFixture) 'mixer-plugin-recovery' 90
        Invoke-Owned $runner @('--deferred-device-candidate-fault-check',(Join-Path $Output 'deferred-device-candidate-fault'),$parameterFixture) 'deferred-device-candidate-fault' 90
        Invoke-Owned $runner @('--midi-take-recovery-check',(Join-Path $Output 'midi-take-recovery')) 'midi-take-recovery' 30
        Invoke-Owned $app @('--midi-take-recovery-ui-check',(Join-Path $Output 'midi-take-recovery-gui')) 'midi-take-recovery-gui' 60
        Invoke-Owned $app @('--history-ui-recovery-check',(Join-Path $Output 'history-ui-recovery')) 'history-ui-recovery' 60
        Invoke-Owned $runner @('--default-dynamic-latency-check',(Join-Path $Output 'mixer-default-dynamic-latency'),$mixerFixture) 'mixer-default-dynamic-latency' 90
        Invoke-Owned $app @('--parameter-ui-check',(Join-Path $Output 'plugin-parameters-gui'),$parameterFixture) 'plugin-parameters-gui' 90
        Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-plugin-editors-mcp.py'),$app,(Join-Path $Output 'plugin-editors-mcp'),$parameterFixture,$mixerFixture) 'plugin-editors-mcp' 120
    }
    if (-not $Quick) {
        Invoke-Owned $app @('--sampler-gesture-check',(Join-Path $Output 'sampler-gestures')) 'sampler-gestures'
        Invoke-Owned $runner @((Join-Path $Output 'musical')) 'musical'
        Invoke-Owned $runner @('--sampler-preview-check',(Join-Path $Output 'audio-recovery')) 'audio-recovery'
        Invoke-Owned $runner @('--parameter-check',(Join-Path $Output 'parameters'),(Join-Path $folder 'VibeRaParameterFixture.vst3')) 'parameters'
        Invoke-Owned $app @('--catalogue-controls-check',(Join-Path $Output 'catalogue-controls'),(Join-Path $folder 'VibeRaParameterFixture.vst3')) 'catalogue-controls' 30
        if ($browserLive) {
            Invoke-Owned $runner @('--browser-search-check',(Join-Path $Output 'browser-search')) 'browser-search'
            Invoke-Owned $app @('--midi-file-check',(Join-Path $Output 'browser-ui')) 'browser-ui'
            Invoke-Owned $app @('--browser-live-check',(Join-Path $Output 'browser-live')) 'browser-live'
        }
        $instrument='C:/Program Files/Common Files/VST3/Arturia/Analog Lab V.vst3'
        $effect='C:/Program Files/Common Files/VST3/MeldaProduction/Modulation/MVibratoMB.vst3'
        if ((Test-Path -LiteralPath $instrument) -and (Test-Path -LiteralPath $effect)) {
            $provider=Join-Path $Output 'installed-plugins'
            Invoke-Owned $runner @('--plugin-check',$provider,$instrument,$effect) 'installed-plugins'
            # Actual Analog Lab catalogue is inspected by this branch; no synthetic substitute.
            if ($mixerModule) {
                Invoke-Owned $app @('--native-scalar-history-installed-check',(Join-Path $Output 'mixer-native-scalar-installed'),$instrument) 'mixer-native-scalar-installed' 90
            }
            Invoke-Owned $runner @('--normalize-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'normalize-vst3-live')) 'normalize-vst3-live'
            Invoke-Owned $runner @('--sampler-filter-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'sampler-filter-vst3-live')) 'sampler-filter-vst3-live' 60
            Invoke-Owned $runner @('--clip-groups-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'clip-groups-vst3-live')) 'clip-groups-vst3-live'
            Invoke-Owned $runner @('--note-mute-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'note-mute-vst3-live')) 'note-mute-vst3-live'
            Invoke-Owned $runner @('--note-slice-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'note-slice-vst3-live')) 'note-slice-vst3-live'
            Invoke-Owned $runner @('--note-stretch-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'note-stretch-vst3-live')) 'note-stretch-vst3-live'
            Invoke-Owned $runner @('--mixer-routing-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'mixer-routing-vst3-live')) 'mixer-routing-vst3-live'
            Invoke-Owned $runner @('--loop-bounds-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'loop-bounds-vst3-live')) 'loop-bounds-vst3-live'
            Invoke-Owned $runner @('--rack-groups-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'rack-vst3-live')) 'rack-vst3-live'
            Invoke-Owned $runner @('--step-fill-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'step-fill-vst3-live')) 'step-fill-vst3-live'
            Invoke-Owned $runner @('--channel-solo-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'channel-solo-vst3-live')) 'channel-solo-vst3-live'
            Invoke-Owned $runner @('--channel-management-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'channel-reorder-vst3-live')) 'channel-reorder-vst3-live'
            Invoke-Owned $app @('--channel-management-check',(Join-Path $Output 'channel-management-vst3'),(Join-Path $provider 'hosted-project.vibra')) 'channel-management-vst3'
            Invoke-Owned $runner @('--step-velocity-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'step-velocity-vst3-live')) 'step-velocity-vst3-live'
            Invoke-Owned $runner @('--swing-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'swing-vst3-live')) 'swing-vst3-live'
            Invoke-Owned $runner @('--quantizer-provider-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'quantizer-vst3-live')) 'quantizer-vst3-live'
            Invoke-Owned $app @('--step-velocity-check',(Join-Path $Output 'step-velocity-vst3'),(Join-Path $provider 'hosted-project.vibra')) 'step-velocity-vst3'
            if (-not (Get-Content -LiteralPath (Join-Path $Output 'step-velocity-vst3/mixer-routing-held-take.txt') -Raw).StartsWith('PASS')) {throw 'Mixer routing held-recording gate failed'}
            Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-installed-mcp.py'),$app,(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'installed-plugin-mcp')) 'installed-plugin-mcp'
            Invoke-Owned (Get-Command python).Source @((Join-Path $root 'scripts/verify-catalogue-mcp.py'),$app,(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'catalogue-mcp')) 'catalogue-mcp'
            if ($nativeClips) {
                Invoke-Owned $runner @('--parameter-provider-check',(Join-Path $Output 'native-clips-instrument'),$instrument) 'native-clips-instrument'
                Invoke-Owned $runner @('--parameter-provider-check',(Join-Path $Output 'native-clips-effect'),$effect) 'native-clips-effect'
            }
            Invoke-Owned $app @('--instrument-check',(Join-Path $provider 'hosted-project.vibra'),(Join-Path $Output 'instrument')) 'instrument'
            Invoke-Owned $app @('--audio-playlist-live-check',(Join-Path $Output 'live-hosted'),(Join-Path $provider 'mixed-audio-plugin.vibra')) 'live-hosted'
            if ($browserLive) {
                Invoke-Owned $app @('--browser-live-check',(Join-Path $Output 'browser-live-vst3'),(Join-Path $provider 'hosted-project.vibra')) 'browser-live-vst3'
            }
            $result.installedPlugins='Analog Lab V/MVibratoMB scan, state, save/reopen, native editor, virtual playback and WAV; expired MVibrato demo limits audio-quality claims'
        } else {$result.installedPlugins='Unavailable: installed providers absent; not qualified'}
    }
    $result.finished=$true
    $result.passed=$failures.Count -eq 0
    if (-not $result.passed) {throw 'Distribution checks completed with recorded failures; inspect verification.json'}
} finally {
    $result.processes=$receipts
    $result.failures=$failures
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'verification.json') -Encoding UTF8
}
Write-Output "PASS selected distribution: $folder"
