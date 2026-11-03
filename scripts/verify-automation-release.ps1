param([ValidateSet('Release','Debug')][string]$Configuration='Release',
      [ValidateRange(1,20)][int]$ControllerRuns=6,
      [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$versionMatch=[regex]::Match((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw),'project\(VibeRa VERSION ([0-9.]+)')
if (-not $versionMatch.Success) {throw 'Cannot identify the current application version'}
$version=$versionMatch.Groups[1].Value
$app=Join-Path $root "build/windows/VibeRa_artefacts/$Configuration/VibeRa Studio.exe"
$runner=Join-Path $root "build/windows/VibeRaTests_artefacts/$Configuration/VibeRaTests.exe"
$worker=Join-Path $root "build/windows/VibeRaPluginWorker_artefacts/$Configuration/VibeRa Plugin Worker.exe"
$python=(Get-Command python).Source
$powershell=(Get-Command powershell).Source
$proofPath=Join-Path $root 'build/automation-source-qualification.json'
$proof=[ordered]@{finished=$false;exitCode=1;configuration=$Configuration;controllerRuns=@();
    retainedTimingOutlier=@{wallMs=27.9823;deadlineMs=(256000. / 48000.);cause='unestablished';
        evidence='build/automation-controller-deadline-failure';resolution='Not resolved by later passing runs; no hardware or full real-time claim'};
    sourceHashes=@();ownedProcesses=@()}
if (Test-Path -LiteralPath (Join-Path $root 'build/automation-sampler-deadline-failure')) {
    $proof.retainedSamplerTimingOutlier=@{wallMs=5.4364;deadlineMs=(256000. / 48000.);mode='Stopped sampler preview';cause='unestablished';
        evidence='build/automation-sampler-deadline-failure';resolution='Failure retained; later passes do not establish its cause'}
}
if (Test-Path -LiteralPath (Join-Path $root 'build/automation-bundled-sampler-failure')) {
    $proof.retainedBundledTimingOutlier=@{wallMs=5.9334;deadlineMs=(256000. / 48000.);mode='Packaged sampler gesture Pattern';cause='unestablished';
        evidence='build/automation-bundled-sampler-failure';resolution='All recorded model/MIDI/graph/device flags retained; failure remains unqualified'}
}
function Invoke-Owned([string]$Executable,[string[]]$Arguments,[string]$Purpose,[string]$Log) {
    $quoted=@($Arguments | ForEach-Object {'"'+$_+'"'})
    $process=Start-Process -FilePath $Executable -ArgumentList $quoted -PassThru -WindowStyle Hidden -RedirectStandardOutput $Log -RedirectStandardError ($Log+'.error')
    $handle=$process.Handle
    $receipt=[ordered]@{pid=$process.Id;creation=$process.StartTime.ToUniversalTime().ToString('o');purpose=$Purpose;
        executableSha256=(Get-FileHash -LiteralPath $Executable).Hash;exitCode=$null}
    $receipt | ConvertTo-Json | Set-Content -LiteralPath ($Log+'.process.json') -Encoding UTF8
    $process.WaitForExit(); $receipt.exitCode=$process.ExitCode
    $receipt | ConvertTo-Json | Set-Content -LiteralPath ($Log+'.process.json') -Encoding UTF8
    $script:proof.ownedProcesses += $receipt
    if ($process.ExitCode -ne 0) {throw "$Purpose failed: $($process.ExitCode); $Log"}
    return $receipt
}
function Source-Hashes {
    $files=@(Get-ChildItem -LiteralPath (Join-Path $root 'src'),(Join-Path $root 'tests'),(Join-Path $root 'assets') -Recurse -File |
        Where-Object {$_.FullName -notmatch '[\\/]__pycache__[\\/]'})
    $files+=Get-Item -LiteralPath (Join-Path $root 'CMakeLists.txt'),(Join-Path $root 'dependencies.lock.json'),
        (Join-Path $root 'scripts/package.ps1'),(Join-Path $root 'scripts/verify-automation-release.ps1'),
        (Join-Path $root 'scripts/copy-parameter-evidence.ps1'),
        (Join-Path $root 'scripts/verify-controller-providers.ps1'),(Join-Path $root 'scripts/verify-automation-desktop.py')
    return @($files | Sort-Object FullName | ForEach-Object {
        @{path=$_.FullName.Substring($root.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
}
try {
    if (-not $SkipBuild) {
        $beforeBuild=Source-Hashes
        Invoke-Owned $powershell @('-ExecutionPolicy','Bypass','-File',(Join-Path $root 'scripts/build.ps1'),'-Configuration',$Configuration,'-CompileOnly') 'Automation release source build' (Join-Path $root 'build/automation-build.log') | Out-Null
        if (($beforeBuild | ConvertTo-Json -Compress) -cne ((Source-Hashes) | ConvertTo-Json -Compress)) {throw 'Source changed while building; do not qualify these bytes'}
    }
    foreach ($file in @($app,$runner,$worker)) {if (-not (Test-Path -LiteralPath $file)) {throw "Required executable missing: $file"}}
    $proof.applicationSha256=(Get-FileHash -LiteralPath $app).Hash
    $proof.runnerSha256=(Get-FileHash -LiteralPath $runner).Hash
    $proof.workerSha256=(Get-FileHash -LiteralPath $worker).Hash
    $proof.sourceHashes=Source-Hashes
    Invoke-Owned $powershell @('-ExecutionPolicy','Bypass','-File',(Join-Path $root 'scripts/test.ps1'),'-Configuration',$Configuration) 'Full automation source CTest without relaxed deadlines' (Join-Path $root 'build/automation-ctest.log') | Out-Null
    $ctestText=Get-Content -LiteralPath (Join-Path $root 'build/automation-ctest.log') -Raw
    $vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $ctest=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
    $inventoryText=& $ctest --test-dir (Join-Path $root 'build/windows') -C $Configuration --show-only=json-v1
    if ($LASTEXITCODE -ne 0) {throw 'Cannot read declared CTest inventory'}
    $inventory=($inventoryText -join "`n") | ConvertFrom-Json
    $proof.ctestInventory=@($inventory.tests.name)
    $summary=[regex]::Match($ctestText,'100% tests passed, 0 tests failed out of ([0-9]+)')
    if (-not $summary.Success -or [int]$summary.Groups[1].Value -ne $inventory.tests.Count) {throw 'Complete declared CTest qualification missing'}
    foreach ($required in @('musical_acceptance','gui_workflow','automation_foundation','coproducer_context','midi_interchange_gui','audio_playlist_gui','audio_playlist_live','sampler_gestures_gui','controller_mapping_gui','automation_clip_gui')) {
        if ($proof.ctestInventory -notcontains $required) {throw "Required CTest suite missing: $required"}
    }
    $gui=Get-Content -LiteralPath (Join-Path $root 'build/windows/automation_clip_gui/automation-gui-verification.txt') -Raw
    $take=Get-Content -LiteralPath (Join-Path $root 'build/windows/automation_clip_gui/automation-midi-take.txt') -Raw
    if (-not $gui.StartsWith('PASS') -or -not $gui.Contains('source-window trim') -or
        -not $take.StartsWith('PASS') -or -not $take.Contains('Pattern') -or -not $take.Contains('Song')) {
        throw 'Point/source-window/MIDI take GUI qualification missing'
    }
    $bound=Join-Path $root 'build/automation-controller-bound'
    New-Item -ItemType Directory -Force -Path $bound | Out-Null
    for ($run=1;$run -le $ControllerRuns;$run++) {
        $output=Join-Path $bound ('run-'+$run)
        $receipt=Invoke-Owned $app @('--controller-check',$output) "Bounded controller run $run/$ControllerRuns" (Join-Path $bound ('run-'+$run+'.log'))
        $report=[ordered]@{run=$run;pid=$receipt.pid;creation=$receipt.creation;applicationSha256=$proof.applicationSha256;exitCode=0;modes=@()}
        foreach ($mode in @('pattern','song')) {
            $path=Join-Path $output "controller-$mode-callbacks.csv"
            $rows=@(Import-Csv -LiteralPath $path)
            if ($rows.Count -ne 1280) {throw "Incomplete $mode callback samples in $path"}
            $values=@($rows | ForEach-Object {
                $value=[double]::Parse($_.wallMs,[Globalization.CultureInfo]::InvariantCulture)
                if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0 -or $value -ge (256000. / 48000.) -or $_.deadlineOverrun -ne '0') {
                    throw "Measured controller deadline failure in $path; raw failed run retained"
                }
                $value
            } | Sort-Object)
            $invariants=Get-Content -LiteralPath (Join-Path $output "controller-$mode-invariants.txt") -Raw
            foreach ($flag in @('playing','retainedDevice','retainedEdit','retainedContext','retainedReplacementCount','retainedGainStages')) {
                if ($invariants -notmatch ($flag+'=1(?:\s|$)')) {throw "Lifecycle invariant $flag missing for $mode"}
            }
            $report.modes+=@{name=$mode;callbacks=1280;overruns=0;maximumMs=$values[-1];medianMs=$values[640];p99Ms=$values[1267];p999Ms=$values[1278];
                rawCsv=$path.Substring($root.Length+1);scope='Wall deadline unchanged; raw thread cycles are diagnostic only'}
        }
        $proof.controllerRuns+=$report
        $proof | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    }
    $mcpOutput=Join-Path $root 'build/automation-mcp'
    Invoke-Owned $python @((Join-Path $root 'tests/mcp_acceptance.py'),$app,$mcpOutput,$version) 'Matching automation MCP regression' (Join-Path $root 'build/automation-mcp.log') | Out-Null
    $mcp=Get-Content -LiteralPath (Join-Path $mcpOutput 'verification.json') -Raw | ConvertFrom-Json
    if ($mcp.exitCode -ne 0 -or $mcp.applicationSha256 -ne $proof.applicationSha256) {throw 'MCP receipt does not match application'}
    $after=Source-Hashes
    if (($proof.sourceHashes | ConvertTo-Json -Compress) -cne ($after | ConvertTo-Json -Compress) -or
        $proof.applicationSha256 -ne (Get-FileHash -LiteralPath $app).Hash -or
        $proof.runnerSha256 -ne (Get-FileHash -LiteralPath $runner).Hash) {throw 'Sources/executables changed during qualification'}
    $proof.exitCode=0
} finally {
    $proof.finished=$true
    $proof | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    # Specialized source qualification uses temporary canonical compilation
    # products; it must never leave another runnable application behind.
    & (Join-Path $PSScriptRoot 'clear-build-runnables.ps1')
}
Write-Output "Automation source qualification completed; retained timing failure remains unestablished: $proofPath"
